/* Host simulation of the STM32F7 QUADSPI controller and an AT25SF641,
 * for fs/storage_qspi.c (compiled with -DQSPI_SIM). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
enum { R_CR, R_DCR, R_SR, R_FCR, R_DLR, R_CCR, R_AR, R_ABR, R_DR };
uint32_t sim_rd(int reg);
void     sim_wr(int reg, uint32_t v);
uint8_t  sim_dr_rd8(void);
void     sim_dr_wr8(uint8_t v);
void     sim_irq(bool on);
void     sim_guard(bool on);
void     sim_wdg(void);
extern uint8_t *sim_view;               /* the memory-mapped window */
#define REG_RD(r)      sim_rd(R_##r)
#define REG_WR(r, v)   sim_wr(R_##r, (v))
#define DR_RD8()       sim_dr_rd8()
#define DR_WR8(v)      sim_dr_wr8(v)
#define IRQ_OFF()      sim_irq(false)
#define IRQ_ON()       sim_irq(true)
#define BARRIER()      ((void)0)
#define WDG_FEED()     sim_wdg()
#define RAMFUNC
#define INLINE         static inline
#define MAP_BASE       ((uintptr_t)sim_view)
#define GUARD_ON()     sim_guard(true)
#define GUARD_OFF()    sim_guard(false)
