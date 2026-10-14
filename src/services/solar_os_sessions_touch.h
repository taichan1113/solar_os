#pragma once

#include "solar_os.h"

/* Application-task adapter. Bind before acquisition; unbind BEFORE app suspend,
 * stop, context release or focus change. The descriptor must outlive the bind. */
typedef bool (*solar_os_sessions_touch_deliver_t)(void *identity, const solar_os_event_t *event);
esp_err_t solar_os_sessions_touch_bind(uint8_t owner, const char *target,
    const solar_os_app_t *app, void *identity, solar_os_sessions_touch_deliver_t deliver);
void solar_os_sessions_touch_unbind(uint8_t owner);
void solar_os_sessions_touch_unbind_all(void);
bool solar_os_sessions_touch_dispatch(const solar_os_input_touch_frame_t *frame,
    uint16_t width, uint16_t height, uint16_t orientation);
