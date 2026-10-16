/* Verify the actual trial renderer preserves the fixed left image. */
#include <assert.h>
#include <string.h>
#define main existing_epd_test_main
#include "epd_ssd1677_test.c"
#undef main
#include "solar_os_fonts.h"
#define SOLAR_OS_FIXED_BLACK_FRAME_ONLY 1
#include "../../src/shell/solar_os_shell_fixed_black_trial.inc"

static uint8_t initial_frame[48000];

int main(void)
{
    const u8g2_cb_t *rotations[] = {U8G2_R0, U8G2_R2};
    for (size_t rotation = 0; rotation < 2; ++rotation) {
        epd_ssd1677_t d = make_display();
        u8g2_SetupDisplay(&d.u8g2, ssd1677_u8x8_display_cb,
                          u8x8_dummy_cb, u8x8_dummy_cb, u8x8_dummy_cb);
        u8g2_SetupBuffer(&d.u8g2, frame, 60,
                         u8g2_ll_hvline_vertical_top_lsb, rotations[rotation]);
        fixed_black_trial_image(&d.u8g2);
        memcpy(initial_frame, frame, sizeof(frame));
        const uint32_t fixed_hash = fixed_black_trial_left_hash(&d.u8g2, frame);
        for (unsigned step = 1; step <= 6; ++step) {
            memcpy(frame, initial_frame, sizeof(frame));
            fixed_black_trial_right(&d.u8g2, step);
            assert(fixed_black_trial_left_hash(&d.u8g2, frame) == fixed_hash);
            assert(fixed_black_trial_changed(initial_frame, frame, sizeof(frame)) > 0);
            const size_t first_column = rotation == 0 ? 0 : 400;
            for (size_t tile_y = 0; tile_y < 60; ++tile_y)
                assert(!memcmp(frame + tile_y * 800 + first_column,
                               initial_frame + tile_y * 800 + first_column, 400));
        }
    }
    puts("fixed_black_frame_test: PASS");
    return 0;
}
