/* RAM model of the storage region: NOR flash semantics (programming
 * only clears bits), with power cuts and write protection injectable. */
#include <string.h>
#include <setjmp.h>
#include "../../fs/storage.h"
#include "../../include/config.h"
uint8_t sim_mem[STORAGE_SIZE];
int sim_present = 1, sim_protected = 0;
long sim_ops = 0;            /* program/erase operations so far */
long sim_cut_at = -1;        /* power cut during this operation */
jmp_buf sim_cut;
bool storage_init(void) { return sim_present; }
const char *storage_status(void) { return sim_present ? "ok" : "sim: absent"; }
const uint8_t *storage_base(void) { return sim_mem; }
uint32_t storage_size(void) { return STORAGE_SIZE; }
static uint32_t rng = 12345;
static uint8_t rbits(void) { rng = rng * 1103515245u + 12345u; return (uint8_t)(rng >> 16); }
static void maybe_cut(uint32_t off, uint32_t len, int erase, const uint8_t *src) {
    if (++sim_ops != sim_cut_at) return;
    /* The power goes mid-operation: an erase has set some bits to 1, a
     * program has cleared some of the bits it was going to clear. */
    for (uint32_t i = 0; i < len; i++) {
        if (erase) sim_mem[off + i] |= rbits();
        else       sim_mem[off + i] &= (uint8_t)(src[i] | rbits());
    }
    longjmp(sim_cut, 1);
}
int storage_erase(uint32_t off, uint32_t len) {
    if (off % STORAGE_ERASE_SIZE || len % STORAGE_ERASE_SIZE || off + len > STORAGE_SIZE) return -1;
    for (uint32_t b = 0; b < len; b += STORAGE_ERASE_SIZE) {
        maybe_cut(off + b, STORAGE_ERASE_SIZE, 1, NULL);
        if (!sim_protected) memset(sim_mem + off + b, 0xFF, STORAGE_ERASE_SIZE);
    }
    return 0;
}
int storage_program(uint32_t off, const void *src, uint32_t len) {
    if (off + len > STORAGE_SIZE) return -1;
    static uint8_t tmp[STORAGE_SIZE];     /* any length, like the real driver */
    memcpy(tmp, src, len);                /* src may be inside sim_mem */
    maybe_cut(off, len, 0, tmp);
    if (!sim_protected) for (uint32_t i = 0; i < len; i++) sim_mem[off + i] &= tmp[i];
    return 0;
}
