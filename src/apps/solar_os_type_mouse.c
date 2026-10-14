#include "solar_os_type_mouse.h"

#include <stdio.h>
#include <string.h>
#include "solar_os_type_mouse_engine.h"
#include "solar_os_ble.h"
#include "solar_os_config.h"
#include "solar_os_expansion.h"
#include "solar_os_gfx.h"
#include "solar_os_hid.h"

typedef struct {
    solar_os_type_mouse_engine_t *engine;
    solar_os_ble_session_t ble;
    bool touch_lease, suspended, mouse, connected, keyboard_ready, mouse_ready;
    bool header_armed, header_exit;
    bool keyboard_release_pending, mouse_release_pending;
    uint8_t header_id, mouse_buttons;
    uint32_t mouse_sequence;
    uint8_t last_modifiers, last_keys[6];
    uint32_t last_status_ms, passkey;
} type_mouse_state_t;

static void *type_mouse_storage;
#define tm (*(type_mouse_state_t *)type_mouse_storage)

static void type_mouse_release(void)
{
    if (!type_mouse_storage || tm.ble == SOLAR_OS_BLE_SESSION_INVALID) return;
    tm.keyboard_release_pending =
        solar_os_ble_hid_keyboard_release_all(tm.ble) != ESP_OK;
    tm.mouse_release_pending =
        solar_os_ble_hid_mouse_report(tm.ble, 0, 0, 0, 0, 0) != ESP_OK;
    tm.last_modifiers = 0;
    memset(tm.last_keys, 0, sizeof(tm.last_keys));
    tm.mouse_buttons = 0;
}

static void type_mouse_keyboard_cache_clear(void)
{
    tm.last_modifiers = 0;
    memset(tm.last_keys, 0, sizeof(tm.last_keys));
}

static void type_mouse_touch_acquire(void)
{
#if defined(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE) && SOLAR_OS_PACKAGE_SERVICE_EXPANSION
    if (solar_os_expansion_enable_acquire(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE,
            type_mouse_storage) == ESP_OK) tm.touch_lease = true;
#endif
}

static void type_mouse_touch_release(void)
{
#if defined(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE) && SOLAR_OS_PACKAGE_SERVICE_EXPANSION
    if (tm.touch_lease)
        (void)solar_os_expansion_enable_release(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE,
            type_mouse_storage);
#endif
    tm.touch_lease = false;
}

static bool type_mouse_send(void *context, const solar_os_type_mouse_mouse_t *report)
{
    type_mouse_state_t *state = context;
    if (!state || !report || !state->connected || !state->mouse_ready ||
        state->suspended) return false;
    if (state->mouse_release_pending) {
        if (solar_os_ble_hid_mouse_report(state->ble, 0, 0, 0, 0, 0) != ESP_OK)
            return false;
        state->mouse_release_pending = false;
    }
    const esp_err_t err = solar_os_ble_hid_mouse_report(state->ble,
        report->buttons, report->dx, report->dy, report->wheel, report->pan);
    if (err == ESP_OK) state->mouse_buttons = report->buttons;
    return err == ESP_OK;
}

