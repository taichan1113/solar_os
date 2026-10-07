#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "../../src/services/solar_os_display_async.c"

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
    (void)clear;
    TaskHandle_t t = current_task;
    pthread_mutex_lock(&t->mutex);
    struct timespec until;
    clock_gettime(CLOCK_REALTIME,&until);
    until.tv_sec += timeout/1000;
    until.tv_nsec += (long)(timeout%1000)*1000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec-=1000000000; }
    while (!t->notifications) {
        if (timeout == portMAX_DELAY) pthread_cond_wait(&t->cond, &t->mutex);
        else if (pthread_cond_timedwait(&t->cond,&t->mutex,&until) == ETIMEDOUT) break;
    }
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

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned starts, permits, active, full, fast;
    bool baseline, cleanup, fail;
    uint8_t shown[16];
} panel_t;
static esp_err_t execute(void *ctx, const uint8_t *data, size_t size, bool *physical)
{
    panel_t *p=ctx;
    const uint8_t value=data[0];
    for (size_t i=0;i<size;i++) assert(data[i]==value);
    pthread_mutex_lock(&p->mutex);
    assert(++p->active==1);
    p->starts++;
    pthread_cond_broadcast(&p->cond);
    while (!p->permits) pthread_cond_wait(&p->cond,&p->mutex);
    p->permits--;
    /* The original source was overwritten while this callback was blocked. */
    for (size_t i=0;i<size;i++) assert(data[i]==value);
    const bool fail=p->fail;
    if (!fail) {
        if (!p->baseline || p->cleanup) p->full++; else p->fast++;
        p->baseline=true; p->cleanup=false;
        p->shown[p->starts-1]=value;
        *physical=true;
    } else p->baseline=false;
    p->active--;
    pthread_mutex_unlock(&p->mutex);
    return fail ? ESP_FAIL : ESP_OK;
}
static void wait_started(panel_t *p,unsigned n)
{
    pthread_mutex_lock(&p->mutex);
    while(p->starts<n) pthread_cond_wait(&p->cond,&p->mutex);
    pthread_mutex_unlock(&p->mutex);
}
static void permit(panel_t *p,unsigned n)
{
    pthread_mutex_lock(&p->mutex); p->permits+=n;
    pthread_cond_broadcast(&p->cond); pthread_mutex_unlock(&p->mutex);
}
static void init_panel(panel_t *p)
{
    memset(p,0,sizeof(*p)); pthread_mutex_init(&p->mutex,NULL); pthread_cond_init(&p->cond,NULL);
}
static void free_panel(panel_t *p) { pthread_mutex_destroy(&p->mutex); pthread_cond_destroy(&p->cond); }
static void submit(solar_os_display_async_t *a,uint8_t value)
{
    uint8_t source[48000]; memset(source,value,sizeof(source));
    assert(solar_os_display_async_submit(a,source,sizeof(source),NULL)==ESP_OK);
    memset(source,0xcc,sizeof(source));
}

static void test_latest_and_lifetime(void)
{
    panel_t p; init_panel(&p);
    solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
    submit(a,1); wait_started(&p,1);
    assert(solar_os_display_async_wait(a,1)==ESP_ERR_TIMEOUT);
    const int allocations=allocation_count;
    for(unsigned i=0;i<1000;i++) submit(a,(uint8_t)i);
    assert(allocation_count==allocations); /* no allocation per submit */
    solar_os_display_submission_status_t s;
    solar_os_display_async_status(a,&s);
    assert(s.submitted==1001 && s.replaced==999 && s.peak_outstanding==2 && s.snapshot_bytes==96000);
    permit(&p,2);
    assert(solar_os_display_async_wait(a,2000)==ESP_OK);
    assert(p.starts==2 && p.shown[0]==1 && p.shown[1]==(uint8_t)999);
    assert(p.full==1 && p.fast==1);
    solar_os_display_async_destroy(a); reap(); free_panel(&p);
}

