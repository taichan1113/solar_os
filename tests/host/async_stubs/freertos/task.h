#pragma once
#include "FreeRTOS.h"
typedef struct host_async_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
unsigned ulTaskNotifyTake(int, TickType_t);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelay(TickType_t);
