/* Production Sketch callbacks, with a busy Expansion enable reservation. */
#define main old_sketch_main
#include "sketch_lifecycle_test.c"
#undef main

static void cancel(solar_os_context_t *ctx)
{
    solar_os_event_t e={.type=SOLAR_OS_EVENT_TOUCH_CANCEL};
    /* Same sequence as the real Session CANCEL adapter (tested separately). */
    assert(sketch_event(ctx,&e));
    sketch_suspend(ctx);
    sketch_resume(ctx);
}

int main(void)
{
    strcpy(directory,"/tmp/solar-sketch-cancel-XXXXXX");assert(mkdtemp(directory));
    char saved[SOLAR_OS_STORAGE_PATH_MAX]={0};
    for(unsigned entry=0;entry<3;entry++) {
        solar_os_context_t ctx=begin(0);
        if(entry){ctx.argc=2;strcpy(ctx.argv[1],saved);}
        assert(sketch_start(&ctx)==ESP_OK && leased);
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,120,130);
        assert(sketch.drawing);
        unsigned a=acquire_calls,r=release_calls,frames=submits;
        /* Previously CANCEL powered off then reacquired inside UPDATING. */
        fail_enable=true;enable_failure=ESP_ERR_INVALID_STATE;
        cancel(&ctx);
        assert(leased && !sketch.drawing && !sketch.suspended);
        assert(acquire_calls==a && release_calls==r && submits==frames);
        fail_enable=false;
        solar_os_event_t save={.type=SOLAR_OS_EVENT_CHAR,.data.ch='s'};
        assert(sketch_event(&ctx,&save) && sketch.modal==SKETCH_MODAL_INFO);
        if(!entry)strcpy(saved,sketch.path);
        cancel(&ctx);assert(sketch.modal==SKETCH_MODAL_INFO && leased);
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,400,200);
        assert(sketch.modal==SKETCH_MODAL_NONE && leased);
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,400,200);
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,160,150);
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,180,150);assert(!sketch.drawing);
        sketch_set_info("saved");cancel(&ctx);
        solar_os_event_t esc={.type=SOLAR_OS_EVENT_CHAR,.data.ch=SOLAR_OS_KEY_ESCAPE};
        assert(sketch_event(&ctx,&esc) && sketch.modal==SKETCH_MODAL_NONE && leased);
        /* Browser/child switch and light-sleep boundaries still release. */
        sketch.modal=SKETCH_MODAL_OPEN;sketch_suspend(&ctx);assert(!leased);
        sketch_resume(&ctx);assert(leased && sketch.modal==SKETCH_MODAL_OPEN);
        sketch.modal=SKETCH_MODAL_NONE;
        sketch_suspend(&ctx);assert(!leased);
        fail_enable=true;enable_failure=ESP_ERR_INVALID_STATE;
        sketch_resume(&ctx);assert(!leased);
        fail_enable=false;tick(&ctx);assert(leased);
        sketch_suspend(&ctx);fail_enable=true;sketch_resume(&ctx);
        a=acquire_calls;for(unsigned i=0;i<10;i++)tick(&ctx);
        assert(acquire_calls<=a+2); /* finite deferred retries; no delay/task */
        fail_enable=false;sketch_suspend(&ctx);sketch_resume(&ctx);assert(leased);
        end(&ctx);assert(!leased);
    }
    assert(unlink(saved)==0 && rmdir(directory)==0);
    puts("sketch_cancel_lifecycle_test: Save/OK/Esc/CANCEL/reentry/sleep/lease retry PASS");
}
