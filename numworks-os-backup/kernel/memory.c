/* ================================================================
 * NumWorks OS — Static Pool Allocator
 * File: kernel/memory.c
 *
 * A minimal fixed-size block allocator.
 * No heap fragmentation, no malloc overhead.
 * Uses a bitmap to track 64-byte blocks within the 8 KB heap.
 *
 * Code size target: < 1 KB
 * ================================================================ */
#include "memory.h"
#include "../include/config.h"
#include <string.h>
#include <stdbool.h>


#define BLOCK_SIZE   64U
#define NUM_BLOCKS   (MP_HEAP_SIZE / BLOCK_SIZE)  /* 128 blocks in heap area */

/* Heap lives in .heap section (see linker script) */
extern uint8_t _sheap[];
extern uint8_t _eheap[];

/* 1 bit per block: 0=free, 1=used. 128 bits = 16 bytes. */
static uint32_t s_bitmap[NUM_BLOCKS / 32 + 1];

static void bitmap_set(uint16_t idx) { s_bitmap[idx>>5] |=  (1U<<(idx&31)); }
static void bitmap_clr(uint16_t idx) { s_bitmap[idx>>5] &= ~(1U<<(idx&31)); }
static bool bitmap_get(uint16_t idx) { return !!(s_bitmap[idx>>5] & (1U<<(idx&31))); }

void mem_init(void) {
    memset(s_bitmap, 0, sizeof(s_bitmap));
}

/* Allocate contiguous blocks (first-fit) */
void *mem_alloc(size_t bytes) {
    if (!bytes) return NULL;
    uint16_t need = (uint16_t)((bytes + BLOCK_SIZE - 1) / BLOCK_SIZE);
    uint16_t run  = 0;
    uint16_t start = 0;

    for (uint16_t i = 0; i < NUM_BLOCKS; i++) {
        if (!bitmap_get(i)) {
            if (!run) start = i;
            if (++run == need) {
                /* Mark blocks used */
                for (uint16_t j = start; j <= i; j++) bitmap_set(j);
                /* Store block count in first byte for free() */
                uint8_t *ptr = _sheap + (size_t)start * BLOCK_SIZE;
                *(uint16_t *)ptr = need;
                return ptr + 2;
            }
        } else {
            run = 0;
        }
    }
    return NULL;  /* Out of memory */
}

void mem_free(void *ptr) {
    if (!ptr) return;
    uint8_t *raw = (uint8_t *)ptr - 2;
    uint16_t start = (uint16_t)((raw - _sheap) / BLOCK_SIZE);
    uint16_t count = *(uint16_t *)raw;
    for (uint16_t i = start; i < start + count; i++) bitmap_clr(i);
}

void mem_stats(uint32_t *used, uint32_t *free_bytes) {
    uint32_t u = 0;
    for (uint16_t i = 0; i < NUM_BLOCKS; i++) if (bitmap_get(i)) u++;
    if (used)       *used       = u * BLOCK_SIZE;
    if (free_bytes) *free_bytes = (NUM_BLOCKS - u) * BLOCK_SIZE;
}
