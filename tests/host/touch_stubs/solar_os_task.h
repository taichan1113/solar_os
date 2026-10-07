#pragma once
#include "freertos/task.h"
#include <stdbool.h>
#define SOLAR_OS_TASK_ROLE_BACKGROUND 2
#define SOLAR_OS_TASK_STOP_WAIT_MS 2000
BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t, const char *, uint32_t,
    void *, UBaseType_t, TaskHandle_t *, BaseType_t, int);
void solar_os_task_delete_internal(TaskHandle_t);
bool solar_os_task_wait_done(TaskHandle_t, volatile bool *, uint32_t);
