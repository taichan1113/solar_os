/* Real Canvas -> local XBM -> GFX/U8g2 -> immutable snapshot -> SSD1677.
 * Reuse both explicit controller-RAM models; this is not an optical model.
 * App pending-region/release/admission behavior is covered by lifecycle tests. */
#define SSD1677_DIRTY_TEST_MAIN dirty_fixture_main
#include "epd_ssd1677_dirty_hold_test.c"
#include "solar_os_sketch_canvas.h"
#include "solar_os_gfx_internal.h"

static uint8_t oracle_frame[SSD1677_BUFFER_BYTES], immutable[SSD1677_BUFFER_BYTES];
static uint8_t canvas_pixels[736U * 392U / 4U], bitmap[92U * 392U];

static void setup_raster(u8g2_t *u, uint8_t *pixels, const u8g2_cb_t *rotation)
{
    u8g2_SetupDisplay(u, ssd1677_u8x8_display_cb,
                     u8x8_dummy_cb, u8x8_dummy_cb, u8x8_dummy_cb);
    u8g2_SetupBuffer(u, pixels, 60, u8g2_ll_hvline_vertical_top_lsb, rotation);
}

static void render_region(solar_os_gfx_t *gfx, solar_os_sketch_canvas_t *canvas,
                          int x, int y, int width, int height)
{
    assert(solar_os_sketch_canvas_render_xbm_region(canvas, x, y, width, height,
                                                    bitmap, sizeof(bitmap)));
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_fill_rect(gfx, 64+x, 53+y, width, height);
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_bitmap(gfx, 64+x, 53+y, width, height, bitmap);
}

static transfer_t present_and_check(epd_ssd1677_t *d, controller_ram_t *model,
                                     solar_os_gfx_t *oracle,
                                     solar_os_sketch_canvas_t *canvas, bool r2)
{
    render_region(oracle, canvas, 0, 0, canvas->width, canvas->height);
    assert(!memcmp(frame, oracle_frame, sizeof(frame)));
    /* Independent pixel check includes R2, the canvas offset and brush edges. */
    for (unsigned y=0; y<canvas->height; ++y)
        for (unsigned x=0; x<canvas->width; ++x) {
            unsigned nx=64+x, ny=53+y;
            if (r2) { nx=799-nx; ny=479-ny; }
            const bool white=(frame[(ny/8U)*800U+nx] & (1U<<(ny&7U))) != 0;
            assert(white == (solar_os_sketch_canvas_get(canvas,x,y) == 0));
        }
    memcpy(immutable,frame,sizeof(frame));
    command_count=0;
    assert(epd_ssd1677_present_snapshot(d,immutable,sizeof(immutable)) == ESP_OK);
    const transfer_t t=controller_apply(model,d);
    assert(t.activations==1 && !memcmp(shadow,immutable,sizeof(shadow)));
    assert(!memcmp(frame,immutable,sizeof(frame)) && d->snapshot==NULL);
    if (d->dirty_hold_trial) assert(!memcmp(model->bw,model->red,sizeof(model->bw)));
    return t;
}

static void scenario(bool r2, bool swap)
{
    epd_ssd1677_t d=make_display();
    d.papermono_otp=true; d.fastest_tiles=true; d.band_buffer=band;
    d.fastest_spi_hz=20000000; d.configured_spi_hz=10000000;
    const u8g2_cb_t *rotation=r2 ? U8G2_R2 : U8G2_R0;
    setup_raster(&d.u8g2,frame,rotation);
    u8g2_t oracle_u8g2;
    setup_raster(&oracle_u8g2,oracle_frame,rotation);
    solar_os_gfx_t gfx={.u8g2=&d.u8g2}, oracle={.u8g2=&oracle_u8g2};
    solar_os_gfx_clear(&gfx,SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_clear(&oracle,SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_sketch_canvas_t canvas={.pixels=canvas_pixels,.width=736,.height=392};
    solar_os_sketch_canvas_clear(&canvas,0);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK); /* OTP baseline before custom fastest. */
    controller_ram_t *model=malloc(sizeof(*model)); assert(model);
    controller_seed(model,&d); model->swap_after_activation=swap;
    assert(epd_ssd1677_set_controller_mode(&d,"dirty-window=rows-dual")==ESP_OK);
    for (unsigned i=0;i<66;++i) {
        const int x=16+(int)i*8, y=80+(int)(i%8)*20;
        /* Three segments coalesce in one local raster and one activation. */
        solar_os_sketch_canvas_line(&canvas,x,y,x+2,y,3,0,1);
        solar_os_sketch_canvas_line(&canvas,x+2,y,x+3,y+2,3,0,1);
        solar_os_sketch_canvas_line(&canvas,x+3,y+2,x+5,y+3,3,0,1);
        render_region(&gfx,&canvas,x-1,y-1,8,6);
        const transfer_t t=present_and_check(&d,model,&oracle,&canvas,r2);
        const bool full=i==0 || i==32 || i==64;
        assert(t.bw_bytes==(full ? 96000U : 800U));
        assert(t.red_bytes==(full ? 96000U : 400U));
        assert(d.dirty_hold_dual_updates==i%32+1);
        if (i==30 || i==31 || i==32 || i==63 || i==64)
            printf("pipeline R%u swap=%u update=%u ram=%zu 0x20=%u\n",
                   r2?2:0,swap,i+1,t.bw_bytes+t.red_bytes,t.activations);
    }
    assert(actual_spi_hz==20000000);
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,immutable,sizeof(immutable))==ESP_OK);
    assert(command_count==0); /* Unchanged raster does not activate the panel. */

    assert(epd_ssd1677_set_controller_mode(&d,"dirty-window=off")==ESP_OK);
    solar_os_sketch_canvas_line(&canvas,1,1,733,380,3,0,8);
    render_region(&gfx,&canvas,0,0,736,392);
    transfer_t t=present_and_check(&d,model,&oracle,&canvas,r2);
    assert(t.bw_bytes==48000 && t.red_bytes==48000);
    /* Small local raster still works with Dirty OFF, including white erasure. */
    solar_os_sketch_canvas_line(&canvas,16,8,30,20,0,0,8);
    render_region(&gfx,&canvas,10,2,27,25);
    t=present_and_check(&d,model,&oracle,&canvas,r2);
    assert(t.bw_bytes==48000 && t.red_bytes==48000);

    assert(epd_ssd1677_set_controller_mode(&d,"dirty-window=rows-dual")==ESP_OK);
    solar_os_sketch_canvas_line(&canvas,730,386,735,391,3,0,8);
    render_region(&gfx,&canvas,725,381,11,11);
    t=present_and_check(&d,model,&oracle,&canvas,r2);
    assert(t.bw_bytes==96000 && t.red_bytes==96000); /* ON re-baselines RAM. */
    solar_os_sketch_canvas_line(&canvas,0,385,3,391,3,0,8);
    render_region(&gfx,&canvas,0,380,9,12);
    t=present_and_check(&d,model,&oracle,&canvas,r2);
    assert(t.bw_bytes<96000 && t.red_bytes<96000);
    free(model);
}

int main(void)
{
    for (unsigned rotation=0;rotation<2;++rotation)
        for (unsigned swap=0;swap<2;++swap) scenario(rotation,swap);
    puts("sketch_dirty_pipeline_test: PASS (real Canvas/GFX/U8g2/SSD1677, R0/R2, RAM models, OFF/ON, 32/64 boundaries)");
    return 0;
}
