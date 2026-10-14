#define main ft6336_lifecycle_unused_main
#include "ft6336_lifecycle_test.c"
#undef main

static unsigned cases;
static void passed(const char *name) { printf("touchraw: %s PASS\n", name); cases++; }
static void poll(void) { seq_len=0; poll_device(&touch); }
static void setup(void)
{
    if(touch.active) assert(solar_os_ft6336_detach("touch0")==ESP_OK);
    free(raw_records);raw_records=NULL;
    raw_status=(solar_os_touchraw_status_t){0};raw_stop_request=0;
    fail_i2c=fail_alloc=fail_quiesce=fail_stop=fail_pointer=false;usb_connected=valid_handle=true;
    fake_us=0;seq_len=0;read6=read13=0;memset(report,0,sizeof(report));
    assert(solar_os_ft6336_attach("touch0",bindings,8)==ESP_OK);
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
}
static solar_os_touchraw_record_t get(size_t i)
{
    solar_os_touchraw_status_t s;
    solar_os_touchraw_record_t r;
    assert(solar_os_ft6336_raw_dump_begin(&s)==ESP_OK);
    assert(solar_os_ft6336_raw_record(i,&r)==ESP_OK);
    solar_os_ft6336_raw_dump_end();return r;
}
static void stop(void) { assert(solar_os_ft6336_raw_stop()==ESP_OK);poll(); }
int main(void)
{
    setup();
    assert(sizeof(solar_os_touchraw_record_t)==48);
    assert(solar_os_ft6336_set_enabled("touch0",false)==ESP_OK);
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_INVALID_STATE);
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
    usb_connected=false;assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_INVALID_STATE);
    usb_connected=true;passed("disabled/disconnected start rejected");
    fail_alloc=true;assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_NO_MEM);
    assert(raw_status.mode==TOUCHRAW_IDLE && input_ready);fail_alloc=false;
    poll();assert(read6==1 && read13==0);passed("allocation rollback / normal six-byte reader");
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_INVALID_STATE);
    solar_os_touchraw_status_t s;
    assert(solar_os_ft6336_raw_dump_begin(&s)==ESP_ERR_INVALID_STATE);
    assert(solar_os_ft6336_raw_clear()==ESP_ERR_INVALID_STATE);
    stop();assert(raw_status.mode==TOUCHRAW_IDLE && input_ready && !read13);
    passed("pending stop and active dump/clear/double-start rejection");

    setup();report[0]=1;report[2]=42;report[4]=51;poll();assert(touch.pressed);
    unsigned writes=pointer_writes, releases_before=releases;
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);
    fail_quiesce=true;poll();fail_quiesce=false;
    assert(raw_status.mode==TOUCHRAW_IDLE && input_ready && touch.pressed && !read13);
    passed("quiesce failure preserves legacy PRESS and restores readiness");
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);unsigned six=read6;poll();
    assert(raw_status.mode==TOUCHRAW_RUNNING && !input_ready && !touch.pressed);
    assert(read13==1 && read6==six && pointer_writes==writes && releases==releases_before+2);
    passed("one owner / one thirteen-byte burst / PRESS released / no pointer delivery");
    memcpy(report,(uint8_t[]){2,0x80,50,0x10,60,5,6,0x40,70,0x20,80,11,12},13);
    poll();assert(raw_status.results[FT6336_FRAME_UNCERTAIN]==1);
    report[9]=0x10;poll();assert(raw_status.results[FT6336_FRAME_INVALID]==1);
    stop();solar_os_touchraw_record_t r=get(1);
    assert(r.result==FT6336_FRAME_UNCERTAIN && r.reasons==FT6336_FRAME_REASON_UP_COUNT_UNKNOWN);
    assert(r.start_us<r.end_us && r.sequence==2 && !memcmp(r.raw,(uint8_t[]){2,0x80,50,0x10,60,5,6,0x40,70,0x20,80,11,12},13));
    assert(get(2).reasons & FT6336_FRAME_REASON_DUPLICATE_ID);
    passed("all raw bytes / decoder classifications / timestamps preserved");
    assert(solar_os_ft6336_raw_dump_begin(&s)==ESP_OK && s.stored==3);
    assert(solar_os_ft6336_raw_clear()==ESP_ERR_INVALID_STATE);
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_INVALID_STATE);
    assert(solar_os_ft6336_raw_record(3,&r)==ESP_ERR_INVALID_STATE);
    solar_os_ft6336_raw_dump_end();assert(solar_os_ft6336_raw_record(0,&r)==ESP_ERR_INVALID_STATE);
    passed("frozen dump / index bounds / no silent overwrite");
    poll();assert(raw_status.mode==TOUCHRAW_WAIT_RELEASE && !input_ready && pointer_writes==writes);
    fail_i2c=true;report[0]=0;poll();assert(!input_ready);fail_i2c=false;
    poll();assert(raw_status.mode==TOUCHRAW_IDLE && input_ready && pointer_writes==writes);
    report[0]=1;poll();assert(last_pointer.action==SOLAR_OS_INPUT_POINTER_PRESS && pointer_writes==writes+1);
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_ERR_INVALID_STATE);
    assert(solar_os_ft6336_raw_clear()==ESP_OK && !raw_status.reads);
    passed("held finger/error gate / healthy zero rearm / six-byte restoration / explicit clear");

    setup();assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();
    fake_us=raw_status.started_us+SOLAR_OS_TOUCHRAW_TIMEOUT_US;poll();
    assert(raw_status.end==TOUCHRAW_END_TIMEOUT && raw_status.stored==1 && read13==1);
    passed("thirty-second timeout preserves journal");
    setup();assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();usb_connected=false;poll();
    assert(raw_status.end==TOUCHRAW_END_DISCONNECT && read13==1);poll();assert(input_ready);
    passed("USB disconnect stops diagnosis and restores after all-up");
    setup();assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();
    valid_handle=false;poll();valid_handle=true;
    assert(raw_status.end==TOUCHRAW_END_DISCONNECT && read13==1);
    passed("Shell claim token invalidation stops even when USB remains connected");

    setup();report[0]=1;report[2]=123;assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();
    fail_i2c=true;poll();fail_i2c=false;poll();fail_i2c=true;
    poll();poll();poll();fail_i2c=false;poll();
    assert(raw_status.end==TOUCHRAW_END_IO && raw_status.failures==4 && raw_status.max_consecutive==3);
    r=get(1);assert(r.error==ESP_FAIL && r.result==0 && !r.reasons);
    for(size_t i=0;i<13;i++)assert(r.raw[i]==0);
    assert(get(2).raw[2]==123 && raw_status.stored==6);
    passed("failed reads never reuse data / consecutive errors bounded");

    setup();assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();
    for(unsigned i=0;i<SOLAR_OS_TOUCHRAW_CAPACITY;i++)poll();
    assert(raw_status.stored==SOLAR_OS_TOUCHRAW_CAPACITY && raw_status.reads==SOLAR_OS_TOUCHRAW_CAPACITY+1 && raw_status.dropped==1);
    poll();assert(raw_status.end==TOUCHRAW_END_FULL);
    assert(get(SOLAR_OS_TOUCHRAW_CAPACITY-1).sequence==SOLAR_OS_TOUCHRAW_CAPACITY);
    passed("bounded capacity / overflow recorded / no out-of-range store");

    setup();assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();report[0]=1;
    assert(solar_os_ft6336_set_suspended("touch0",true)==ESP_OK);
    assert(raw_status.end==TOUCHRAW_END_SOURCE && raw_status.stored==1);
    assert(solar_os_ft6336_set_suspended("touch0",false)==ESP_OK && !input_ready);
    writes=pointer_writes;poll();assert(!input_ready && pointer_writes==writes);
    report[0]=0;poll();assert(input_ready && raw_status.mode==TOUCHRAW_IDLE);
    passed("suspend joins worker / journal retained / resume waits all-up");
    assert(solar_os_ft6336_raw_clear()==ESP_OK);
    assert(solar_os_ft6336_raw_start(&diag_owner)==ESP_OK);poll();report[0]=1;
    assert(solar_os_ft6336_detach("touch0")==ESP_OK);
    assert(raw_status.mode==TOUCHRAW_WAIT_RELEASE && raw_status.stored==1);
    assert(solar_os_ft6336_attach("touch0",bindings,8)==ESP_OK);
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK && !input_ready);
    poll();assert(!input_ready);report[0]=0;poll();assert(input_ready);
    assert(solar_os_ft6336_detach("touch0")==ESP_OK);
    free(raw_records);raw_records=NULL;
    passed("detach retains raw data / reattach waits all-up");
    printf("ft6336_raw_test: %u cases PASS\n",cases);
}
