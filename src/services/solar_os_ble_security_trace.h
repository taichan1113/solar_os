#pragma once
#include <stdint.h>

/* Diagnostic only. SDK hooks never print or take the application log mutex. */
#if defined(SOLAR_OS_BLE_SECURITY_TRACE) && SOLAR_OS_BLE_SECURITY_TRACE
void solar_os_ble_security_trace_init(void);
void solar_os_ble_security_trace_drain(void);
void solar_os_ble_security_trace_record(const char *event, uint16_t conn,
    uintptr_t proc, uint8_t state, uint8_t flags, int a, int b);
#else
static inline void solar_os_ble_security_trace_init(void) {}
static inline void solar_os_ble_security_trace_drain(void) {}
#endif
