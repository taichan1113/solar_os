#pragma once

#include "../drivers/ft6336_frame.h"

/* Pure state comparison, no Input Service, queue, timer or gesture dependency. */
typedef struct {
    uint8_t id;
    uint16_t x, y;
} solar_os_touch_contact_t;

typedef struct {
    size_t count;
    solar_os_touch_contact_t contacts[FT6336_FRAME_CONTACTS];
} solar_os_touch_snapshot_t;

typedef struct {
    solar_os_touch_snapshot_t snapshot; /* Last trustworthy state, ID-sorted. */
    bool needs_resync;
} solar_os_input_touch_core_t;

typedef enum {
    SOLAR_OS_TOUCH_INVALID_ARGUMENT = 0,
    SOLAR_OS_TOUCH_TRANSITION,
    SOLAR_OS_TOUCH_CANCEL_REQUIRED,
    SOLAR_OS_TOUCH_RESYNCED,
} solar_os_touch_result_t;

typedef enum {
    SOLAR_OS_TOUCH_REASON_NONE = 0,
    SOLAR_OS_TOUCH_REASON_READ_FAILURE,
    SOLAR_OS_TOUCH_REASON_FRAME,
    SOLAR_OS_TOUCH_REASON_ID_DISCONTINUITY,
} solar_os_touch_reason_t;

typedef struct {
    solar_os_touch_result_t result;
    solar_os_touch_reason_t reason;
    uint32_t frame_reasons;
    solar_os_touch_snapshot_t current;
    solar_os_touch_snapshot_t started, moved, ended;
} solar_os_touch_transition_t;

/* Initialization establishes an empty baseline. NULL is a harmless no-op. */
void solar_os_input_touch_core_init(solar_os_input_touch_core_t *core);

/* frame == NULL denotes a failed acquisition, NEVER an empty frame.
 * Invalid/uncertain input invalidates stored contacts and requests cancellation;
 * it emits no normal ended/start/move edges. The next valid frame establishes a
 * fresh snapshot with RESYNCED and no edges. The consumer must handle cancel,
 * epoch and any all-fingers-up policy before forwarding subsequent transitions.
 * Complete ID replacement also requests cancellation; proximity is not identity.
 * This function does not deliver events or implement an all-fingers-up gate.
 * core, frame and out must not overlap; core must have been initialized. */
solar_os_touch_result_t solar_os_input_touch_core_apply(
    solar_os_input_touch_core_t *core, const ft6336_frame_t *frame,
    solar_os_touch_transition_t *out);
