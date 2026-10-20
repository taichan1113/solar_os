/* Actual SPI/Driver tests using the existing fixture; no physical device. */
static solar_os_display_submission_status_t trial_queue;
static esp_err_t test_submission_status(void *p, solar_os_display_submission_status_t *s)
{ (void)p; *s=trial_queue; return ESP_OK; }

static void test_sketch_d_modes(void)
{
    const char *modes[]={"baseline","candidate_d","candidate_e1","candidate_d_sketch"};
    for(unsigned from=0;from<4;from++) for(unsigned to=0;to<4;to++) {
        boot(); epd_ssd1677_t d=prepare();
        assert(!d.writer_trial.sketch_d_selected);
        assert(epd_ssd1677_writer_lut(&d,modes[from],NULL,NULL)==ESP_OK);
        const char *sequence[]={"candidate_d_sketch",modes[to]};
        for(unsigned m=0;m<2;m++) for(unsigned source=0;source<3;source++) {
            const char *mode=sequence[m];
            assert(epd_ssd1677_writer_lut(&d,mode,NULL,NULL)==ESP_OK);
            d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=source==2,.is_writer=source==1};
            const bool e1=!strcmp(mode,"candidate_e1"), ds=!strcmp(mode,"candidate_d_sketch");
            const bool use_d=ds || (!strcmp(mode,"candidate_d") && source!=2);
            flip(400+source+3*m,300); command_count=0;
            assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
            expect_data(0x32,e1?ssd1677_trial_black_hold_e1_lut:use_d?ssd1677_trial_black_hold_d_lut:ssd1677_reference_lut,105);
            assert(d.writer_trial.current.origin.is_sketch==(source==2));
            assert(d.writer_trial.current.candidate_d==use_d && d.writer_trial.current.candidate_e1==e1);
            assert(count_command(0x20)==1 && count_command(0x32)==(e1||use_d?2U:1U));
            assert(!memcmp(shadow,frame,sizeof(frame)) && d.dirty_hold_ram_synced);
        }
    }
    puts("Sketch D actual SPI: all A/D/E1/D-Sketch sources/transitions, D->D restores Sketch A PASS");
}

static void test_trial_summary(void)
{
    boot(); epd_ssd1677_t d=prepare();
    d.submission_status=test_submission_status;
    trial_queue=(solar_os_display_submission_status_t){.submitted=10,.replaced=2,.pending=true};
    const uint64_t lifetime=d.writer_trial.status.records;
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_OK);
    const int64_t start=d.writer_trial.status.trial.start_us;
    assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_ERR_INVALID_STATE);
    assert(d.writer_trial.status.trial.start_us==start && !d.faulted);
    const char *modes[]={"baseline","candidate_d","candidate_e1","candidate_d_sketch"};
    d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=true};
    d.dirty_hold_dual_updates=32; /* Include the EXISTING periodic full fallback. */
    for(unsigned i=0;i<40;i++) {
        assert(epd_ssd1677_writer_lut(&d,modes[i%4],NULL,NULL)==ESP_OK);
        flip(300+i,300); command_count=0; trial_queue.submitted++;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        const epd_ssd1677_writer_record_t *r=&d.writer_trial.current;
        assert(r->ram_x==0 && r->ram_width==800 && r->timing_valid);
        if(i==0 || i==32) {
            assert(r->ram_path==SSD1677_RAM_FULL && r->periodic_resync && r->ram_height==480 && r->ram_bytes==192000);
        } else assert(r->ram_path==SSD1677_RAM_ROWS && !r->periodic_resync && r->ram_height==1 && r->ram_bytes==300);
        assert(r->activation_first_seq==r->activation_last_seq && count_command(0x20)==1);
    }
    trial_queue.replaced+=7;
    trial_queue.submitted++;
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    assert(!d.writer_trial.current.timing_valid && !d.writer_trial.current.ram_bytes);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=full")==ESP_OK);
    d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=true};
    flip(450,310); command_count=0; trial_queue.submitted++;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && count_command(0x20)==2);
    trial_queue.pending=false;
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_OK);
    epd_ssd1677_trial_summary_t s=d.writer_trial.status.trial;
    assert(!s.active && s.started && s.frames==42 && s.physical==42 && s.a==20 && s.d==10 && s.e1==10 && s.otp==2);
    assert(s.sketch==40 && s.writer==2 && !s.other && !s.errors && s.noop==1);
    assert(s.full_ram==2 && s.rows_ram==38 && s.periodic_resync==2 && s.otp_ram==1);
    assert(s.ram_bytes==192000U*2+300U*38+48000U*3);
    assert(s.busy.count==42 && s.start_gap.count==41 && s.ready_gap.count==41 && !s.timing_missing);
    assert(s.submitted==42 && s.replaced==7 && s.queue_valid && s.pending_start && !s.pending_end && !s.overflow);
    assert(d.writer_trial.status.trace_count==32 && d.writer_trial.status.records==lifetime+42);
    time_us+=9000000;
    assert(epd_ssd1677_writer_lut(&d,"trial-status",NULL,NULL)==ESP_OK);
    assert(!memcmp(&s,&d.writer_trial.status.trial,sizeof(s))); /* Frozen after stop. */
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_OK);
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_OK);
    assert(!d.writer_trial.status.trial.physical && d.writer_trial.sketch_d_selected && !d.faulted);
    puts("Trial >32 records: actual 40 fastest + OTP(2), rows/full/resync bytes, no-op, frozen/reset summary PASS");
}

