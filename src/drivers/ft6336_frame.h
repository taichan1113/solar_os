#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT6336_FRAME_CONTACTS 2U
#define FT6336_FRAME_BYTES 13U

typedef enum {
    FT6336_EVENT_DOWN = 0,
    FT6336_EVENT_UP = 1,
    FT6336_EVENT_CONTACT = 2,
    FT6336_EVENT_RESERVED = 3,
} ft6336_event_t;

/* INVALID is zero so a zero-initialized frame is never accepted as empty. */
typedef enum {
    FT6336_FRAME_INVALID = 0,
    FT6336_FRAME_OK,
    FT6336_FRAME_UNCERTAIN,
} ft6336_frame_result_t;

typedef enum {
    FT6336_FRAME_REASON_NONE = 0,
    FT6336_FRAME_REASON_ARGUMENT = 1U << 0,
    FT6336_FRAME_REASON_LENGTH = 1U << 1,
    FT6336_FRAME_REASON_COUNT = 1U << 2,
    FT6336_FRAME_REASON_DUPLICATE_ID = 1U << 3,
    FT6336_FRAME_REASON_ID_SENTINEL = 1U << 4,
    FT6336_FRAME_REASON_RESERVED_EVENT = 1U << 5,
    FT6336_FRAME_REASON_UP_COUNT_UNKNOWN = 1U << 6,
} ft6336_frame_reason_t;

typedef struct {
    uint8_t id;                 /* Full raw four-bit ID, never a slot index. */
    uint16_t x, y;              /* Raw 12-bit coordinates, no rotation/clamp. */
    ft6336_event_t event;
    bool reported;             /* Slot is included in TD_STATUS count. */
    bool valid;                /* Reported, non-sentinel, non-reserved, unique. */
} ft6336_contact_t;

typedef struct {
    uint8_t raw[FT6336_FRAME_BYTES]; /* Includes unused slots and UP records. */
    uint8_t reported_count;         /* Unclamped TD_STATUS low nibble. */
    ft6336_contact_t contacts[FT6336_FRAME_CONTACTS];
    ft6336_frame_result_t result;
    uint32_t reasons;               /* Bitset; multiple problems can coexist. */
} ft6336_frame_t;

/* Pure decoder. Requires at least 13 bytes beginning at register 0x02.
 * With non-NULL out, publishes a complete result even for bad arguments/length.
 * NULL out returns INVALID. UP in a counted slot is preserved but UNCERTAIN:
 * the FT6336G-specific UP/count contract has not been established.
 * Uncounted slots are retained for diagnostics but never interpreted as active.
 * OK means structurally usable, not proof of controller snapshot atomicity.
 * Input and output must not overlap. */
ft6336_frame_result_t ft6336_frame_decode(const uint8_t *data, size_t length,
                                          ft6336_frame_t *out);
