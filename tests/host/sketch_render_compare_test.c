/* Compare the existing full fallback and local paint path, not new algorithms.
 * Peripherals are stubbed: no SPI/BUSY/Async latency is measured here. */
#include <time.h>
#define main sketch_lifecycle_fixture_main
#include "sketch_lifecycle_test.c"
#undef main

enum { MAX_FRAMES = 32 };
static uint8_t *frames[MAX_FRAMES];
static size_t frame_count, frame_index;
static uint8_t *previous_xbm, *full_xbm, *region_xbm;
static size_t canvas_size, xbm_size;
static uint64_t bitmap_pixels, bitmap_bytes;

static int64_t now_ns(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void checked_tick(solar_os_context_t *ctx, bool full)
{
    assert(sketch.render_pending);
    assert(solar_os_sketch_canvas_render_xbm(&sketch.canvas, full_xbm, xbm_size));
    if (!full && !sketch.full_render_pending) {
        assert(sketch.paint_region_valid);
        const int x0=sketch.paint_x0, y0=sketch.paint_y0;
        const int w=sketch.paint_x1-x0, h=sketch.paint_y1-y0;
        const size_t region_stride=((size_t)w+7)/8;
        const size_t full_stride=((size_t)sketch.canvas.width+7)/8;
        assert(solar_os_sketch_canvas_render_xbm_region(&sketch.canvas,
            x0,y0,w,h,region_xbm,xbm_size));
        /* Compose the actual region over the last frame and check ALL pixels,
         * including unchanged areas, Bayer phase and unaligned byte edges. */
        for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
            const uint8_t mask=(uint8_t)(1U<<((x0+x)&7));
            uint8_t *p=&previous_xbm[(y0+y)*full_stride+(x0+x)/8];
            if (region_xbm[y*region_stride+x/8] & (1U<<(x&7))) *p|=mask;
            else *p&=(uint8_t)~mask;
        }
        assert(!memcmp(previous_xbm,full_xbm,xbm_size));
    }
    memcpy(previous_xbm,full_xbm,xbm_size);
    if (full) {
        assert(frame_count<MAX_FRAMES);
        frames[frame_count]=malloc(canvas_size); assert(frames[frame_count]);
        memcpy(frames[frame_count++],sketch.canvas.pixels,canvas_size);
        /* Existing full-render request only: no selector in production. */
        sketch_request_full();
    } else {
        assert(frame_index<frame_count);
        assert(!memcmp(frames[frame_index++],sketch.canvas.pixels,canvas_size));
    }
    const unsigned before=submits;
    tick(ctx);
    assert(submits==before+1 && !sketch.render_pending);
    bitmap_pixels+=(uint64_t)last_bitmap_width*last_bitmap_height;
    bitmap_bytes+=((size_t)last_bitmap_width+7)/8*last_bitmap_height;
}

static void run(bool full)
{
    solar_os_context_t ctx=begin(0); assert(sketch_start(&ctx)==ESP_OK);
    const sketch_layout_t l=sketch_layout(ctx.gfx);
    canvas_size=solar_os_sketch_canvas_bytes(sketch.canvas.width,sketch.canvas.height);
    xbm_size=sketch.canvas_bitmap_size;
    previous_xbm=calloc(1,xbm_size); full_xbm=malloc(xbm_size); region_xbm=malloc(xbm_size);
    assert(previous_xbm && full_xbm && region_xbm);
    assert(solar_os_sketch_canvas_render_xbm(&sketch.canvas,previous_xbm,xbm_size));
    bitmap_pixels=bitmap_bytes=0;
    const unsigned initial_submits=submits, initial_clears=full_clears;
    for (int stroke=0; stroke<4; ++stroke) {
        const int x=l.canvas_x+5, y=l.canvas_y+10+(stroke%3)*35;
        sketch.weight=stroke==0?1:8;
        sketch.color=(uint8_t)(3-stroke%3);
        sketch.tool=stroke==3?SKETCH_TOOL_ERASER:SKETCH_TOOL_FREEHAND;
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,x,y); checked_tick(&ctx,full);
        for (int step=1; step<=8; ++step) {
            pointer(&ctx,SOLAR_OS_INPUT_POINTER_MOVE,x+step*13,y+(step%2)*11);
            if (!(step%2)) checked_tick(&ctx,full);
        }
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,x+8*13+3,y+4);
        checked_tick(&ctx,full);
        assert(!sketch.drawing);
        const unsigned done=submits;
        pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,x+8*13+3,y+4);
        tick(&ctx); assert(submits==done); /* No extra unchanged RELEASE frame. */
    }
    assert(allocations==3); /* Same persistent Canvas/preview/XBM allocations. */
    printf("render_compare %s frames=%u full_clears=%u bitmap_pixels=%llu xbm_bytes=%llu persistent_canvas=%zu preview=%zu bitmap=%zu\n",
        full?"full":"local",submits-initial_submits,full_clears-initial_clears,
        (unsigned long long)bitmap_pixels,(unsigned long long)bitmap_bytes,
        canvas_size,canvas_size,xbm_size);
    free(previous_xbm); free(full_xbm); free(region_xbm); end(&ctx);
}

static void benchmark(void)
{
    /* Isolated real XBM conversion: same 736x392 Canvas, 53x19 ROI, 200 calls.
     * Test/composition overhead and peripheral stubs are excluded. */
    uint8_t pixels[736*392/4], bitmap[736*392/8];
    solar_os_sketch_canvas_t canvas={.width=736,.height=392,.pixels=pixels};
    memset(pixels,0x1b,sizeof(pixels));
    volatile unsigned checksum=0;
    for (int local=0; local<2; ++local) {
        const int64_t start=now_ns();
        for (int i=0; i<200; ++i) {
            assert(local?solar_os_sketch_canvas_render_xbm_region(&canvas,7,9,53,19,bitmap,sizeof(bitmap)):
                solar_os_sketch_canvas_render_xbm(&canvas,bitmap,sizeof(bitmap)));
            checksum+=bitmap[i%100];
        }
        printf("xbm_host %s iterations=200 mean_us=%.3f\n",local?"53x19":"736x392",(now_ns()-start)/200000.0);
    }
    const int64_t start=now_ns();
    for(int i=0;i<2000;++i)
        solar_os_sketch_canvas_line(&canvas,7,9,60,27,(uint8_t)(i%4),0,8);
    printf("canvas_host line53x18 weight8 iterations=2000 mean_us=%.3f checksum=%u\n",(now_ns()-start)/2000000.0,checksum);
}

int main(void)
{
    strcpy(directory,"/tmp/solar-sketch-compare-XXXXXX"); assert(mkdtemp(directory));
    run(true); run(false); assert(frame_index==frame_count && frame_count==24);
    for(size_t i=0;i<frame_count;++i) free(frames[i]);
    benchmark(); assert(!rmdir(directory));
    puts("sketch_render_compare: full/local Canvas and composed XBM match at every frame; endpoint/eraser/coalescing PASS");
    return 0;
}
