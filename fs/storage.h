/* ================================================================
 * NumWorks OS — Flash storage for the file system
 * File: fs/storage.h
 *
 * A region of NOR flash that reads as ordinary memory. Erased bytes read
 * 0xFF; programming can only clear bits, so a byte can be programmed
 * once between erases. Implemented by fs/storage_qspi.c (the external
 * AT25SF641) and, in host tests, by a RAM simulation.
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define STORAGE_ERASE_SIZE 4096U

/* Find and check the flash; false if there is no usable storage, and
 * storage_status() says why */
bool           storage_init(void);
const char    *storage_status(void);

const uint8_t *storage_base(void);     /* the region, readable in place */
uint32_t       storage_size(void);

/* offset and len are relative to the region. Erase works on whole
 * STORAGE_ERASE_SIZE blocks. src may point anywhere, including into the
 * region itself. 0 on success, -1 on failure. */
int storage_erase(uint32_t offset, uint32_t len);
int storage_program(uint32_t offset, const void *src, uint32_t len);