typedef struct { solar_os_display_async_t *a; panel_t *p; int kind; atomic_bool done; } control_t;
static void *control_thread(void *arg)
{
    control_t *c=arg;
    if(c->kind==3) solar_os_display_async_destroy(c->a);
    else {
        solar_os_display_async_control_begin(c->a,c->kind==1);
        assert(c->p->active==0);
        if(c->kind==1) c->p->baseline=false; /* rail off */
        else c->p->cleanup=true; /* cleanup / full-mode mutation */
        solar_os_display_async_control_end(c->a,c->kind==1?1:-1,ESP_OK);
    }
    atomic_store(&c->done,true);
    return NULL;
}
static void test_control(int kind)
{
    panel_t p; init_panel(&p);
    solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
    submit(a,1); wait_started(&p,1); submit(a,2);
    control_t c={.a=a,.p=&p,.kind=kind}; atomic_init(&c.done,false);
    pthread_t thread; assert(!pthread_create(&thread,NULL,control_thread,&c));
    /* Synchronization probe only: ensure the real control has reached its
     * barrier before releasing our deliberately blocked physical callback. */
    for (;;) { lock(a); bool entered=a->controlled; unlock(a); if(entered) break; vTaskDelay(1); }
    assert(!atomic_load(&c.done));
    if(kind==0 || kind==2) submit(a,3); /* allowed latest replacement during mode barrier */
    permit(&p,1); pthread_join(thread,NULL); assert(atomic_load(&c.done));
    if(kind==3) { reap(); free_panel(&p); return; }
    if(kind==1) {
        uint8_t src[48000]={0};
        assert(solar_os_display_async_submit(a,src,sizeof(src),NULL)==ESP_ERR_INVALID_STATE);
        assert(solar_os_display_async_wait(a,1)==ESP_ERR_INVALID_STATE);
        solar_os_display_async_control_begin(a,false);
        solar_os_display_async_control_end(a,0,ESP_OK);
        submit(a,4);
    }
    permit(&p,1); assert(solar_os_display_async_wait(a,2000)==ESP_OK);
    assert(p.starts==2 && p.full==2 && p.shown[1]==(kind==1?4:3));
    solar_os_display_async_destroy(a); reap(); free_panel(&p);
}

static void test_fault(void)
{
    panel_t p; init_panel(&p); p.fail=true;
    solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
    submit(a,1); wait_started(&p,1); submit(a,2); permit(&p,1);
    assert(solar_os_display_async_wait(a,2000)==ESP_FAIL);
    solar_os_display_submission_status_t s; solar_os_display_async_status(a,&s);
    assert(!s.pending && !s.busy && s.cancelled==1 && s.completed==0 && !p.baseline);
    uint8_t src[48000]={0};
    assert(solar_os_display_async_submit(a,src,sizeof(src),NULL)==ESP_FAIL);
    solar_os_display_async_destroy(a); reap(); free_panel(&p);
}

