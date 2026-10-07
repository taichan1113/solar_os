#pragma once
#include "freertos/task.h"
#define SOLAR_OS_TASK_ROLE_SYSTEM 0
BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t, const char *, uint32_t,
    void *, UBaseType_t, TaskHandle_t *, BaseType_t, int);
void solar_os_task_delete_internal(TaskHandle_t);
