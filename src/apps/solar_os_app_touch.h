#pragma once
/* Small app-local adapter to existing expansion leases and pointer events.
 * Acquisition/power, epochs and release gates remain in their existing services. */
#include "solar_os_board.h"
#include "solar_os_expansion.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef struct {
    bool leased, canceling, pressed, moved;
    int x, y;
} solar_os_app_touch_t;
static inline esp_err_t solar_os_app_touch_acquire(solar_os_app_touch_t *t)
{
#if SOLAR_OS_BOARD_LAUNCHER_TOUCH_GRID && defined(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE) && SOLAR_OS_PACKAGE_SERVICE_EXPANSION
    if (!t->leased) {
        esp_err_t err = ESP_FAIL;
        for (unsigned i=0; i<3; ++i) {
            err=solar_os_expansion_enable_acquire(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE,t);
            if (err==ESP_OK) { t->leased=true; return ESP_OK; }
            if (i<2) vTaskDelay(pdMS_TO_TICKS(100));
        }
        return err;
    }
#else
    (void)t;
#endif
    return ESP_OK;
}
static inline void solar_os_app_touch_release(solar_os_app_touch_t *t)
{
#if SOLAR_OS_BOARD_LAUNCHER_TOUCH_GRID && defined(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE) && SOLAR_OS_PACKAGE_SERVICE_EXPANSION
    if (t->leased) (void)solar_os_expansion_enable_release(SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE,t);
#endif
    t->leased=false; t->pressed=false;
}
static inline void solar_os_app_touch_cancel(solar_os_app_touch_t *t)
{ t->pressed=false; t->canceling=true; }
static inline void solar_os_app_touch_suspend(solar_os_app_touch_t *t)
{ t->pressed=false; if (!t->canceling) solar_os_app_touch_release(t); }
/* A CANCEL's balanced callbacks must not power cycle or redraw the app. */
static inline bool solar_os_app_touch_resume(solar_os_app_touch_t *t)
{ if (t->canceling) { t->canceling=false; return false; } return true; }
/* Return true once on release. MOVE never redraws; 12 px rejects tap jitter,
 * and callers use a separate 48 px threshold for deliberate swipes. */
static inline bool solar_os_app_touch_end(solar_os_app_touch_t *t,
                                         const solar_os_input_pointer_event_t *p)
{
    if (p->mode!=SOLAR_OS_INPUT_POINTER_ABSOLUTE) return false;
    if (p->action==SOLAR_OS_INPUT_POINTER_PRESS) {
        t->pressed=true; t->moved=false; t->x=p->x; t->y=p->y;
    }
    if (t->pressed && (p->x-t->x>12 || t->x-p->x>12 ||
                      p->y-t->y>12 || t->y-p->y>12)) t->moved=true;
    if (p->action!=SOLAR_OS_INPUT_POINTER_RELEASE || !t->pressed) return false;
    t->pressed=false; return true;
}
