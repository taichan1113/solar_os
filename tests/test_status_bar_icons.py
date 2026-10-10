from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/services/solar_os_terminal.c").read_text()


def function(name):
    start = SOURCE.rfind("\nstatic ", 0, SOURCE.index(name + "(")) + 1
    opening = SOURCE.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


class StatusBarIconsTest(unittest.TestCase):
    def test_production_icon_pixels(self):
        helpers = "\n".join(function(name) for name in (
            "terminal_draw_status_slash", "terminal_draw_diag_down",
            "terminal_draw_diag_up", "terminal_draw_keyboard_icon",
            "terminal_draw_bluetooth_icon",
        ))
        harness = r'''
#include <assert.h>
#include "solar_os_ui_scale.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u8g2_uint_t;
typedef struct { unsigned char pixels[16][24]; } u8g2_t;
static void u8g2_DrawPixel(u8g2_t *u, unsigned x, unsigned y) {
    assert(x < 24 && y < 16); u->pixels[y][x] = 1;
}
static void u8g2_DrawHLine(u8g2_t *u, unsigned x, unsigned y, unsigned n) {
    for (unsigned i=0; i<n; ++i) u8g2_DrawPixel(u,x+i,y);
}
static void u8g2_DrawVLine(u8g2_t *u, unsigned x, unsigned y, unsigned n) {
    for (unsigned i=0; i<n; ++i) u8g2_DrawPixel(u,x,y+i);
}
static void u8g2_DrawFrame(u8g2_t *u, unsigned x, unsigned y, unsigned w, unsigned h) {
    u8g2_DrawHLine(u,x,y,w); u8g2_DrawHLine(u,x,y+h-1,w);
    u8g2_DrawVLine(u,x,y,h); u8g2_DrawVLine(u,x+w-1,y,h);
}
static void u8g2_DrawBox(u8g2_t *u, unsigned x, unsigned y, unsigned w, unsigned h) {
    for (unsigned i=0; i<h; ++i) u8g2_DrawHLine(u,x,y+i,w);
}
''' + helpers + r'''
int main(void) {
    u8g2_t icons[5] = {0}, disabled_scanning = {0};
    terminal_draw_bluetooth_icon(&icons[0],0,3,true,false);
    terminal_draw_bluetooth_icon(&icons[1],0,3,false,false);
    terminal_draw_bluetooth_icon(&icons[2],0,3,true,true);
    terminal_draw_keyboard_icon(&icons[3],0,3,true);
    terminal_draw_keyboard_icon(&icons[4],0,3,false);
    terminal_draw_bluetooth_icon(&disabled_scanning,0,3,false,true);
    assert(memcmp(&icons[1],&disabled_scanning,sizeof(u8g2_t)) == 0);
    for (int a=0; a<5; ++a) for (int b=a+1; b<5; ++b)
        assert(memcmp(&icons[a],&icons[b],sizeof(u8g2_t)) != 0);
    for (int a=0; a<5; ++a) for (int y=0; y<16; ++y)
        for (int x=a<3 ? 14 : 18; x<24; ++x) assert(!icons[a].pixels[y][x]);
    puts("P1\n120 16");
    for (int y=0; y<16; ++y) {
        for (int a=0; a<5; ++a) for (int x=0; x<24; ++x)
            printf("%u ",icons[a].pixels[y][x]);
        puts("");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "icons.c"
            binary = Path(directory) / "icons"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            f"-I{ROOT / 'tests/host'}", f"-I{ROOT / 'src/services'}", str(source), "-o", str(binary)], check=True)
            preview = subprocess.check_output([str(binary)])
            if os.environ.get("SOLAROS_ICON_PREVIEW"):
                Path(os.environ["SOLAROS_ICON_PREVIEW"]).write_bytes(preview)

    def test_all_icons_scale_and_fit(self):
        helpers = "\n".join(function(name) for name in (
            "terminal_draw_status_slash", "terminal_draw_diag_down",
            "terminal_draw_diag_up", "terminal_draw_battery_icon",
            "terminal_draw_plug_icon", "terminal_draw_keyboard_icon",
            "terminal_draw_bluetooth_icon", "terminal_draw_wifi_icon",
            "terminal_draw_sd_icon", "terminal_draw_speaker_icon",
            "terminal_draw_radio_icon", "terminal_draw_link_icon",
            "terminal_draw_clock_icon",
        ))
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "solar_os_ui_scale.h"
typedef unsigned u8g2_uint_t;
typedef struct { unsigned char pixels[24][32]; } u8g2_t;
static void u8g2_DrawPixel(u8g2_t *u, unsigned x, unsigned y) {
    assert(x < 32 && y < 24); u->pixels[y][x] = 1;
}
static void u8g2_DrawHLine(u8g2_t *u, unsigned x, unsigned y, unsigned n) {
    for (unsigned i=0; i<n; ++i) u8g2_DrawPixel(u,x+i,y);
}
static void u8g2_DrawVLine(u8g2_t *u, unsigned x, unsigned y, unsigned n) {
    for (unsigned i=0; i<n; ++i) u8g2_DrawPixel(u,x,y+i);
}
static void u8g2_DrawFrame(u8g2_t *u, unsigned x, unsigned y, unsigned w, unsigned h) {
    u8g2_DrawHLine(u,x,y,w); u8g2_DrawHLine(u,x,y+h-1,w);
    u8g2_DrawVLine(u,x,y,h); u8g2_DrawVLine(u,x+w-1,y,h);
}
static void u8g2_DrawBox(u8g2_t *u, unsigned x, unsigned y, unsigned w, unsigned h) {
    for (unsigned i=0; i<h; ++i) u8g2_DrawHLine(u,x,y+i,w);
}
''' + helpers + r'''
static void draw(u8g2_t *u, int kind) {
    switch (kind) {
    case 0: terminal_draw_battery_icon(u,1,1,true,50); break;
    case 1: terminal_draw_plug_icon(u,1,1); break;
    case 2: terminal_draw_keyboard_icon(u,1,1,true); break;
    case 3: terminal_draw_bluetooth_icon(u,1,1,true,true); break;
    case 4: terminal_draw_wifi_icon(u,1,1,true,true,true,3); break;
    case 5: terminal_draw_sd_icon(u,1,1,true); break;
    case 6: terminal_draw_speaker_icon(u,1,1,true,100); break;
    case 7: terminal_draw_radio_icon(u,1,1); break;
    case 8: terminal_draw_link_icon(u,1,1); break;
    case 9: terminal_draw_clock_icon(u,1,1,true); break;
    }
}
static void bounds(const u8g2_t *u, int *w, int *h) {
    int minx=32, miny=24, maxx=-1, maxy=-1;
    for (int y=0;y<24;y++) for (int x=0;x<32;x++) if (u->pixels[y][x]) {
        if (x<minx) minx=x;
        if (y<miny) miny=y;
        if (x>maxx) maxx=x;
        if (y>maxy) maxy=y;
    }
    assert(maxx>=0); *w=maxx-minx+1; *h=maxy-miny+1;
}
int main(void) {
    u8g2_t icons[10]={0};
    for (int i=0;i<10;i++) {
        u8g2_t original={0}; int w,h,sw,sh;
        assert(solar_os_ui_scale_set_percent(100)); draw(&original,i);
        bounds(&original,&w,&h);
        assert(solar_os_ui_scale_set_percent(140)); draw(&icons[i],i);
        bounds(&icons[i],&sw,&sh);
        /* Every icon grows in both axes, including the previously native clock. */
        assert(sw>w && sh>h);
        assert(sw>=solar_os_ui_px(w)-2 && sw<=solar_os_ui_px(w)+2);
        assert(sh>=solar_os_ui_px(h)-2 && sh<=solar_os_ui_px(h)+2);
        assert(sh<=17);
        assert(solar_os_ui_scale_set_percent(100));
        u8g2_t restored={0}; draw(&restored,i);
        assert(memcmp(&original,&restored,sizeof(original))==0);
    }
    puts("P1\n320 24");
    for (int y=0;y<24;y++) {
        for (int i=0;i<10;i++) for (int x=0;x<32;x++)
            printf("%u ",icons[i].pixels[y][x]);
        puts("");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "all_icons.c"
            binary = Path(directory) / "all_icons"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-DSOLAR_OS_BOARD_UI_SCALE_PERCENT=140",
                            f"-I{ROOT / 'tests/host'}", f"-I{ROOT / 'src/services'}",
                            str(source), str(ROOT / "src/services/solar_os_ui_scale.c"),
                            "-o", str(binary)], check=True)
            preview = subprocess.check_output([str(binary)])
            if os.environ.get("SOLAROS_SCALED_ICON_PREVIEW"):
                Path(os.environ["SOLAROS_SCALED_ICON_PREVIEW"]).write_bytes(preview)

    def test_status_changes_invalidate_rendering(self):
        for field in ("bluetooth_supported", "bluetooth_enabled", "bluetooth_scanning"):
            self.assertIn("a->" + field + " == b->" + field,
                          function("terminal_status_bar_equal"))
            self.assertIn("status->" + field, function("terminal_render_status_hash"))

    def test_keyboard_and_bluetooth_are_independent(self):
        draw = function("terminal_draw_status_bar")
        self.assertEqual(draw.count("status->keyboard_count > 0"), 2)
        self.assertEqual(draw.count("terminal_draw_bluetooth_icon("), 2)
        self.assertNotIn("scanning", function("terminal_draw_keyboard_icon"))
        self.assertNotIn("count", function("terminal_draw_keyboard_icon"))
        for path in ("src/main.c", "src/shell/solar_os_shell_hardware.c"):
            source = (ROOT / path).read_text()
            self.assertNotIn("keyboard_scanning", source)
            self.assertIn("status.bluetooth_enabled = solar_os_ble_keyboard_enabled_for_current_boot()", source)
            self.assertIn("status.bluetooth_scanning =", source)


if __name__ == "__main__":
    unittest.main()
