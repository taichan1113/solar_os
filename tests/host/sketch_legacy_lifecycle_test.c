/* Same actual app, without the board's auto-pointer/async opt-ins. */
#define main integrated_fixture_main
#include "sketch_lifecycle_test.c"
#undef main
int main(void)
{
    other_pointer=true;
    solar_os_context_t c=begin(0);
    assert(sketch_start(&c)==ESP_OK && !leased && sketch.modal==SKETCH_MODAL_NONE);
    assert(!submits && synchronous_presents==1);
    sketch_suspend(&c);sketch_resume(&c);
    assert(!leased && synchronous_presents==2 && !submits);
    end(&c);other_pointer=false;
    c=begin(0);assert(sketch_start(&c)==ESP_OK && sketch.modal==SKETCH_MODAL_NO_POINTER);
    end(&c);
    puts("legacy Sketch: no auto-pointer lease, synchronous presentation preserved: PASS");
    return 0;
}
