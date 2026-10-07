#include "solar_os_writer_checkpoint.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "solar_os_task.h"
#include "solar_os_perf.h"

#define CHECKPOINT_CLIENTS 16
#define CHECKPOINT_HEADER 200U
#define CHECKPOINT_STACK 6144U
static const uint8_t magic[8] = {'S','O','L','C','P',1,'\r','\n'};
struct solar_os_writer_checkpoint {
    char recovery[SOLAR_OS_STORAGE_PATH_MAX], document[SOLAR_OS_STORAGE_PATH_MAX];
    uint8_t *pending;
    solar_os_writer_checkpoint_status_t status;
};
static struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
    solar_os_writer_checkpoint_t *clients[CHECKPOINT_CLIENTS];
    unsigned next, count;
    bool stopping, done;
} service;
static uint32_t next_generation;
static void cp_lock(void) { xSemaphoreTake(service.mutex, portMAX_DELAY); }
static void cp_unlock(void) { xSemaphoreGive(service.mutex); }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t record_version(const uint8_t *p) { return get32(p+24)|((uint64_t)get32(p+28)<<32); }
static size_t record_size(const uint8_t *p) { return CHECKPOINT_HEADER+get32(p+8); }
static uint32_t checksum(const uint8_t *p, size_t n)
{
    uint32_t hash=2166136261U;
    for (size_t i=0;i<n;i++) { hash ^= (i>=36 && i<40) ? 0 : p[i]; hash*=16777619U; }
    return hash;
}
static void trace(solar_os_writer_checkpoint_t *c, uint64_t v, const char *event, esp_err_t error)
{
    if (solar_os_perf_enabled()) SOLAR_OS_LOGD("checkpoint",
        "gen=%" PRIu32 " ver=%" PRIu64 " %s t=%" PRId64 " err=%d",
        c->status.generation, v, event, esp_timer_get_time(), error);
}
static uint8_t *snapshot(const char *document, const solar_os_writer_buffer_t *buffer,
                        const solar_os_writer_checkpoint_meta_t *m)
{
    const size_t len=m->saved ? 0 : solar_os_writer_buffer_length(buffer);
    if (len>SOLAR_OS_WRITER_BUFFER_MAX_BYTES || strlen(document)>=160 || m->zoom>4) return NULL;
    uint8_t *p=heap_caps_malloc(CHECKPOINT_HEADER+len+1, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!p) return NULL;
    memset(p,0,CHECKPOINT_HEADER);
    memcpy(p,magic,8); put32(p+8,len); put32(p+12,m->cursor); put32(p+16,m->anchor);
    put32(p+20,m->zoom); put32(p+24,m->version); put32(p+28,m->version>>32);
    put32(p+32,m->saved); memcpy(p+40,document,strlen(document)+1);
    if (len && solar_os_writer_buffer_copy(buffer,0,len,(char *)p+CHECKPOINT_HEADER)!=ESP_OK) {
        heap_caps_free(p); return NULL;
    }
    p[CHECKPOINT_HEADER+len]=0;
    return p;
}
static void observe(void *arg, const char *event)
{
    solar_os_writer_checkpoint_t *c=arg;
    trace(c,c->status.inflight,event,ESP_OK);
}
static esp_err_t write_record(solar_os_writer_checkpoint_t *c, const char *path, uint8_t *p)
{
    put32(p+36,checksum(p,record_size(p)));
    return solar_os_writer_safe_replace_observed(path,(char *)p,record_size(p),
        SOLAR_OS_WRITER_FILE_FAULT_NONE,observe,c);
}
static void drop_pending(solar_os_writer_checkpoint_t *c)
{
    if (!c->pending) return;
    c->status.snapshot_bytes-=record_size(c->pending)+1;
    heap_caps_free(c->pending); c->pending=NULL; c->status.pending=0;
}
static void checkpoint_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        for (;;) {
            cp_lock();
            if (service.stopping) {
                service.done=true; cp_unlock();
                solar_os_task_delete_internal(NULL); return;
            }
            solar_os_writer_checkpoint_t *c=NULL;
            for (unsigned n=0;n<CHECKPOINT_CLIENTS;n++) {
                unsigned i=(service.next+n)%CHECKPOINT_CLIENTS;
                if (service.clients[i] && service.clients[i]->pending && !service.clients[i]->status.paused) {
                    c=service.clients[i]; service.next=(i+1)%CHECKPOINT_CLIENTS; break;
                }
            }
            if (!c) { cp_unlock(); break; }
            uint8_t *p=c->pending; c->pending=NULL;
            uint64_t v=record_version(p);
            c->status.pending=0; c->status.busy=true; c->status.inflight=v;
            cp_unlock();
            trace(c,v,"CHECKPOINT_WORKER_START",ESP_OK);
            esp_err_t err=write_record(c,c->recovery,p);
            const size_t bytes=record_size(p)+1;
            heap_caps_free(p);
            /* No reference to c after publishing busy=false: close may free it. */
            cp_lock();
            c->status.snapshot_bytes-=bytes;
            /* A later snapshot-allocation failure must survive an older success. */
            if (err!=ESP_OK || c->status.requested<=v) c->status.error=err;
            c->status.stack_free=uxTaskGetStackHighWaterMark(NULL);
            if (err==ESP_OK) { c->status.completed=v; c->status.writes++; }
            else { c->status.errors++; drop_pending(c); }
            trace(c,v,err==ESP_OK ? "CHECKPOINT_COMPLETE" : "CHECKPOINT_ERROR",err);
            c->status.inflight=0; c->status.busy=false;
            cp_unlock();
        }
    }
}
esp_err_t solar_os_writer_checkpoint_available(solar_os_writer_checkpoint_t *self,
    const char *recovery, const char *document)
{
    if (!recovery || !document || !recovery[0] || strlen(recovery)>=SOLAR_OS_STORAGE_PATH_MAX ||
        strlen(document)>=SOLAR_OS_STORAGE_PATH_MAX) return ESP_ERR_INVALID_ARG;
    /* Registry is application-task-owned. Worker never changes client paths. */
    for (unsigned i=0;i<CHECKPOINT_CLIENTS;i++) {
        solar_os_writer_checkpoint_t *c=service.clients[i];
        if (c && c!=self && (!strcasecmp(c->recovery,recovery) || !strcasecmp(c->document,document)))
            return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
esp_err_t solar_os_writer_checkpoint_open(const char *recovery, const char *document,
    solar_os_writer_checkpoint_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    if (service.stopping) return ESP_ERR_INVALID_STATE;
#ifndef SOLAR_OS_WRITER_HOST_TEST
    const size_t internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const size_t external_before=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
    esp_err_t err=solar_os_writer_checkpoint_available(NULL,recovery,document);
    if (err!=ESP_OK) return err;
    if (service.count==CHECKPOINT_CLIENTS) return ESP_ERR_NO_MEM;
    solar_os_writer_checkpoint_t *c=heap_caps_calloc(1,sizeof(*c),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!c) return ESP_ERR_NO_MEM;
    strcpy(c->recovery,recovery); strcpy(c->document,document);
    c->status.generation=++next_generation;
    if (!service.mutex) {
        service.mutex=xSemaphoreCreateMutex();
        if (!service.mutex) { heap_caps_free(c); return ESP_ERR_NO_MEM; }
        if (solar_os_task_create_pinned_internal(checkpoint_worker,"writer_checkpoint",CHECKPOINT_STACK,
                NULL,1,&service.task,tskNO_AFFINITY,SOLAR_OS_TASK_ROLE_SYSTEM)!=pdPASS) {
            vSemaphoreDelete(service.mutex); memset(&service,0,sizeof(service));
            heap_caps_free(c); return ESP_ERR_NO_MEM;
        }
    }
    cp_lock();
    for (unsigned i=0;i<CHECKPOINT_CLIENTS;i++) if (!service.clients[i]) { service.clients[i]=c; break; }
    service.count++; cp_unlock(); *out=c;
#ifndef SOLAR_OS_WRITER_HOST_TEST
    SOLAR_OS_LOGI("checkpoint","open gen=%u clients=%u stack=%u internal=%u->%u psram=%u->%u",
        (unsigned)c->status.generation,service.count,CHECKPOINT_STACK,
        (unsigned)internal_before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)external_before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#endif
    return ESP_OK;
}
void solar_os_writer_checkpoint_status(solar_os_writer_checkpoint_t *c, solar_os_writer_checkpoint_status_t *s)
{
    memset(s,0,sizeof(*s)); if (!c) return;
    cp_lock(); *s=c->status; cp_unlock();
}
esp_err_t solar_os_writer_checkpoint_submit(solar_os_writer_checkpoint_t *c,
    const solar_os_writer_buffer_t *buffer, const solar_os_writer_checkpoint_meta_t *m)
{
    if (!c || !buffer || !m || !m->version || m->saved) return ESP_ERR_INVALID_ARG;
    trace(c,m->version,"CHECKPOINT_REQUEST",ESP_OK);
    cp_lock();
    if (c->status.paused || service.stopping) { cp_unlock(); return ESP_ERR_INVALID_STATE; }
    if (m->version<c->status.requested) { cp_unlock(); return ESP_ERR_INVALID_ARG; }
    c->status.requested=m->version;
    if (c->pending) {
        trace(c,c->status.pending,"CHECKPOINT_REPLACED_PENDING",ESP_OK);
        c->status.replaced++; drop_pending(c);
    }
    cp_unlock();
    trace(c,m->version,"CHECKPOINT_SNAPSHOT_START",ESP_OK);
    const int64_t copy_start=esp_timer_get_time();
    uint8_t *p=snapshot(c->document,buffer,m);
    const int64_t copy_us=esp_timer_get_time()-copy_start;
    trace(c,m->version,"CHECKPOINT_SNAPSHOT_END",p ? ESP_OK : ESP_ERR_NO_MEM);
    if (solar_os_perf_enabled()) SOLAR_OS_LOGD("checkpoint",
        "gen=%" PRIu32 " ver=%" PRIu64 " SNAPSHOT bytes=%u copy_us=%" PRId64,
        c->status.generation,m->version,p ? (unsigned)record_size(p) : 0,copy_us);
    cp_lock();
    if (!p) {
        c->status.error=ESP_ERR_NO_MEM; c->status.errors++;
        trace(c,m->version,"CHECKPOINT_ERROR",ESP_ERR_NO_MEM); cp_unlock(); return ESP_ERR_NO_MEM;
    }
    c->pending=p; c->status.pending=m->version; c->status.submitted++; c->status.error=ESP_OK;
    c->status.snapshot_bytes+=record_size(p)+1;
    if (c->status.snapshot_bytes>c->status.peak_snapshot_bytes) c->status.peak_snapshot_bytes=c->status.snapshot_bytes;
    trace(c,m->version,"CHECKPOINT_SUBMIT",ESP_OK);
    cp_unlock(); xTaskNotifyGive(service.task); return ESP_OK;
}
void solar_os_writer_checkpoint_fence(solar_os_writer_checkpoint_t *c)
{
    if (!c) return;
    cp_lock(); c->status.paused=true;
    trace(c,c->status.inflight,"CHECKPOINT_FENCE_START",ESP_OK);
    if (c->pending) { c->status.cancelled++; drop_pending(c); }
    cp_unlock();
    solar_os_writer_checkpoint_status_t s;
    do { solar_os_writer_checkpoint_status(c,&s); if (s.busy) vTaskDelay(pdMS_TO_TICKS(1)); } while (s.busy);
    trace(c,s.completed,"CHECKPOINT_FENCE_END",s.error);
}
void solar_os_writer_checkpoint_resume(solar_os_writer_checkpoint_t *c)
{
    if (!c) return;
    cp_lock(); c->status.paused=false; cp_unlock(); xTaskNotifyGive(service.task);
}
esp_err_t solar_os_writer_checkpoint_wait(solar_os_writer_checkpoint_t *c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    solar_os_writer_checkpoint_status_t s;
    do { solar_os_writer_checkpoint_status(c,&s); if (s.busy || s.pending) vTaskDelay(pdMS_TO_TICKS(1)); }
    while (s.busy || s.pending);
    return s.error;
}
esp_err_t solar_os_writer_checkpoint_rebind(solar_os_writer_checkpoint_t *c,
    const char *recovery, const char *document)
{
    if (!c || !c->status.paused || c->status.busy) return ESP_ERR_INVALID_STATE;
    esp_err_t err=solar_os_writer_checkpoint_available(c,recovery,document);
    if (err!=ESP_OK) return err;
    cp_lock(); strcpy(c->recovery,recovery); strcpy(c->document,document);
    c->status.completed=0; c->status.requested=0; cp_unlock(); return ESP_OK;
}
esp_err_t solar_os_writer_checkpoint_commit(solar_os_writer_checkpoint_t *c,
    const char *recovery, const char *document, const solar_os_writer_buffer_t *buffer,
    const solar_os_writer_checkpoint_meta_t *m)
{
    if (!c || !m || (!m->saved && !buffer) || !c->status.paused || c->status.busy) return ESP_ERR_INVALID_STATE;
    esp_err_t err=solar_os_writer_checkpoint_available(c,recovery,document);
    if (err!=ESP_OK) return err;
    uint8_t *p=snapshot(document,buffer,m);
    if (!p) return ESP_ERR_NO_MEM;
    c->status.inflight=m->version;
    trace(c,m->version,"CHECKPOINT_BOUNDARY_START",ESP_OK);
    err=write_record(c,recovery,p); heap_caps_free(p);
    trace(c,m->version,"CHECKPOINT_BOUNDARY_END",err);
    c->status.inflight=0;
    if (err==ESP_OK && !strcmp(recovery,c->recovery)) c->status.completed=m->version;
    return err;
}
void solar_os_writer_checkpoint_close(solar_os_writer_checkpoint_t *c)
{
    if (!c) return;
    solar_os_writer_checkpoint_fence(c);
    cp_lock();
    for (unsigned i=0;i<CHECKPOINT_CLIENTS;i++) if (service.clients[i]==c) service.clients[i]=NULL;
    service.count--; bool last=service.count==0;
    if (last) service.stopping=true;
    cp_unlock(); heap_caps_free(c);
    if (!last) return;
    if (service.task) {
        xTaskNotifyGive(service.task);
        for (;;) { cp_lock(); bool done=service.done; cp_unlock(); if (done) break; vTaskDelay(pdMS_TO_TICKS(1)); }
    }
    vSemaphoreDelete(service.mutex); memset(&service,0,sizeof(service));
}
esp_err_t solar_os_writer_checkpoint_drain_all(bool shutdown)
{
    if (!service.mutex) return ESP_OK;
    const bool perf=solar_os_perf_enabled();
    unsigned active=0,pending=0;
    cp_lock();
    for (unsigned i=0;i<CHECKPOINT_CLIENTS;i++) if (service.clients[i]) {
        active+=service.clients[i]->status.busy;
        pending+=service.clients[i]->pending!=NULL;
    }
    cp_unlock();
    if (perf) SOLAR_OS_LOGD("checkpoint","DEVICE_DRAIN_START t=%" PRId64 " active=%u pending=%u shutdown=%u",
        esp_timer_get_time(),active,pending,shutdown);
    esp_err_t result=ESP_OK;
    for (unsigned i=0;i<CHECKPOINT_CLIENTS;i++) if (service.clients[i]) {
        esp_err_t err=solar_os_writer_checkpoint_wait(service.clients[i]);
        if (err!=ESP_OK) result=err;
    }
    if (shutdown && service.task) {
        cp_lock(); service.stopping=true; cp_unlock(); xTaskNotifyGive(service.task);
        for (;;) { cp_lock(); bool done=service.done; cp_unlock(); if (done) break; vTaskDelay(pdMS_TO_TICKS(1)); }
        service.task=NULL;
    }
    if (result!=ESP_OK) SOLAR_OS_LOGE("checkpoint","device boundary retained previous checkpoint: %d",result);
    if (perf) SOLAR_OS_LOGD("checkpoint","DEVICE_DRAIN_END t=%" PRId64 " err=%d",esp_timer_get_time(),result);
    return result;
}
esp_err_t solar_os_writer_checkpoint_read(const char *path, const char *document,
    char **text, size_t *len, solar_os_writer_checkpoint_meta_t *m, bool *legacy)
{
    *text=NULL; *len=0; *legacy=false; memset(m,0,sizeof(*m));
    struct stat st;
    if (stat(path,&st)) return errno==ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    if (!S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>SOLAR_OS_WRITER_BUFFER_MAX_BYTES+CHECKPOINT_HEADER)
        return ESP_ERR_INVALID_SIZE;
    size_t n=(size_t)st.st_size;
    uint8_t *p=heap_caps_malloc(n+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!p) return ESP_ERR_NO_MEM;
    FILE *f=fopen(path,"rb");
    if (!f) { heap_caps_free(p); return ESP_FAIL; }
    bool bad=fread(p,1,n,f)!=n;
    if (fclose(f)) bad=true;
    if (bad) { heap_caps_free(p); return ESP_FAIL; }
    p[n]=0;
    if (n<5 || memcmp(p,magic,5)) {
        if (n>SOLAR_OS_WRITER_BUFFER_MAX_BYTES) { heap_caps_free(p); return ESP_ERR_INVALID_SIZE; }
        *legacy=true; *text=(char *)p; *len=n; return ESP_OK;
    }
    if (n<CHECKPOINT_HEADER || memcmp(p,magic,8) || get32(p+8)>SOLAR_OS_WRITER_BUFFER_MAX_BYTES || record_size(p)!=n ||
        get32(p+32)>1 || get32(p+20)>4 || !memchr(p+40,0,160) || strcmp((char *)p+40,document) ||
        get32(p+36)!=checksum(p,n) || (get32(p+32) && get32(p+8))) {
        heap_caps_free(p); return ESP_ERR_INVALID_STATE;
    }
    m->cursor=get32(p+12); m->anchor=get32(p+16); m->zoom=get32(p+20);
    m->version=record_version(p); m->saved=get32(p+32)!=0;
    *len=get32(p+8); memmove(p,p+CHECKPOINT_HEADER,*len); p[*len]=0; *text=(char *)p;
    return ESP_OK;
}
