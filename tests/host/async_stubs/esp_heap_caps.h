#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
void *heap_caps_malloc(size_t, uint32_t);
void *heap_caps_calloc(size_t, size_t, uint32_t);
void heap_caps_free(void *);
