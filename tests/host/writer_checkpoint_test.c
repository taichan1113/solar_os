#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include "async_stubs/freertos/task.h"
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t t) { (void)t; return 2048; }
#define solar_os_writer_safe_replace_observed test_replace
#include "../../src/apps/solar_os_writer_checkpoint.c"
#undef solar_os_writer_safe_replace_observed
esp_err_t solar_os_writer_safe_replace_observed(const char *,const char *,size_t,
    solar_os_writer_file_fault_t,solar_os_writer_file_observer_t,void *);
#include "../../src/apps/solar_os_writer_files.c"
/* Run the production executor against real pthread mutexes/notifications. */
struct host_async_task {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned notifications;
    TaskFunction_t fn;
    void *arg;
};
static _Thread_local TaskHandle_t current_task;
static TaskHandle_t tasks[32];
static size_t task_count;
static int fail_allocation = -1, allocation_count, live_allocations, semaphores;
static bool fail_task;

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    if (allocation_count++ == fail_allocation) return NULL;
    void *p = malloc(size);
    if (p) live_allocations++;
    return p;
}
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    void *p = heap_caps_malloc(n * size, caps);
    if (p) memset(p, 0, n * size);
    return p;
}
void heap_caps_free(void *p) { if (p) { live_allocations--; free(p); } }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t s = malloc(sizeof(*s));
    assert(s && pthread_mutex_init(s, NULL) == 0);
    semaphores++;
    return s;
}
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)t; assert(!pthread_mutex_lock(s)); return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { assert(!pthread_mutex_unlock(s)); return 1; }
void vSemaphoreDelete(SemaphoreHandle_t s) { assert(!pthread_mutex_destroy(s)); free(s); semaphores--; }
int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
void vTaskDelay(TickType_t ticks)
{
    const struct timespec t = {.tv_sec=ticks/1000, .tv_nsec=(long)(ticks%1000)*1000000};
    nanosleep(&t, NULL);
}
unsigned ulTaskNotifyTake(int clear, TickType_t timeout)
{
    (void)clear; (void)timeout;
    TaskHandle_t t = current_task;
    pthread_mutex_lock(&t->mutex);
    while (!t->notifications) pthread_cond_wait(&t->cond, &t->mutex);
    unsigned n = t->notifications;
    t->notifications = 0;
    pthread_mutex_unlock(&t->mutex);
    return n;
}
void xTaskNotifyGive(TaskHandle_t t)
{
    pthread_mutex_lock(&t->mutex);
    t->notifications++;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
}
static void *thread_entry(void *arg)
{
    current_task = arg;
    current_task->fn(current_task->arg);
    return NULL;
}
BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t fn, const char *name, uint32_t stack,
    void *arg, UBaseType_t priority, TaskHandle_t *out, BaseType_t core, int role)
{
    (void)name; (void)stack; (void)role;
    assert(priority == 1 && core == tskNO_AFFINITY);
    if (fail_task) return 0;
    TaskHandle_t t = calloc(1, sizeof(*t));
    assert(t && task_count < 32);
    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond, NULL);
    t->fn = fn; t->arg = arg; *out = t;
    tasks[task_count++] = t;
    assert(!pthread_create(&t->thread, NULL, thread_entry, t));
    return pdPASS;
}
void solar_os_task_delete_internal(TaskHandle_t t) { assert(t == NULL); pthread_exit(NULL); }
esp_err_t solar_os_log_get_status(solar_os_log_status_t *s) { s->level=SOLAR_OS_LOG_LEVEL_INFO; return ESP_OK; }
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; return ESP_OK; }
static void reap(void)
{
    for (size_t i=0;i<task_count;i++) {
        TaskHandle_t t=tasks[i];
        pthread_join(t->thread,NULL);
        pthread_cond_destroy(&t->cond);
        pthread_mutex_destroy(&t->mutex);
        free(t);
    }
    task_count=0;
    assert(live_allocations==0 && semaphores==0);
}


