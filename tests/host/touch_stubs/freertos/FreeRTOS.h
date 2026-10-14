#pragma once
#include "../../async_stubs/freertos/FreeRTOS.h"
#define tskIDLE_PRIORITY 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
