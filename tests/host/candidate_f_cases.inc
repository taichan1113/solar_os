/* Reuse the actual Driver/SPI fixture; no new panel model or optical claims. */
static void test_candidate_f_lut(void)
{
    const uint8_t *f=ssd1677_trial_black_hold_f_lut;
    assert(f[0]==0 && f[1]==0xc0);
    assert(!memcmp(f+2,ssd1677_reference_lut+2,108));
    const unsigned vs_expected[]={0,0,0,0,3,0}, time_expected[]={2,2,2,2,1,1};
    const int volts[]={0,16,-15,5};
    unsigned n=0,frames=0,weak=0; int sum=0;
    for(unsigned g=0;g<10;g++) {
        assert(!f[54+g*5]);
        for(unsigned p=0;p<4;p++) {
            const unsigned time=f[50+g*5+p],vs=(f[g]>>(6-2*p))&3U;
            if(!time) continue;
            assert(n<6 && time==time_expected[n] && vs==vs_expected[n]);
            frames+=time; sum+=volts[vs]*(int)time; if(vs==3) weak+=time; ++n;
        }
    }
    assert(n==6 && frames==10 && weak==1 && sum==5);
    puts("F LUT: 00 C0; VSS 8F, +5V 1F, VSS 1F; 10F and Source sum +5 V.F; all other 108 bytes unchanged PASS");
}

static void test_candidate_f_modes(void)
{
    boot(); epd_ssd1677_t d=prepare();
    epd_ssd1677_writer_status_t initial=d.writer_trial.status;
    const char *modes[]={"candidate_f","baseline","candidate_f","candidate_d",
                         "candidate_f","candidate_e1","candidate_f","candidate_d_sketch","disable"};
    const unsigned expected[]={3,0,3,1,3,2,3,1,0};
    unsigned f_count=0;
    for(unsigned i=0;i<sizeof(expected)/sizeof(*expected);i++) for(unsigned source=0;source<3;source++) {
        command_count=0;
        epd_ssd1677_writer_status_t s;
        assert(epd_ssd1677_writer_lut(&d,modes[i],&s,NULL)==ESP_OK && !command_count);
        const unsigned chosen=expected[i]==1 && source==2 && i==3?0:expected[i];
        assert(s.candidate_f==(expected[i]==3));
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=source==1,.is_sketch=source==2};
        assert(epd_ssd1677_writer_lut(&d,"trial-start",NULL,NULL)==ESP_OK);
        const epd_ssd1677_writer_status_t before=d.writer_trial.status;
        flip(100+i*3+source,350);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        expect_data(0x32,ram_expected_lut(chosen),105);
        assert(count_command(0x20)==1 && count_command(0x32)==(chosen?2U:1U));
        assert(d.writer_trial.current.candidate_f==(chosen==3));
        assert(d.writer_trial.current.candidate_d==(chosen==1) && d.writer_trial.current.candidate_e1==(chosen==2));
        assert(!memcmp(shadow,frame,sizeof(frame)) && d.dirty_hold_ram_synced);
        if(chosen) assert(!memcmp(find_command(0x32,1)->data,ssd1677_reference_lut,105));
        if(chosen==3) {
            ++f_count;
            assert(d.writer_trial.status.attempts==before.attempts && d.writer_trial.status.e1_attempts==before.e1_attempts);
            assert(d.writer_trial.status.a_physical==before.a_physical && d.writer_trial.status.otp_physical==before.otp_physical);
            assert(d.writer_trial.status.writer_a==before.writer_a && d.writer_trial.status.writer_otp==before.writer_otp);
        }
        assert(epd_ssd1677_writer_lut(&d,"trial-stop",&s,NULL)==ESP_OK);
        assert(s.trial.physical==1 && s.trial.f==(chosen==3) && s.trial.d==(chosen==1) && s.trial.e1==(chosen==2));
        assert(s.trial.a==(chosen==0) && !s.trial.otp && !s.trial.errors);
    }
    assert(d.writer_trial.status.f_attempts==f_count && d.writer_trial.status.f_success==f_count &&
           d.writer_trial.status.f_physical==f_count && !d.writer_trial.status.f_errors && !initial.f_physical);
    assert(epd_ssd1677_writer_lut(&d,"candidate_f",NULL,NULL)==ESP_OK);
    command_count=0; const uint64_t attempts=d.writer_trial.status.f_attempts;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    assert(d.writer_trial.status.f_attempts==attempts && !d.writer_trial.current.candidate_f);
    /* Existing Sleep/Wake keeps explicit volatile selection, then establishes
     * its OTP baseline. Recreating a Driver, unlike sleep, restores defaults. */
    assert(ssd1677_sleep(&d)==ESP_OK && d.writer_trial.f_selected);
    flip(200,350); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !count_command(0x32));
    assert(!d.writer_trial.used_f);
    flip(201,350); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_trial_black_hold_f_lut,105);
    const char *otp[]={"refresh=full","refresh=partial"};
    for(unsigned i=0;i<2;i++) {
        assert(epd_ssd1677_set_controller_mode(&d,otp[i])==ESP_OK);
        command_count=0; flip(210+i,350);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !count_command(0x32));
        assert(!d.writer_trial.used_f && !d.writer_trial.current.candidate_f);
    }
    boot(); d=prepare();
    assert(!d.writer_trial.f_selected && !d.writer_trial.e1_selected && !d.writer_trial.baseline_selected && !d.dirty_hold_force_full);
    d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=true}; flip(220,350); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_reference_lut,105);
    assert(ssd1677_sleep(&d)==ESP_OK);
    flip(221,350); assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    flip(222,350); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_reference_lut,105);
    assert(!d.writer_trial.f_selected && !d.writer_trial.baseline_selected);
    puts("F modes: all sources, independent counters/summary, F<->A/D/E1/Sketch D, no-op, OTP, Sleep/Wake selection and boot D/Sketch A/rows defaults PASS");
}

static void test_candidate_f_faults(void)
{
    for(unsigned kind=0;kind<4;kind++) {
        boot(); epd_ssd1677_t d=prepare();
        assert(epd_ssd1677_writer_lut(&d,"candidate_f",NULL,NULL)==ESP_OK);
        uint8_t old[48000]; memcpy(old,shadow,sizeof(old));
        if(kind==0) fail_command=0x32;
        if(kind==1) busy_on_activation=true;
        if(kind>=2) { fail_plane_command=kind==2?0x26:0x24; fail_plane_after_bytes=0; }
        flip(250+kind,350); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
        assert(d.writer_trial.status.f_attempts==1 && d.writer_trial.status.f_errors==1 && !d.writer_trial.status.f_success);
        assert(!d.writer_trial.status.attempts && !d.writer_trial.status.e1_attempts);
        assert(d.faulted && !d.shadow_valid && !memcmp(old,shadow,sizeof(old)) && atomic_load(&writer_d_failed));
        const size_t sent=command_count;
        assert(epd_ssd1677_writer_lut(&d,"candidate_f",NULL,NULL)==ESP_ERR_INVALID_STATE);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_ERR_INVALID_STATE && command_count==sent);
    }
    puts("F LUT/BUSY/RAM write/post-sync failure paths: no Shadow promotion, existing latch, no replay PASS");
}
