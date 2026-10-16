#include "solar_os_type_mouse_engine.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct Reports {
    unsigned count = 0;
    bool send(const solar_os_type_mouse_mouse_t *report) {
        ++count;
        return report != nullptr;
    }
};

static bool send_report(void *context, const solar_os_type_mouse_mouse_t *report) {
    return static_cast<Reports *>(context)->send(report);
}

static solar_os_type_mouse_point_t point(uint8_t id, int key) {
    int x, y, w, h;
    assert(solar_os_type_mouse_engine_rect(key, &x, &y, &w, &h));
    return {id, static_cast<int16_t>(x+w/2), static_cast<int16_t>(y+h/2)};
}

int main() {
    Reports reports;
    auto *engine = solar_os_type_mouse_engine_create(send_report, &reports);
    assert(engine);
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 13), "A"));
    assert(!std::strcmp(solar_os_type_mouse_engine_hold_label(24), "Shift"));
    assert(!std::strcmp(solar_os_type_mouse_engine_hold_label(37), "Sym"));
    assert(!std::strcmp(solar_os_type_mouse_engine_layer_label(engine), "Base"));
    solar_os_type_mouse_keys_t keys{};
    auto a = point(0, 13);
    auto same_a = point(1, 13);
    solar_os_type_mouse_point_t both[] = {a, same_a};
    solar_os_type_mouse_engine_type(engine, both, 2, 10, &keys);
    assert(keys.keys[0] == 0x04 && keys.keys[1] == 0);
    solar_os_type_mouse_engine_type(engine, &same_a, 1, 30, &keys);
    assert(keys.keys[0] == 0x04); // One finger still owns A.
    assert(solar_os_type_mouse_engine_visual_positions(engine) == (UINT64_C(1) << 13));
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 50, &keys);
    assert(keys.keys[0] == 0);

    auto l = point(2, 21);
    solar_os_type_mouse_point_t separated[] = {a, l};
    solar_os_type_mouse_engine_type(engine, separated, 2, 51, &keys);
    assert(keys.keys[0] == 0x04 && keys.keys[1] == 0x0F);
    solar_os_type_mouse_engine_type(engine, &l, 1, 52, &keys);
    assert(keys.keys[0] == 0x0F && keys.keys[1] == 0);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 53, &keys);
    assert(keys.keys[0] == 0 && keys.keys[1] == 0);

    auto shift = point(3, 24);
    auto b = point(5, 29);
    solar_os_type_mouse_engine_type(engine, &shift, 1, 60, &keys);
    assert(keys.modifiers & 0x02);
    assert(solar_os_type_mouse_engine_held_modifiers(engine) & 0x02);
    solar_os_type_mouse_point_t shifted[] = {b, shift}; // Slot order swaps.
    solar_os_type_mouse_engine_type(engine, shifted, 2, 80, &keys);
    assert(keys.keys[0] == 0x05 && (keys.modifiers & 0x02));
    solar_os_type_mouse_engine_type(engine, &shift, 1, 100, &keys);
    assert(keys.keys[0] == 0 && (keys.modifiers & 0x02));
    solar_os_type_mouse_engine_cancel(engine);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 120, &keys);
    assert(keys.modifiers == 0 && keys.keys[0] == 0);

    auto alt = point(12, 39);
    solar_os_type_mouse_engine_type(engine, &alt, 1, 120, &keys);
    assert(solar_os_type_mouse_engine_held_modifiers(engine) & 0x04);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 120, &keys);

    auto sym = point(6, 37);
    auto symbol_l = point(7, 21);
    solar_os_type_mouse_engine_type(engine, &sym, 1, 121, &keys);
    assert(solar_os_type_mouse_engine_layer(engine) == 1);
    assert(!std::strcmp(solar_os_type_mouse_engine_layer_label(engine), "Sym"));
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 21), "]"));
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 27), "C"));
    solar_os_type_mouse_point_t symbol_pair[] = {sym, symbol_l};
    solar_os_type_mouse_engine_type(engine, symbol_pair, 2, 122, &keys);
    assert(keys.keys[0] == 0x32); // JIS mapping for ].
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 123, &keys);

    auto nav = point(8, 38);
    auto j = point(9, 19);
    solar_os_type_mouse_engine_type(engine, &nav, 1, 124, &keys);
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 19), "Left"));
    assert(!std::strcmp(solar_os_type_mouse_engine_layer_label(engine), "Nav"));
    solar_os_type_mouse_point_t nav_pair[] = {nav, j};
    solar_os_type_mouse_engine_type(engine, nav_pair, 2, 125, &keys);
    assert(keys.keys[0] == 0x50); // Left arrow.
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 126, &keys);

    auto fn = point(10, 0);
    auto e = point(11, 3);
    solar_os_type_mouse_engine_type(engine, &fn, 1, 127, &keys);
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 3), "F3"));
    assert(!std::strcmp(solar_os_type_mouse_engine_layer_label(engine), "Fn"));
    solar_os_type_mouse_point_t function_pair[] = {fn, e};
    solar_os_type_mouse_engine_type(engine, function_pair, 2, 128, &keys);
    assert(keys.keys[0] == 0x3C); // F3.
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 129, &keys);

    auto mouse = point(4, 41);
    solar_os_type_mouse_engine_type(engine, &mouse, 1, 130, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 160, &keys);
    assert(keys.switch_mouse);

    solar_os_type_mouse_point_t pad[] = {{1, 300, 300}};
    solar_os_type_mouse_engine_mouse(engine, pad, 1, 1, 200);
    pad[0].x = 360;
    solar_os_type_mouse_engine_mouse(engine, pad, 1, 2, 240);
    solar_os_type_mouse_engine_mouse(engine, nullptr, 0, 3, 270);
    assert(reports.count > 0);

    // A solo long hold locks the action after release, leaving the other
    // finger free to reach a key next to the layer or modifier key.
    solar_os_type_mouse_engine_type(engine, &shift, 1, 300, &keys);
    assert(solar_os_type_mouse_engine_visual_positions(engine) & (UINT64_C(1) << 24));
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 1300, &keys);
    assert(keys.modifiers == 0x02 && keys.tap_usage == 0);
    assert(solar_os_type_mouse_engine_locked_modifiers(engine) == 0x02);
    assert(solar_os_type_mouse_engine_visual_positions(engine) == (UINT64_C(1) << 24));
    auto z = point(0, 25); // Adjacent to the Shift/Esc key.
    solar_os_type_mouse_engine_type(engine, &z, 1, 1320, &keys);
    assert(keys.keys[0] == 0x1D && (keys.modifiers & 0x02));
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 1340, &keys);
    assert(keys.modifiers == 0x02);
    solar_os_type_mouse_engine_type(engine, &shift, 1, 1400, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 2400, &keys);
    assert(keys.modifiers == 0 && keys.tap_usage == 0);
    assert(solar_os_type_mouse_engine_locked_modifiers(engine) == 0);
    assert(solar_os_type_mouse_engine_visual_positions(engine) == 0);

    solar_os_type_mouse_engine_type(engine, &shift, 1, 2500, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 2550, &keys);
    assert(keys.tap_usage == 0x29); // Short Esc tap is unchanged.

    solar_os_type_mouse_engine_type(engine, &sym, 1, 2600, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 3600, &keys);
    assert(solar_os_type_mouse_engine_layer(engine) == 1);
    assert(solar_os_type_mouse_engine_locked_layers(engine) == (1U << 1));
    assert(solar_os_type_mouse_engine_visual_positions(engine) == (UINT64_C(1) << 37));
    solar_os_type_mouse_engine_type(engine, &l, 1, 3620, &keys);
    assert(keys.keys[0] == 0x32); // Sym mapping persists without two fingers.
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 3640, &keys);
    solar_os_type_mouse_engine_type(engine, &sym, 1, 3700, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 4700, &keys);
    assert(solar_os_type_mouse_engine_layer(engine) == 0);
    assert(solar_os_type_mouse_engine_visual_positions(engine) == 0);

    // A two-finger chord remains momentary even when held past the threshold.
    solar_os_type_mouse_engine_type(engine, &shift, 1, 4800, &keys);
    solar_os_type_mouse_point_t chord[] = {shift, z};
    solar_os_type_mouse_engine_type(engine, chord, 2, 4850, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 5900, &keys);
    assert(solar_os_type_mouse_engine_locked_modifiers(engine) == 0);
    assert(keys.modifiers == 0);

    solar_os_type_mouse_engine_type(engine, &nav, 1, 6000, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 7000, &keys);
    assert(solar_os_type_mouse_engine_locked_layers(engine) == (1U << 2));
    solar_os_type_mouse_engine_cancel(engine);
    assert(solar_os_type_mouse_engine_locked_layers(engine) == 0);
    assert(solar_os_type_mouse_engine_locked_modifiers(engine) == 0);
    assert(solar_os_type_mouse_engine_visual_positions(engine) == 0);
    auto ctrl = point(1, 12);
    solar_os_type_mouse_engine_type(engine, &ctrl, 1, 7100, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 8100, &keys);
    assert(keys.modifiers == 0x01 && keys.tap_usage == 0);
    solar_os_type_mouse_engine_cancel(engine);
    solar_os_type_mouse_engine_type(engine, &fn, 1, 8200, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 9200, &keys);
    assert(solar_os_type_mouse_engine_layer(engine) == 3);
    assert(!std::strcmp(solar_os_type_mouse_engine_tap_label(engine, 3), "F3"));
    solar_os_type_mouse_engine_cancel(engine);
    // Alt retains its existing momentary behavior, including long holds.
    solar_os_type_mouse_engine_type(engine, &alt, 1, 9300, &keys);
    solar_os_type_mouse_engine_type(engine, nullptr, 0, 10300, &keys);
    assert(keys.modifiers == 0 && !solar_os_type_mouse_engine_locked_modifiers(engine));
    assert(solar_os_type_mouse_engine_visual_positions(engine) == 0);
    solar_os_type_mouse_engine_destroy(engine);
    std::puts("type_mouse_engine_test: PASS");
}
