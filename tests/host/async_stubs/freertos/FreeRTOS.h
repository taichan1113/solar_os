#pragma once
#include <stdint.h>
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
#define tskNO_AFFINITY (-1)