static void type_mouse_render(solar_os_context_t *ctx)
{
    solar_os_gfx_t *gfx = solar_os_context_gfx(ctx);
    if (!gfx || !type_mouse_storage || tm.suspended) return;
    solar_os_gfx_clear(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_set_font_native(gfx, SOLAR_OS_GFX_FONT_BOLD_20);
    solar_os_gfx_text(gfx, 18, 33, tm.mouse ? "Mouse" : "Type / Mouse");
    solar_os_gfx_set_font_native(gfx, SOLAR_OS_GFX_FONT_MONO_16);
    solar_os_gfx_text(gfx, 18, 67, tm.connected ? "BLE connected" : "BLE: pair with SolarOS TypeMouse");
    if (tm.passkey) {
        char passkey[32];
        snprintf(passkey, sizeof(passkey), "Passkey %06lu", (unsigned long)tm.passkey);
        solar_os_gfx_text(gfx, 330, 67, passkey);
    }
    solar_os_gfx_rect(gfx, 660, 4, 120, 55);
    solar_os_gfx_text(gfx, 692, 37, "Exit");
    if (tm.mouse) {
        solar_os_gfx_rect(gfx, 18, 85, 165, 62);
        solar_os_gfx_text(gfx, 42, 124, "Type");
        solar_os_gfx_text(gfx, 20, 200, "Move 1 finger / Tap to click");
        solar_os_gfx_text(gfx, 20, 245, "Double tap, hold 2nd + move to drag");
        solar_os_gfx_text(gfx, 20, 290, "2 fingers to scroll");
    } else {
        for (int i = 0; i < 42; ++i) {
            int x, y, width, height;
            if (!solar_os_type_mouse_engine_rect(i, &x, &y, &width, &height)) continue;
            solar_os_gfx_rect(gfx, x, y, width, height);
            const char *label = solar_os_type_mouse_engine_label(i);
            solar_os_gfx_text(gfx, x+4, y+height/2+6, label);
        }
        solar_os_gfx_text(gfx, 18, 459, "Tap Mouse to switch. Hold Sym/Nav/Fn for layers.");
    }
    (void)solar_os_gfx_submit(gfx, NULL);
}

static void type_mouse_mode(solar_os_context_t *ctx, bool mouse)
{
    type_mouse_release();
    solar_os_type_mouse_engine_cancel(tm.engine);
    tm.mouse = mouse;
    tm.header_armed = false;
    type_mouse_render(ctx);
}

static bool type_mouse_hid_press(const solar_os_type_mouse_keys_t *keys)
{
    uint16_t report[8]; size_t count = 0;
    for (unsigned bit = 0; bit < 8; ++bit)
        if (keys->modifiers & (1U << bit))
            report[count++] = SOLAR_OS_HID_KEY_MODIFIER_FLAG | (1U << bit);
    if (count && solar_os_ble_hid_keyboard_press(tm.ble, report, count) != ESP_OK)
        return false;
    for (size_t i = 0; i < 6; ++i)
        if (keys->keys[i]) {
            const uint16_t usage = keys->keys[i];
            if (solar_os_ble_hid_keyboard_press(tm.ble, &usage, 1) != ESP_OK)
                return false;
        }
    return true;
}

static void type_mouse_keys_send(const solar_os_type_mouse_keys_t *keys)
{
    if (!tm.keyboard_ready || !tm.connected) return;
    if (tm.keyboard_release_pending) {
        if (solar_os_ble_hid_keyboard_release_all(tm.ble) != ESP_OK) return;
        tm.keyboard_release_pending = false;
        type_mouse_keyboard_cache_clear();
    }
    if (keys->modifiers != tm.last_modifiers ||
        memcmp(keys->keys, tm.last_keys, sizeof(tm.last_keys))) {
        if (solar_os_ble_hid_keyboard_release_all(tm.ble) != ESP_OK) {
            tm.keyboard_release_pending = true;
            return;
        }
        if (!type_mouse_hid_press(keys)) {
            tm.keyboard_release_pending =
                solar_os_ble_hid_keyboard_release_all(tm.ble) != ESP_OK;
            type_mouse_keyboard_cache_clear();
            return;
        }
        tm.last_modifiers = keys->modifiers;
        memcpy(tm.last_keys, keys->keys, sizeof(tm.last_keys));
    }
    if (keys->tap_usage) {
        bool already_down = false;
        for (size_t i = 0; i < 6; ++i) already_down |= keys->tap_usage == tm.last_keys[i];
        if (already_down) return;
        uint16_t press[2] = {keys->tap_usage, 0}; size_t count = 1;
        const uint8_t added = keys->tap_modifiers & ~tm.last_modifiers;
        if (added && !(added & (added-1U))) {
            press[count++] = SOLAR_OS_HID_KEY_MODIFIER_FLAG | added;
        }
        if (solar_os_ble_hid_keyboard_press(tm.ble, press, count) != ESP_OK ||
            solar_os_ble_hid_keyboard_release(tm.ble, press, count) != ESP_OK)
            tm.keyboard_release_pending = true;
    }
}

static void type_mouse_frame(solar_os_context_t *ctx,
    const solar_os_input_touch_frame_t *frame)
{
    if (!frame || !type_mouse_storage || !tm.engine || tm.suspended) return;
    if (frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL) {
        type_mouse_release();
        solar_os_type_mouse_engine_cancel(tm.engine);
        tm.header_armed = false;
        return;
    }
    solar_os_type_mouse_point_t points[2] = {{0}};
    const size_t count = frame->snapshot.count;
    if (count > 2) return;
    for (size_t i = 0; i < count; ++i) {
        points[i].id = frame->snapshot.contacts[i].id;
        points[i].x = (int16_t)frame->snapshot.contacts[i].x;
        points[i].y = (int16_t)frame->snapshot.contacts[i].y;
    }
    if (tm.header_armed) {
        if (count == 0) {
            if (tm.header_exit) solar_os_context_finish(ctx, 0, "Type/Mouse closed");
            else type_mouse_mode(ctx, false);
        } else if (count != 1 || points[0].id != tm.header_id ||
            (tm.header_exit && !(points[0].y < 65 && points[0].x >= 660)) ||
            (!tm.header_exit && !(points[0].x < 183 &&
                points[0].y >= 85 && points[0].y < 147)))
            tm.header_armed = false;
        return;
    }
    if (count == 1 && (points[0].y < 65 && points[0].x >= 660)) {
        tm.header_armed = true; tm.header_exit = true;
        tm.header_id = points[0].id;
        return;
    }
    if (tm.mouse && count == 1 && points[0].x < 183 &&
        points[0].y >= 85 && points[0].y < 147) {
        tm.header_armed = true; tm.header_exit = false;
        tm.header_id = points[0].id;
        return;
    }
    const uint32_t ms = (uint32_t)(frame->timestamp_us / 1000U);
    if (tm.mouse) {
        solar_os_type_mouse_engine_mouse(tm.engine, points, count,
            ++tm.mouse_sequence, ms);
    } else {
        solar_os_type_mouse_keys_t keys = {0};
        solar_os_type_mouse_engine_type(tm.engine, points, count, ms, &keys);
        if (keys.switch_mouse) type_mouse_mode(ctx, true);
        else type_mouse_keys_send(&keys);
    }
}

static esp_err_t type_mouse_ble_start(void)
{
    if (tm.ble == SOLAR_OS_BLE_SESSION_INVALID) {
        esp_err_t err = solar_os_ble_session_create("type_mouse", &tm.ble);
        if (err != ESP_OK) return err;
    }
    return solar_os_ble_hid_device_start(tm.ble, "SolarOS TypeMouse");
}

static void type_mouse_cleanup(void)
{
    if (!type_mouse_storage) return;
    type_mouse_release();
    type_mouse_touch_release();
    if (tm.ble != SOLAR_OS_BLE_SESSION_INVALID) {
        (void)solar_os_ble_hid_device_stop(tm.ble);
        (void)solar_os_ble_session_close(tm.ble);
        tm.ble = SOLAR_OS_BLE_SESSION_INVALID;
    }
    solar_os_type_mouse_engine_destroy(tm.engine);
    tm.engine = NULL;
}

static esp_err_t type_mouse_start(solar_os_context_t *ctx)
{
    if (!type_mouse_storage || !solar_os_context_gfx(ctx)) return ESP_ERR_INVALID_STATE;
    memset(type_mouse_storage, 0, sizeof(type_mouse_state_t));
    tm.engine = solar_os_type_mouse_engine_create(type_mouse_send, type_mouse_storage);
    if (!tm.engine) return ESP_ERR_NO_MEM;
    type_mouse_touch_acquire();
    esp_err_t err = type_mouse_ble_start();
    if (err != ESP_OK) { type_mouse_cleanup(); return err; }
    solar_os_context_set_graphics_active(ctx, true);
    type_mouse_render(ctx);
    return ESP_OK;
}

static void type_mouse_stop(solar_os_context_t *ctx)
{ type_mouse_cleanup(); solar_os_context_set_graphics_active(ctx, false); }
static void type_mouse_suspend(solar_os_context_t *ctx)
{
    tm.suspended = true;
    type_mouse_release();
    solar_os_type_mouse_engine_cancel(tm.engine);
    type_mouse_touch_release();
    solar_os_context_set_graphics_active(ctx, false);
}
static void type_mouse_resume(solar_os_context_t *ctx)
{
    tm.suspended = false;
    type_mouse_touch_acquire();
    solar_os_context_set_graphics_active(ctx, true);
    type_mouse_render(ctx);
}

static void type_mouse_status(solar_os_context_t *ctx, uint32_t ms)
{
    if (ms - tm.last_status_ms < 150) return;
    tm.last_status_ms = ms;
    solar_os_ble_hid_info_t info = {0};
    if (solar_os_ble_hid_device_status(tm.ble, &info) != ESP_OK) return;
    const bool connected = info.connected && info.encrypted;
    const bool keyboard = connected && info.keyboard_subscribed;
    const bool mouse = connected && info.mouse_subscribed;
    if (keyboard && tm.keyboard_release_pending &&
        solar_os_ble_hid_keyboard_release_all(tm.ble) == ESP_OK) {
        tm.keyboard_release_pending = false;
        type_mouse_keyboard_cache_clear();
    }
    if (mouse && tm.mouse_release_pending &&
        solar_os_ble_hid_mouse_report(tm.ble, 0, 0, 0, 0, 0) == ESP_OK)
        tm.mouse_release_pending = false;
    if (tm.connected != connected || tm.keyboard_ready != keyboard ||
        tm.mouse_ready != mouse) {
        if (!connected || !keyboard || !mouse) type_mouse_release();
        tm.connected = connected;
        tm.keyboard_ready = keyboard;
        tm.mouse_ready = mouse;
        solar_os_type_mouse_engine_cancel(tm.engine);
        type_mouse_render(ctx);
    }
    solar_os_ble_hid_device_event_t event;
    while (solar_os_ble_hid_device_poll(tm.ble, &event) == ESP_OK)
        if (event.type == SOLAR_OS_BLE_HID_PASSKEY) {
            tm.passkey = event.passkey;
            type_mouse_render(ctx);
        }
}

static bool type_mouse_event(solar_os_context_t *ctx, const solar_os_event_t *event)
{
    if (!type_mouse_storage || !event) return false;
    switch (event->type) {
    case SOLAR_OS_EVENT_TOUCH_FRAME:
        type_mouse_frame(ctx, event->data.touch_frame); return true;
    case SOLAR_OS_EVENT_TICK:
        type_mouse_status(ctx, event->data.tick_ms); return true;
    case SOLAR_OS_EVENT_CHAR:
        if (event->data.ch == 27) {
            solar_os_context_finish(ctx, 0, "Type/Mouse closed"); return true;
        }
        if (event->data.ch == 'm' || event->data.ch == 'M') {
            type_mouse_mode(ctx, !tm.mouse); return true;
        }
        if (event->data.ch == 'p' || event->data.ch == 'P') {
            (void)solar_os_ble_hid_device_pair(tm.ble); return true;
        }
        return false;
    default: return false;
    }
}

const solar_os_app_t solar_os_type_mouse_app = {
    .name = "type-mouse", .summary = "Touch keyboard and BLE mouse",
    .app_class = SOLAR_OS_APP_CLASS_GUI,
    .flags = SOLAR_OS_APP_FLAG_RESUMABLE | SOLAR_OS_APP_FLAG_TOUCH_FRAMES,
    .start = type_mouse_start, .suspend = type_mouse_suspend,
    .resume = type_mouse_resume, .stop = type_mouse_stop,
    .event = type_mouse_event, .state_slot = &type_mouse_storage,
    .state_size = sizeof(type_mouse_state_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_PREFERRED,
    .state_release_cleanup = type_mouse_cleanup,
    .tick_interval_ms = 30U, .tick_deadline_ms = 30U,
};