static esp_err_t maintain(void *ctx, const uint8_t *data, size_t size, bool *physical)
{
    panel_t *p=ctx;
    pthread_mutex_lock(&p->mutex);p->cleanup=true;pthread_mutex_unlock(&p->mutex);
    return execute(ctx,data,size,physical);
}
static void policy(solar_os_display_async_t *a, unsigned frame, unsigned idle, unsigned budget)
{
    solar_os_display_async_control_begin(a,false);
    solar_os_display_async_policy(a,frame,idle,budget,maintain);
    solar_os_display_async_control_end(a,-1,ESP_OK);
}
static void test_pacing_and_idle_snapshot(void)
{
    panel_t p;init_panel(&p);solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
    policy(a,120,100,2);
    submit(a,1);wait_started(&p,1);permit(&p,1);
    assert(solar_os_display_async_wait(a,2000)==ESP_OK);
    /* The next frame is still pending during the pace window: all replaced
     * sources may be reused, and the worker must claim only the newest. */
    submit(a,2);submit(a,3);submit(a,4);
    solar_os_display_submission_status_t s;solar_os_display_async_status(a,&s);
    assert(s.pending && !s.busy && s.replaced==2);
    wait_started(&p,2);permit(&p,1);
    assert(solar_os_display_async_wait(a,2000)==ESP_OK && p.shown[1]==4);
    /* Maintenance owns completed frame 4. A concurrent input must use the
     * other buffer without changing the cleanup image or submission fence. */
    wait_started(&p,3);submit(a,5);permit(&p,1);
    wait_started(&p,4);permit(&p,1);
    assert(solar_os_display_async_wait(a,2000)==ESP_OK);
    solar_os_display_async_status(a,&s);
    assert(s.started==3 && s.completed==3 && s.maintenance_started==1 &&
           s.maintenance_completed==1 && s.completed_id==5);
    assert(p.shown[2]==4 && p.shown[3]==5 && p.full==2);
    solar_os_display_async_destroy(a);reap();free_panel(&p);
}
static void test_idle_deferral_suspend_and_fault(void)
{
    for (unsigned fail=0;fail<2;fail++) {
        panel_t p;init_panel(&p);solar_os_display_async_t *a;
        assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
        policy(a,0,150,1);
        submit(a,1);wait_started(&p,1);permit(&p,1);
        assert(solar_os_display_async_wait(a,2000)==ESP_OK);
        vTaskDelay(70);
        submit(a,2);wait_started(&p,2);permit(&p,1);
        assert(solar_os_display_async_wait(a,2000)==ESP_OK);
        vTaskDelay(90); /* Old deadline passed, last-submit deadline not yet. */
        solar_os_display_submission_status_t s;solar_os_display_async_status(a,&s);
        assert(s.maintenance_started==0);
        if (!fail) {
            solar_os_display_async_control_begin(a,true);
            solar_os_display_async_control_end(a,1,ESP_OK);
            vTaskDelay(180);solar_os_display_async_status(a,&s);
            assert(s.maintenance_started==0 && s.suspended);
        } else {
            wait_started(&p,3);
            pthread_mutex_lock(&p.mutex);p.fail=true;pthread_mutex_unlock(&p.mutex);
            submit(a,3);permit(&p,1);
            assert(solar_os_display_async_wait(a,2000)==ESP_FAIL);
            solar_os_display_async_status(a,&s);
            assert(s.completed_id==2 && s.maintenance_completed==0 &&
                   !s.pending && s.cancelled==1 && s.error==ESP_FAIL);
        }
        solar_os_display_async_destroy(a);reap();free_panel(&p);
    }
}

static void test_discard_during_pace_window(void)
{
    panel_t p;init_panel(&p);solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,&p,&a)==ESP_OK);
    policy(a,1000,100,1);
    submit(a,1);wait_started(&p,1);permit(&p,1);
    assert(solar_os_display_async_wait(a,2000)==ESP_OK);
    submit(a,2);
    solar_os_display_async_control_begin(a,true);
    solar_os_display_async_control_end(a,1,ESP_OK);
    solar_os_display_submission_status_t s;solar_os_display_async_status(a,&s);
    assert(s.suspended && !s.pending && s.cancelled==1 && s.completed_id==1);
    vTaskDelay(180);solar_os_display_async_status(a,&s);
    assert(s.started==1 && s.maintenance_started==0);
    solar_os_display_async_destroy(a);reap();free_panel(&p);
}

int main(void)
{
    for(int n=0;n<3;n++) {
        allocation_count=0; fail_allocation=n;
        solar_os_display_async_t *a=(void *)1;
        assert(solar_os_display_async_create(48000,execute,NULL,&a)==ESP_ERR_NO_MEM && a==NULL);
        assert(!live_allocations && !semaphores);
    }
    fail_allocation=-1; fail_task=true;
    solar_os_display_async_t *a;
    assert(solar_os_display_async_create(48000,execute,NULL,&a)==ESP_ERR_NO_MEM && a==NULL);
    assert(!live_allocations && !semaphores); fail_task=false;
    test_latest_and_lifetime();
    for(int kind=0;kind<4;kind++) test_control(kind);
    test_fault();
    test_pacing_and_idle_snapshot();
    test_idle_deferral_suspend_and_fault();
    test_discard_during_pace_window();
    puts("display async: snapshots, replacements, pacing, idle cleanup, controls, suspend, shutdown, fault and allocation tests passed");
    return 0;
}