static void test_trial_times_and_overflow(void)
{
    boot(); epd_ssd1677_t d=prepare();
    d.submission_status=test_submission_status;
    trial_queue=(solar_os_display_submission_status_t){.submitted=UINT32_MAX-1};
    assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_OK);
    d.writer_trial.last_activation_valid=d.writer_trial.last_ready_valid=false;
    const int64_t starts[]={10000000,10000100,10000350}, ends[]={10000040,10000150,10000410};
    for(unsigned i=0;i<3;i++) {
        d.timing=(epd_ssd1677_timing_t){.activations=1,.activate_us={starts[i]},.ready_us={ends[i]}};
        epd_ssd1677_writer_record_t r={.seq=i+1,.physical=1,.reference=true,.timing_valid=true,.error=ESP_OK};
        writer_trial_finish(&d,&r);
        assert(r.busy_us==40+10*i);
        if(!i) assert(r.start_gap_us==-1 && r.ready_gap_us==-1);
        else assert(r.start_gap_us==starts[i]-starts[i-1] && r.ready_gap_us==starts[i]-ends[i-1]);
    }
    epd_ssd1677_trial_summary_t *s=&d.writer_trial.status.trial;
    assert(s->busy.count==3 && s->busy.sum_us==150 && s->busy.min_us==40 && s->busy.max_us==60);
    assert(s->start_gap.count==2 && s->start_gap.sum_us==350 && s->start_gap.min_us==100 && s->start_gap.max_us==250);
    assert(s->ready_gap.count==2 && s->ready_gap.sum_us==260 && s->ready_gap.min_us==60 && s->ready_gap.max_us==200);
    s->physical=s->busy.sum_us=UINT64_MAX;
    epd_ssd1677_writer_record_t r={.seq=4,.physical=1,.timing_valid=true,.reference=true};
    writer_trial_finish(&d,&r);
    assert(s->physical==UINT64_MAX && s->busy.sum_us==UINT64_MAX && s->overflow && !d.faulted);
    trial_queue.submitted=1;
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_OK);
    assert(s->queue_wrapped && s->submitted==3 && s->queue_valid);
    printf("Trial time definitions, first-gap exclusion, saturation/wrap; RAM sizes record=%zu summary=%zu state=%zu PASS\n",
           sizeof(epd_ssd1677_writer_record_t),sizeof(*s),sizeof(d.writer_trial));
}

static void test_trial_failure(void)
{
    boot(); epd_ssd1677_t d=prepare();
    assert(epd_ssd1677_writer_lut(&d,"candidate_d_sketch",NULL,NULL)==ESP_OK);
    assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_OK);
    d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=true};
    flip(550,300); command_count=0; fail_command=0x32;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
    assert(d.faulted && !d.shadow_valid);
    assert(epd_ssd1677_writer_lut(&d,"candidate_d_sketch",NULL,NULL)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_writer_lut(&d,"trial-stop",NULL,NULL)==ESP_OK);
    assert(d.writer_trial.status.trial.errors==1 && !d.writer_trial.status.trial.physical &&
           !d.writer_trial.status.trial.queue_valid);
    fail_command=-1;
    puts("Sketch D failed LUT: normal failure latch/Shadow stop, summary error without fake activation PASS");
}
