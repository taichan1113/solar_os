#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct solar_os_type_mouse_engine solar_os_type_mouse_engine_t;
typedef struct { uint8_t id; int16_t x, y; } solar_os_type_mouse_point_t;
typedef struct {
    uint8_t modifiers;
    uint8_t keys[6];
    uint8_t tap_usage, tap_modifiers;
    bool switch_mouse;
} solar_os_type_mouse_keys_t;
typedef struct {
    uint8_t buttons;
    int16_t dx, dy, wheel, pan;
} solar_os_type_mouse_mouse_t;
typedef bool (*solar_os_type_mouse_send_t)(void *context,
    const solar_os_type_mouse_mouse_t *report);

solar_os_type_mouse_engine_t *solar_os_type_mouse_engine_create(
    solar_os_type_mouse_send_t send, void *context);
void solar_os_type_mouse_engine_destroy(solar_os_type_mouse_engine_t *engine);
void solar_os_type_mouse_engine_cancel(solar_os_type_mouse_engine_t *engine);
void solar_os_type_mouse_engine_type(solar_os_type_mouse_engine_t *engine,
    const solar_os_type_mouse_point_t *points, size_t count, uint32_t ms,
    solar_os_type_mouse_keys_t *out);
void solar_os_type_mouse_engine_mouse(solar_os_type_mouse_engine_t *engine,
    const solar_os_type_mouse_point_t *points, size_t count,
    uint32_t sequence, uint32_t ms);
int solar_os_type_mouse_engine_hit(int x, int y);
const char *solar_os_type_mouse_engine_label(int position);
bool solar_os_type_mouse_engine_rect(int position, int *x, int *y,
    int *width, int *height);

#ifdef __cplusplus
}
#endif