static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static bool hold, entered;
static solar_os_writer_file_fault_t injected;
esp_err_t test_replace(const char *path,const char *data,size_t len,
    solar_os_writer_file_fault_t fault,solar_os_writer_file_observer_t observer,void *ctx)
{
    (void)fault;
    pthread_mutex_lock(&gate); entered=true; pthread_cond_broadcast(&gate_cond);
    while(hold) pthread_cond_wait(&gate_cond,&gate);
    solar_os_writer_file_fault_t f=injected;
    pthread_mutex_unlock(&gate);
    return solar_os_writer_safe_replace_observed(path,data,len,f,observer,ctx);
}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b)
{
    struct stat st;
    if(!stat(b,&st)) { errno=EEXIST; return -1; }
    return __real_rename(a,b);
}
static void block_write(void) { pthread_mutex_lock(&gate); hold=true; entered=false; pthread_mutex_unlock(&gate); }
static void started(void) { pthread_mutex_lock(&gate); while(!entered) pthread_cond_wait(&gate_cond,&gate); pthread_mutex_unlock(&gate); }
static void release_write(void) { pthread_mutex_lock(&gate); hold=false; pthread_cond_broadcast(&gate_cond); pthread_mutex_unlock(&gate); }
static void expect(const char *path,const char *doc,const char *value,uint64_t version,bool saved)
{
    char *text; size_t len; bool legacy; solar_os_writer_checkpoint_meta_t m;
    assert(solar_os_writer_checkpoint_read(path,doc,&text,&len,&m,&legacy)==ESP_OK);
    assert(!legacy && len==strlen(value) && !memcmp(text,value,len));
    assert(m.version==version && m.saved==saved);
    heap_caps_free(text);
}
static void *delayed_release(void *unused) { (void)unused; vTaskDelay(30); release_write(); return NULL; }
int main(void)
{
    char dir[]="/tmp/solaros-cp-XXXXXX"; assert(mkdtemp(dir));
    char path[160],other[160]; snprintf(path,sizeof(path),"%s/recovery",dir); snprintf(other,sizeof(other),"%s/other",dir);
    solar_os_writer_checkpoint_t *c=NULL,*d=NULL;
    fail_task=true; assert(solar_os_writer_checkpoint_open(path,"doc",&c)==ESP_ERR_NO_MEM); fail_task=false; reap();
    assert(solar_os_writer_checkpoint_open(path,"doc",&c)==ESP_OK);
    assert(solar_os_writer_checkpoint_open(path,"collision",&d)==ESP_ERR_INVALID_STATE);
    assert(solar_os_writer_checkpoint_open(other,"DOC",&d)==ESP_ERR_INVALID_STATE);
    assert(solar_os_writer_checkpoint_open(other,"other",&d)==ESP_OK);
    assert(task_count==1); /* shared internal stack */
    solar_os_writer_buffer_t b; solar_os_writer_buffer_init(&b);
    assert(solar_os_writer_buffer_set(&b,"v1",2)==ESP_OK);
    solar_os_writer_checkpoint_meta_t m={.version=1,.cursor=2,.anchor=1,.zoom=1};
    block_write(); assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); started();
    assert(solar_os_writer_buffer_set(&b,"latest",6)==ESP_OK);
    for(unsigned i=0;i<1000;i++) { m.version=2+i; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); }
    solar_os_writer_checkpoint_status_t s; solar_os_writer_checkpoint_status(c,&s);
    assert(s.inflight==1 && s.pending==1001 && s.completed==0 && s.replaced==999);
    assert(s.peak_snapshot_bytes<=2*(CHECKPOINT_HEADER+7));
    /* Another session is not lost by latest coalescing in this session. */
    m.version=1; assert(solar_os_writer_checkpoint_submit(d,&b,&m)==ESP_OK);
    assert(solar_os_writer_buffer_set(&b,"unsnapshotted edit",18)==ESP_OK);
    release_write(); assert(solar_os_writer_checkpoint_wait(c)==ESP_OK); assert(solar_os_writer_checkpoint_wait(d)==ESP_OK);
    expect(path,"doc","latest",1001,false); expect(other,"other","latest",1,false);
    solar_os_writer_checkpoint_status(c,&s); assert(s.completed==1001 && s.writes==2 && !s.snapshot_bytes);
    /* Allocation failure retains last durable data and retries latest. */
    fail_allocation=allocation_count; m.version=1002;
    assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_ERR_NO_MEM); fail_allocation=-1;
    expect(path,"doc","latest",1001,false);
    for(unsigned f=SOLAR_OS_WRITER_FILE_FAULT_OPEN;f<=SOLAR_OS_WRITER_FILE_FAULT_SHORT_WRITE;f++) {
        injected=f; m.version++;
        assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK);
        assert(solar_os_writer_checkpoint_wait(c)!=ESP_OK);
        expect(path,"doc","latest",1001,false);
    }
    injected=0; m.version++;
    assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK);
    assert(solar_os_writer_checkpoint_wait(c)==ESP_OK);
    expect(path,"doc","unsnapshotted edit",m.version,false);
    /* Save barrier while active: pending is cancelled, active joined. */
    block_write(); m.version++; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); started();
    m.version++; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK);
    pthread_t release; assert(!pthread_create(&release,NULL,delayed_release,NULL));
    solar_os_writer_checkpoint_fence(c); pthread_join(release,NULL);
    solar_os_writer_checkpoint_status(c,&s); assert(s.paused && !s.busy && !s.pending && s.cancelled==1);
    assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_ERR_INVALID_STATE);
    m.saved=true;
    assert(solar_os_writer_checkpoint_commit(c,path,"doc",NULL,&m)==ESP_OK);
    expect(path,"doc","",m.version,true);
    assert(solar_os_writer_checkpoint_rebind(c,other,"other")==ESP_ERR_INVALID_STATE);
    solar_os_writer_checkpoint_resume(c);
    /* Suspend fence/flush, then close while busy, with no dangling buffer. */
    m.saved=false; m.version++; block_write(); assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); started();
    assert(!pthread_create(&release,NULL,delayed_release,NULL));
    solar_os_writer_checkpoint_fence(c); pthread_join(release,NULL);
    assert(solar_os_writer_checkpoint_commit(c,path,"doc",&b,&m)==ESP_OK);
    solar_os_writer_checkpoint_resume(c);
    block_write(); m.version++; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); started();
    solar_os_writer_buffer_free(&b); /* worker only owns its immutable snapshot */
    assert(!pthread_create(&release,NULL,delayed_release,NULL));
    solar_os_writer_checkpoint_close(c); pthread_join(release,NULL);
    solar_os_writer_checkpoint_close(d); reap();
    /* Reconstruction adopts complete destination only; corrupt metadata fails. */
    expect(path,"doc","unsnapshotted edit",m.version,false);
    FILE *f=fopen(path,"r+b"); assert(f); assert(!fseek(f,12,SEEK_SET)); assert(fputc(99,f)!=EOF); assert(!fclose(f));
    char *text; size_t len; bool legacy; solar_os_writer_checkpoint_meta_t meta;
    assert(solar_os_writer_checkpoint_read(path,"doc",&text,&len,&meta,&legacy)==ESP_ERR_INVALID_STATE);
    f=fopen(path,"wb"); assert(f); fputs("legacy recovery",f); fclose(f);
    assert(solar_os_writer_checkpoint_read(path,"doc",&text,&len,&meta,&legacy)==ESP_OK && legacy);
    heap_caps_free(text);
    /* New zoom values survive the same durable recovery/tombstone format. */
    assert(solar_os_writer_checkpoint_open(path,"doc",&c)==ESP_OK);
    solar_os_writer_checkpoint_fence(c);
    solar_os_writer_buffer_init(&b);
    assert(solar_os_writer_buffer_set(&b,"ZOOM",4)==ESP_OK);
    for (unsigned z=5;z<=SOLAR_OS_DOC_MAX_ZOOM;z++) {
        for (unsigned saved=0;saved<2;saved++) {
            m.zoom=z; m.saved=saved!=0;
            assert(solar_os_writer_checkpoint_commit(c,path,"doc",&b,&m)==ESP_OK);
            assert(solar_os_writer_checkpoint_read(path,"doc",&text,&len,&meta,&legacy)==ESP_OK);
            assert(!legacy && meta.zoom==z && meta.saved==(saved!=0));
            assert(len==(saved?0:4));
            if (!saved) assert(memcmp(text,"ZOOM",4)==0);
            heap_caps_free(text);
        }
    }
    m.zoom=SOLAR_OS_DOC_MAX_ZOOM+1;
    assert(snapshot("doc",&b,&m)==NULL);
    m.zoom=7; m.saved=false;
    uint8_t *record=snapshot("doc",&b,&m); assert(record);
    put32(record+20,8); put32(record+36,checksum(record,record_size(record)));
    f=fopen(path,"wb"); assert(f);
    assert(fwrite(record,1,record_size(record),f)==record_size(record)); assert(!fclose(f));
    heap_caps_free(record);
    assert(solar_os_writer_checkpoint_read(path,"doc",&text,&len,&meta,&legacy)==ESP_ERR_INVALID_STATE);
    solar_os_writer_buffer_free(&b); m.zoom=1;
    solar_os_writer_checkpoint_close(c); reap();
    /* Maximum-size real gap-buffer snapshot and complete readback. */
    assert(solar_os_writer_checkpoint_open(path,"doc",&c)==ESP_OK);
    solar_os_writer_buffer_init(&b); char *large=malloc(SOLAR_OS_WRITER_BUFFER_MAX_BYTES); assert(large);
    memset(large,'x',SOLAR_OS_WRITER_BUFFER_MAX_BYTES);
    assert(solar_os_writer_buffer_set(&b,large,SOLAR_OS_WRITER_BUFFER_MAX_BYTES)==ESP_OK);
    m.version=1; m.saved=false; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK);
    assert(solar_os_writer_checkpoint_wait(c)==ESP_OK);
    assert(solar_os_writer_checkpoint_read(path,"doc",&text,&len,&meta,&legacy)==ESP_OK);
    assert(len==SOLAR_OS_WRITER_BUFFER_MAX_BYTES && !memcmp(text,large,len));
    free(large); heap_caps_free(text);
    block_write(); m.version++; assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_OK); started();
    assert(!pthread_create(&release,NULL,delayed_release,NULL));
    assert(solar_os_writer_checkpoint_drain_all(true)==ESP_OK); pthread_join(release,NULL);
    assert(solar_os_writer_checkpoint_submit(c,&b,&m)==ESP_ERR_INVALID_STATE);
    solar_os_writer_buffer_free(&b); solar_os_writer_checkpoint_close(c); reap();
    char temp[200]; snprintf(temp,sizeof(temp),"%s.writer.tmp",path); assert(access(temp,F_OK));
    snprintf(temp,sizeof(temp),"%s.writer.bak",path); assert(access(temp,F_OK));
    assert(!unlink(path) && !unlink(other) && !rmdir(dir));
    puts("writer checkpoint concurrency/files: PASS");
}
