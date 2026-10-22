/* Actual Driver/SPI capture + existing retained/swap RAM models. Host only. */
static const char *ram_luts[] = {"baseline", "candidate_d", "candidate_e1", "candidate_f"};
static const uint8_t *ram_expected_lut(unsigned lut)
{
    return lut == 3 ? ssd1677_trial_black_hold_f_lut : lut == 2 ? ssd1677_trial_black_hold_e1_lut :
           lut == 1 ? ssd1677_trial_black_hold_d_lut : ssd1677_reference_lut;
}
static void ram_check_update(epd_ssd1677_t *d, controller_ram_t *model,
                             unsigned lut, bool full, bool prepare_both)
{
    assert(epd_ssd1677_present_snapshot(d,frame,sizeof(frame))==ESP_OK);
    assert(count_command(0x20)==1 && count_command(0x24)==2);
    assert(count_command(0x26)==(prepare_both?2U:1U));
    assert(count_command(0x32)==(lut?2U:1U));
    expect_data(0x32,ram_expected_lut(lut),105);
    if(lut) assert(!memcmp(commands[command_count-1].data,ssd1677_reference_lut,105) &&
                   commands[command_count-1].command==0x32);
    expect_data(0x03,ssd1677_reference_lut+105,1);
    expect_data(0x04,ssd1677_reference_lut+106,3);
    expect_data(0x2c,ssd1677_reference_lut+109,1);
    expect_data(0x21,(uint8_t[]){0},1);
    const epd_ssd1677_writer_record_t *r=&d->writer_trial.current;
    assert(r->candidate_d==(lut==1) && r->candidate_e1==(lut==2));
    assert(r->candidate_f==(lut==3));
    assert(r->ram_path==(full?SSD1677_RAM_FULL:SSD1677_RAM_ROWS));
    assert(r->ram_width==800 && r->ram_height==(full?480:1));
    const transfer_t tr=controller_apply(model,d);
    const size_t plane=full?48000:100;
    assert(tr.bw_bytes==plane*2 && tr.red_bytes==plane*(prepare_both?2:1));
    assert(r->ram_bytes==tr.bw_bytes+tr.red_bytes);
    assert(d->dirty_hold_ram_synced && d->shadow_valid && !memcmp(frame,shadow,sizeof(frame)));
    assert(!memcmp(model->bw,model->visible,sizeof(model->bw)) &&
           !memcmp(model->red,model->visible,sizeof(model->red)));
    assert(d->fastest_spi_hz==20000000 && d->dirty_hold_trial && d->dirty_hold_dual_sync);
}
static void test_ram_lut_matrix(void)
{
    for(unsigned lut=0;lut<4;lut++) for(unsigned full=0;full<2;full++)
    for(unsigned r2=0;r2<2;r2++) for(unsigned swap=0;swap<2;swap++) {
        boot(); epd_ssd1677_t d=prepare();
        assert(!d.dirty_hold_force_full); /* Standard boot RAM window. */
        assert(epd_ssd1677_writer_lut(&d,ram_luts[lut],NULL,NULL)==ESP_OK);
        const bool a=d.writer_trial.baseline_selected,e1=d.writer_trial.e1_selected,f=d.writer_trial.f_selected;
        assert(epd_ssd1677_ram_mode(&d,full?"full":"rows",NULL)==ESP_OK && !command_count);
        assert(d.writer_trial.baseline_selected==a && d.writer_trial.e1_selected==e1);
        assert(d.writer_trial.f_selected==f);
        bool selected=!full;
        assert(epd_ssd1677_ram_mode(&d,"status",&selected)==ESP_OK && selected==!!full);
        assert(epd_ssd1677_writer_lut(&d,ram_luts[lut],NULL,NULL)==ESP_OK &&
               d.dirty_hold_force_full==!!full && !command_count);
        controller_ram_t *model=malloc(sizeof(*model)); assert(model);
        controller_seed(model,&d); model->swap_after_activation=swap;
        u8g2_SetupDisplay(&d.u8g2,ssd1677_u8x8_display_cb,u8x8_dummy_cb,u8x8_dummy_cb,u8x8_dummy_cb);
        u8g2_SetupBuffer(&d.u8g2,frame,60,u8g2_ll_hvline_vertical_top_lsb,r2?U8G2_R2:U8G2_R0);
        u8g2_SetDrawColor(&d.u8g2,2); u8g2_DrawPixel(&d.u8g2,700,321);
        command_count=0; ram_check_update(&d,model,lut,full,false);
        expect_data(0x22,(uint8_t[]){0x0c},1);
        command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
        free(model);
    }
    puts("RAM x LUT 8 conditions: transmitted 105B A/D/E1/F, 300/144000B warm, no fallback, R0/R2 retained/swap model and no-op PASS");
}
static void test_ram_switch_and_resync(void)
{
    for(unsigned swap=0;swap<2;swap++) {
        boot(); epd_ssd1677_t d=prepare();
        controller_ram_t *model=malloc(sizeof(*model)); assert(model);
        controller_seed(model,&d); model->swap_after_activation=swap;
        assert(epd_ssd1677_writer_lut(&d,"candidate_e1",NULL,NULL)==ESP_OK);
        /* Repeated switches keep known planes; periodic preparation is not
         * disabled by full, nor does switching itself cause SPI/activation. */
        for(unsigned n=0;n<5;n++) {
            const bool full=(n%2)==0;
            command_count=0;
            assert(epd_ssd1677_ram_mode(&d,full?"full":"rows",NULL)==ESP_OK && !command_count);
            assert(d.writer_trial.e1_selected);
            flip(300+n,301); ram_check_update(&d,model,2,full,false);
        }
        for(unsigned full=0;full<2;full++) {
            command_count=0;
            assert(epd_ssd1677_ram_mode(&d,full?"full":"rows",NULL)==ESP_OK && !command_count);
            d.dirty_hold_dual_updates=32;
            assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK &&
                   !command_count && d.dirty_hold_dual_updates==32);
            flip(340+full,301); ram_check_update(&d,model,2,true,true);
            assert(d.writer_trial.current.periodic_resync && d.dirty_hold_dual_updates==1);
            command_count=0; flip(350+full,301);
            ram_check_update(&d,model,2,full,false);
            assert(!d.writer_trial.current.periodic_resync);
        }
        /* Source policy is independent of full; lut 2 keeps Sketch A, while
         * lut 3 and E1 really transmit their selected LUT on Sketch frames. */
        assert(epd_ssd1677_ram_mode(&d,"full",NULL)==ESP_OK);
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=true};
        const char *modes[]={"candidate_d","candidate_d_sketch","candidate_e1","candidate_f"};
        for(unsigned i=0;i<4;i++) {
            assert(epd_ssd1677_writer_lut(&d,modes[i],NULL,NULL)==ESP_OK && d.dirty_hold_force_full);
            command_count=0; flip(360+i,301); ram_check_update(&d,model,i,true,false);
        }
        free(model);
    }
    puts("RAM rows<->full keeps both faces, E1 and 32-update preparation; full preserves Sketch A/D/E1 policies PASS");
}
static void test_ram_cold_and_failure(void)
{
    for(unsigned lut=0;lut<4;lut++) for(unsigned state=0;state<2;state++) {
        boot(); epd_ssd1677_t d=prepare();
        assert(epd_ssd1677_writer_lut(&d,ram_luts[lut],NULL,NULL)==ESP_OK);
        assert(epd_ssd1677_ram_mode(&d,"full",NULL)==ESP_OK);
        controller_ram_t *model=malloc(sizeof(*model)); assert(model); controller_seed(model,&d);
        if(!state) { d.reference_awake=false; d.controller_ready=false; }
        else d.dirty_hold_ram_synced=false;
        flip(400+state,305); command_count=0;
        ram_check_update(&d,model,lut,true,true);
        expect_data(0x22,(uint8_t[]){state?0x0c:0xcc},1);
        free(model);
        if(!state) {
            assert(ssd1677_sleep(&d)==ESP_OK);
            assert(d.dirty_hold_force_full);
            command_count=0; flip(410,305);
            assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && d.dirty_hold_force_full);
            /* Wake keeps volatile selection but uses the existing OTP
             * recovery/baseline dispatcher before custom fastest resumes. */
            command_count=0; flip(411,305);
            assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
            expect_data(0x32,ram_expected_lut(lut),105);
        }
    }
    for(unsigned kind=0;kind<4;kind++) {
        boot(); epd_ssd1677_t d=prepare();
        assert(epd_ssd1677_writer_lut(&d,"candidate_e1",NULL,NULL)==ESP_OK);
        assert(epd_ssd1677_ram_mode(&d,"full",NULL)==ESP_OK);
        if(kind==0) fail_command=0x32;
        if(kind==1) busy_on_activation=true;
        if(kind==2) {fail_plane_command=0x26;fail_plane_after_bytes=0;}
        if(kind==3) {fail_plane_command=0x24;fail_plane_after_bytes=0;}
        command_count=0; flip(450+kind,307);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
        const size_t sent=command_count;
        assert(d.faulted && !d.shadow_valid && !d.reference_awake);
        assert(epd_ssd1677_ram_mode(&d,"rows",NULL)==ESP_ERR_INVALID_STATE && d.dirty_hold_force_full);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_ERR_INVALID_STATE && command_count==sent);
        bool full=false; assert(epd_ssd1677_ram_mode(&d,"status",&full)==ESP_OK && full);
    }
    boot(); epd_ssd1677_t d=prepare();
    assert(epd_ssd1677_ram_mode(&d,"full",NULL)==ESP_OK);
    const char *otp_modes[]={"refresh=full","refresh=partial"};
    for(unsigned i=0;i<2;i++) {
        assert(epd_ssd1677_set_controller_mode(&d,otp_modes[i])==ESP_OK);
        command_count=0;flip(470+i,308);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(!count_command(0x32) && count_command(0x20)>0 && d.dirty_hold_force_full);
        assert(!d.writer_trial.used_d && !d.writer_trial.used_e1);
    }
    boot(); d=prepare();
    assert(epd_ssd1677_ram_mode(&d,"unknown",NULL)==ESP_ERR_INVALID_ARG);
    d.dirty_hold_trial=false;
    assert(epd_ssd1677_ram_mode(&d,"full",NULL)==ESP_ERR_INVALID_STATE && !d.dirty_hold_force_full);
    puts("RAM full cold/unsynced 192000B, single activation, OTP full/partial, sleep/wake, errors latch without replay, unsupported backend rejected PASS");
}
