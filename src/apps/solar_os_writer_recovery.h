#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Recovery I/O stays on the normal Writer tick. After a failure, wait another
 * idle interval instead of blocking every tick. Unsigned differences support
 * the runtime's wrapping millisecond clock. New edits reset the retry state. */
static inline bool solar_os_writer_recovery_due(bool written, bool retry,
                                                uint32_t now_ms,
                                                uint32_t edited_ms,
                                                uint32_t attempted_ms,
                                                uint32_t idle_ms)
{
    return !written && now_ms - edited_ms >= idle_ms &&
           (!retry || now_ms - attempted_ms >= idle_ms);
}
