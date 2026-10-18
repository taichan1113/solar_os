#define DISPLAY_ASYNC_TEST_MAIN async_fixture_main
#include "display_async_test.c"
#include "../../src/drivers/epd_ssd1677_writer_d.h"

extern void wd_panel_init(uint8_t *);
extern esp_err_t wd_mode(const char *);
extern void wd_status(epd_ssd1677_writer_status_t *);
extern void wd_fail_restore(void);
extern esp_err_t wd_panel_execute(const uint8_t *,size_t,const solar_os_writer_frame_t *,bool *);
static struct {
    solar_os_display_async_t *a;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned activations, permits, restores, restore_permits, controls;
    bool block_restore, control_done;
    solar_os_writer_frame_t seen[32];
    unsigned executed;
} live={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};
void wd_control_begin(bool discard) { solar_os_display_async_control_begin(live.a,discard); }
void wd_control_end(esp_err_t err) { solar_os_display_async_control_end(live.a,-1,err); }
void wd_gate(unsigned phase)
{
    pthread_mutex_lock(&live.mutex);
    if(!phase) {
        live.activations++; pthread_cond_broadcast(&live.cond);
        while(!live.permits) pthread_cond_wait(&live.cond,&live.mutex);
        live.permits--;
    } else {
        live.restores++; pthread_cond_broadcast(&live.cond);
        while(live.block_restore && !live.restore_permits) pthread_cond_wait(&live.cond,&live.mutex);
        if(live.restore_permits) live.restore_permits--;
    }
    pthread_mutex_unlock(&live.mutex);
}
static void wait_count(unsigned *field,unsigned count)
{ pthread_mutex_lock(&live.mutex); while(*field<count) pthread_cond_wait(&live.cond,&live.mutex); pthread_mutex_unlock(&live.mutex); }
static void release_panel(unsigned n)
{ pthread_mutex_lock(&live.mutex); live.permits+=n; pthread_cond_broadcast(&live.cond); pthread_mutex_unlock(&live.mutex); }
static esp_err_t panel_execute(void *p,const uint8_t *data,size_t size,bool *physical)
{
    (void)p; solar_os_writer_frame_t origin;
    solar_os_display_async_execution_origin(live.a,&origin);
    pthread_mutex_lock(&live.mutex); live.seen[live.executed++]=origin; pthread_mutex_unlock(&live.mutex);
    return wd_panel_execute(data,size,&origin,physical);
}
static void input(uint8_t *image,unsigned n,bool writer_origin)
{
    image[300U/8U*800U+500U+n]^=1U<<(300U&7U);
    const solar_os_writer_frame_t origin={.is_writer=true,.inputs=1,.input_first_us=esp_timer_get_time()};
    const esp_err_t err=writer_origin ? solar_os_display_async_submit_writer(live.a,image,48000,NULL,&origin) :
        solar_os_display_async_submit(live.a,image,48000,NULL);
    assert(err==ESP_OK);
}
static void *change_mode(void *name)
{
    pthread_mutex_lock(&live.mutex); live.controls++; pthread_cond_broadcast(&live.cond); pthread_mutex_unlock(&live.mutex);
    assert(wd_mode(name)==ESP_OK);
    pthread_mutex_lock(&live.mutex); live.control_done=true; pthread_cond_broadcast(&live.cond); pthread_mutex_unlock(&live.mutex);
    return NULL;
}
static void wait_control_barrier(void)
{
    /* Inspect the production barrier under its own mutex, no timing race. */
    for(unsigned i=0;i<2000;i++) {
        lock(live.a); bool held=live.a->controlled; unlock(live.a);
        if(held) return;
        vTaskDelay(1);
    }
    assert(!"control barrier did not engage");
}
int main(int argc,char **argv)
{
    const bool failure=argc>1 && !strcmp(argv[1],"restore-fail");
    uint8_t image[48000]; wd_panel_init(image);
    assert(solar_os_display_async_create(48000,panel_execute,NULL,&live.a)==ESP_OK);
    epd_ssd1677_writer_status_t initial; wd_status(&initial);
    assert(initial.enabled); /* No manual activation after ordinary boot. */
    if(failure) {
        wd_fail_restore(); input(image,0,false); wait_count(&live.activations,1);
        input(image,1,true); release_panel(1);
        assert(solar_os_display_async_wait(live.a,2000)==ESP_FAIL);
        epd_ssd1677_writer_status_t s; wd_status(&s);
        assert(s.attempts==1 && s.d_physical==1 && s.d_errors==1 && s.failed && !s.enabled);
        assert(wd_mode("candidate_d")==ESP_ERR_INVALID_STATE);
        solar_os_display_submission_status_t q; solar_os_display_async_status(live.a,&q);
        assert(q.error==ESP_FAIL && q.writer_cancelled==1 && live.activations==1);
        puts("writer D async: actual Baseline restore failure stops worker/pending, no refund PASS");
    } else {
        /* D in-flight + Writer coalescing + mode A switch. The switch must
         * remain blocked through restoration, not just through 0x20/BUSY. */
        live.block_restore=true;
        input(image,0,true); wait_count(&live.activations,1);
        input(image,1,true); input(image,2,true);
        pthread_t control; assert(!pthread_create(&control,NULL,change_mode,"baseline"));
        wait_control_barrier(); release_panel(1); wait_count(&live.restores,1);
        pthread_mutex_lock(&live.mutex);
        assert(!live.control_done && live.activations==1);
        live.restore_permits++; pthread_cond_broadcast(&live.cond); pthread_mutex_unlock(&live.mutex);
        pthread_join(control,NULL); live.block_restore=false;
        wait_count(&live.activations,2); release_panel(1);
        assert(solar_os_display_async_wait(live.a,2000)==ESP_OK);
        epd_ssd1677_writer_status_t s; wd_status(&s);
        assert(s.attempts==1 && s.d_success==1 && s.writer_a==1);
        assert(live.seen[1].is_writer && live.seen[1].inputs==2 && live.seen[1].coalesced==1);
        puts("writer D async: D->A switch waits through restore; pending coalesced Writer chooses A PASS");
        /* A in-flight, D switch, then a non-Writer replaces pending Writer. */
        input(image,3,true); wait_count(&live.activations,3);
        input(image,4,true); input(image,5,false);
        live.control_done=false;
        assert(!pthread_create(&control,NULL,change_mode,"candidate_d"));
        wait_control_barrier(); release_panel(1); pthread_join(control,NULL);
        wait_count(&live.activations,4); release_panel(1);
        assert(solar_os_display_async_wait(live.a,2000)==ESP_OK);
        wd_status(&s); assert(s.attempts==2 && s.d_physical==2 && !live.seen[3].is_writer);
        puts("writer D async: A->D switch, busy non-Writer replacement correctly uses global D PASS");
        /* Continued physical D updates: no cumulative limit or thinning. */
        for(unsigned i=0;i<20;i++) {
            input(image,6+i,(i%2)==0); wait_count(&live.activations,5+i); release_panel(1);
            assert(solar_os_display_async_wait(live.a,2000)==ESP_OK);
        }
        wd_status(&s); assert(s.attempts==22 && s.d_physical==22 && s.d_success==22 && s.enabled);
        assert(wd_mode("candidate_d")==ESP_OK);
        input(image,26,true); wait_count(&live.activations,25); release_panel(1);
        assert(solar_os_display_async_wait(live.a,2000)==ESP_OK);
        wd_status(&s); assert(s.attempts==23 && s.writer_a==2 && s.enabled);
        /* App-exit/synchronous-present barrier discards pending Writer while
         * joining active work. No stale frame escapes into the next app; D is display-wide. */
        solar_os_display_async_control_begin(live.a,false);
        input(image,27,true); solar_os_display_async_control_end(live.a,-1,ESP_OK);
        wait_count(&live.activations,26); input(image,28,true);
        /* The discard control must run on a separate thread while active. */
        pthread_mutex_lock(&live.mutex); live.control_done=false; pthread_mutex_unlock(&live.mutex);
        extern void *exit_app(void *);
        assert(!pthread_create(&control,NULL,exit_app,NULL)); wait_control_barrier(); release_panel(1);
        pthread_join(control,NULL);
        solar_os_display_submission_status_t q; solar_os_display_async_status(live.a,&q);
        assert(!q.pending && !q.busy && q.writer_cancelled==1 && q.writer_coalesced==1 && q.writer_superseded==1);
        puts("writer D async: all-app continuous through D update 24, no thinning, app-exit cancellation PASS");
    }
    solar_os_display_async_destroy(live.a); reap();
    puts("writer_d_async_test: PASS"); return 0;
}
void *exit_app(void *p)
{ (void)p; solar_os_display_async_control_begin(live.a,true); solar_os_display_async_control_end(live.a,-1,ESP_OK); return NULL; }
