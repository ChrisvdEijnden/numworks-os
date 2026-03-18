#pragma once
#include <stdint.h>
#include <stddef.h>

void  mem_init(void);
void *mem_alloc(size_t bytes);
void  mem_free(void *ptr);
void  mem_stats(uint32_t *used, uint32_t *free_bytes);
