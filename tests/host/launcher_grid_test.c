#define SOLAR_OS_BOARD_LAUNCHER_TOUCH_GRID 1
#define LAUNCHER_GRID_FIXTURE 1
#define SOLAR_OS_PACKAGE_SERVICE_WIFI 1
#define main legacy_launcher_test_main
#include "launcher_touch_test.c"
#undef main
int main(void)
{
    launcher_state_t state={0}; launcher_state_storage=&state;
    solar_os_context_t ctx={.gfx=&display};
    static const char *labels[]={"Manual","Reader","Wi-Fi","Keyboard","Sketch","Clock"};
    assert(launcher_load_config()==ESP_OK);
    assert(launcher.config.columns==3 && launcher.config.rows==2 && launcher.config.item_count==6);
    for (unsigned size=0;size<2;size++) {
        display.width=size?960:800; display.height=size?540:480;
        int header=launcher_header_height();
        launcher.render_pending=false;
        solar_os_input_pointer_event_t held={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,
            .action=SOLAR_OS_INPUT_POINTER_PRESS,.x=display.width/2,.y=header+40};
        launcher_pointer_event(&ctx,&held);
        assert(!launcher.render_pending && !ctx.requested_app);
        launcher_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TOUCH_CANCEL});
        for (size_t i=0;i<6;i++) {
            assert(!strcmp(launcher.config.items[i].name,labels[i]));
            int x0,y0,x1,y1; launcher_cell_bounds(i,display.width,display.height,&x0,&y0,&x1,&y1);
            assert(x0>=0 && y0>=header && x1<=(int)display.width && y1<=(int)display.height);
            solar_os_launcher_cell_t cells[6]; launcher_copy_cells(cells);
            assert(solar_os_launcher_layout_hit(cells,6,3,2,display.width,display.height-header,
                     (x0+x1)/2,(y0+y1)/2-header)==i);
        }
        solar_os_input_pointer_event_t p={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,
            .action=SOLAR_OS_INPUT_POINTER_PRESS,.x=(int)display.width-40,.y=header/2};
        ctx.sleep_requested=false; launcher_pointer_event(&ctx,&p); assert(!ctx.sleep_requested);
        p.action=SOLAR_OS_INPUT_POINTER_RELEASE; launcher_pointer_event(&ctx,&p); assert(ctx.sleep_requested);
        launcher_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TOUCH_CANCEL});
        ctx.sleep_requested=false; launcher_pointer_event(&ctx,&p); assert(!ctx.sleep_requested);
    }
    for (size_t i=0;i<6;i++) {
        launcher.selected=i;ctx.requested_app=NULL;launcher_activate(&ctx);
        assert(ctx.requested_app);
        assert(i==1 || i==2 || ctx.requested_app==solar_os_app_registry_find(launcher.config.items[i].command)->app);
    }
    assert(!strcmp(launcher.config.items[5].command,"clock"));
    /* No seventh item or config writes, including after reload. */
    assert(launcher_load_config()==ESP_OK && launcher.config.item_count==6 && writes==0);
    puts("launcher_grid_test: six ordered cells, 800x480/960x540, release, CANCEL, Sleep, Keyboard/Shell PASS");
    return 0;
}
