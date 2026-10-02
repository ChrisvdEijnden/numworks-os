/* ================================================================
 * NumWorks OS — internal-flash loader
 * File: loader/loader.c
 *
 * Lives at the start of the STM32F730's internal flash (0x0800_0000)
 * and starts the OS, which runs from the external QSPI flash at
 * 0x9000_0000. In order:
 *
 *  1. Recovery key: if 6 is held, jump to ST's built-in USB bootloader
 *     (DFU, 0483:df11), the same thing that 6 + RESET does on NumWorks'
 *     firmware. Done first, before anything else can go wrong.
 *  2. QUADSPI: pins, controller, and the flash chip brought to a known
 *     state: out of continuous-read mode and deep power-down, not busy.
 *     The Quad Enable bit is set if (and only if) it's missing; if it
 *     can't be set, single-line reads are used instead.
 *  3. Memory-mapped mode, with the settings in loader_qspi.h.
 *  4. The OS's vector table is checked (stack pointer in RAM, reset
 *     handler in the QSPI code area); if it doesn't look like a
 *     program, recovery as in 1. Otherwise: jump.
 *
 * Runs on the 16 MHz internal oscillator; the OS sets up the PLL.
 * Kept small and free of initialised data: no startup code to go
 * wrong. Any fault lights the red LED and stops.
 * ================================================================ */
#include <stdint.h>
#include <stdbool.h>
#include "loader_qspi.h"

#define REG(a)          (*(volatile uint32_t *)(a))

/* RCC */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1RSTR    REG(RCC_BASE + 0x10U)
#define RCC_AHB3RSTR    REG(RCC_BASE + 0x18U)
#define RCC_AHB1ENR     REG(RCC_BASE + 0x30U)
#define RCC_AHB3ENR     REG(RCC_BASE + 0x38U)
#define RCC_QSPI        (1U << 1)               /* AHB3 */

/* GPIO ports A..E */
enum { PA, PB, PC, PD, PE };
#define GPIO(n)         (0x40020000UL + 0x400UL * (uint32_t)(n))
#define GPIO_MODER(n)   REG(GPIO(n) + 0x00U)
#define GPIO_OTYPER(n)  REG(GPIO(n) + 0x04U)
#define GPIO_OSPEEDR(n) REG(GPIO(n) + 0x08U)
#define GPIO_PUPDR(n)   REG(GPIO(n) + 0x0CU)
#define GPIO_IDR(n)     REG(GPIO(n) + 0x10U)
#define GPIO_BSRR(n)    REG(GPIO(n) + 0x18U)
#define GPIO_AFR(n, i)  REG(GPIO(n) + 0x20U + 4U * (uint32_t)(i))
#define PORTS_A_TO_E    0x1FU

/* QUADSPI (RM0431 §14.5) */
#define QSPI_BASE       0xA0001000UL
#define Q_CR            REG(QSPI_BASE + 0x00U)
#define Q_DCR           REG(QSPI_BASE + 0x04U)
#define Q_SR            REG(QSPI_BASE + 0x08U)
#define Q_FCR           REG(QSPI_BASE + 0x0CU)
#define Q_DLR           REG(QSPI_BASE + 0x10U)
#define Q_CCR           REG(QSPI_BASE + 0x14U)
#define Q_AR            REG(QSPI_BASE + 0x18U)
#define Q_ABR           REG(QSPI_BASE + 0x1CU)
#define Q_DR8           (*(volatile uint8_t *)(QSPI_BASE + 0x20U))
#define CR_ABORT        (1U << 1)
#define SR_TCF          (1U << 1)
#define SR_FTF          (1U << 2)
#define SR_BUSY         (1U << 5)
#define FCR_ALL         0x1BU
#define FMODE_IW        (0U << 26)
#define FMODE_IR        (1U << 26)

/* Flash chip (AT25SF641 datasheet, table 7-2) */
#define CMD_WREN        0x06U
#define CMD_RDSR1       0x05U
#define CMD_RDSR2       0x35U
#define CMD_WRSR2       0x31U
#define CMD_RDID        0x9FU
#define CMD_RELEASE_DPD 0xABU
#define SR1_BUSY        0x01U
#define SR2_QE          0x02U

/* Cycle counter (time) */
#define DEMCR           REG(0xE000EDFCUL)
#define DEMCR_TRCENA    (1U << 24)
#define DWT_CTRL        REG(0xE0001000UL)
#define DWT_CYCCNT      REG(0xE0001004UL)
#define DWT_LAR         REG(0xE0001FB0UL)
#define CYCLES_PER_US   16U                      /* HSI, 16 MHz */

/* Longest wait for the flash to finish what a reset interrupted: a
 * chip erase takes up to 150 s (tCE). A flash that doesn't answer is
 * noticed at once (all bits read 1), not after this. */
#ifndef LOADER_BUSY_TIMEOUT_MS
#define LOADER_BUSY_TIMEOUT_MS 160000U
#endif

/* Memory map */
#define SCB_VTOR        REG(0xE000ED08UL)
#define QSPI_MAP        0x90000000UL
#define OS_CODE_END     (QSPI_MAP + 0x7C0000UL)  /* the file system follows */
#define RAM_START       0x20000000UL
#define RAM_END         0x20040000UL
#define SYSMEM          0x1FF00000UL             /* ST's bootloader */
#define SYSMEM_END      0x1FF10000UL

static void timer_start(void) {
    DEMCR |= DEMCR_TRCENA;
    DWT_LAR = 0xC5ACCE55U;                       /* unlock (Cortex-M7) */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1U;
}

static void timer_stop(void) {                   /* back to the reset state */
    DWT_CTRL &= ~1U;
    DEMCR &= ~DEMCR_TRCENA;
}

static void delay_us(uint32_t us) {
    uint32_t start = DWT_CYCCNT;
    while (DWT_CYCCNT - start < us * CYCLES_PER_US) {}
}

static void port_reset(uint32_t ports) {
    RCC_AHB1RSTR |= ports;
    RCC_AHB1RSTR &= ~ports;
}

static void led_red(void) {                      /* PB4 high */
    RCC_AHB1ENR |= 1U << PB;
    (void)RCC_AHB1ENR;
    GPIO_BSRR(PB) = 1U << 4;
    GPIO_MODER(PB) = (GPIO_MODER(PB) & ~(3U << 8)) | (1U << 8);
}

static __attribute__((noreturn)) void jump(uint32_t vtor, uint32_t sp, uint32_t pc) {
    SCB_VTOR = vtor;
    __asm volatile("dsb\n isb\n msr msp, %0\n bx %1" :: "r"(sp), "r"(pc) : "memory");
    __builtin_unreachable();
}

/* ── 1. Recovery ───────────────────────────────────────────────── */
/* Key 6: keyboard row G (PA6) and column 3 (PC2). The row is pulled
 * low, the column has a pull-up: held, it reads low. */
static bool recovery_key_held(void) {
    RCC_AHB1ENR |= (1U << PA) | (1U << PC);
    (void)RCC_AHB1ENR;
    GPIO_PUPDR(PC) = (GPIO_PUPDR(PC) & ~(3U << 4)) | (1U << 4);
    GPIO_MODER(PC) &= ~(3U << 4);
    GPIO_OTYPER(PA) |= 1U << 6;
    GPIO_BSRR(PA) = 1U << (6 + 16);
    GPIO_MODER(PA) = (GPIO_MODER(PA) & ~(3U << 12)) | (1U << 12);
    int low = 0;
    for (int i = 0; i < 10; i++) {               /* held for the whole ~1 ms */
        delay_us(100);
        if (!(GPIO_IDR(PC) & (1U << 2))) low++;
    }
    port_reset((1U << PA) | (1U << PC));         /* back to the reset state */
    return low == 10;
}

/* ST's bootloader, as if BOOT0 had been high: everything we touched
 * back to the reset state, then its own vector table */
static __attribute__((noreturn)) void recovery(void) {
    timer_stop();
    RCC_AHB3RSTR |= RCC_QSPI;
    RCC_AHB3RSTR &= ~RCC_QSPI;
    RCC_AHB3ENR &= ~RCC_QSPI;
    port_reset(PORTS_A_TO_E);
    led_red();                                   /* the screen stays dark */
    uint32_t sp = REG(SYSMEM), pc = REG(SYSMEM + 4U);
    if (sp > RAM_START && sp <= RAM_END && (pc & 1U) && pc >= SYSMEM && pc < SYSMEM_END)
        jump(SYSMEM, sp, pc);
    for (;;) {}
}

/* ── 2. QUADSPI ────────────────────────────────────────────────── */
static const struct { uint8_t port, pin, af; } QSPI_PINS[] = {
    { PB, 2, 9 },    /* CLK */
    { PB, 6, 10 },   /* nCS */
    { PC, 9, 9 },    /* IO0 */
    { PD, 12, 9 },   /* IO1 */
    { PE, 2, 9 },    /* IO2 */
    { PD, 13, 9 },   /* IO3 */
};

static bool q_wait(uint32_t flag) {
    for (uint32_t n = 0; n < 100000U; n++)
        if (Q_SR & flag) return true;
    return false;
}

static bool q_idle(void) {
    for (uint32_t n = 0; n < 100000U; n++)
        if (!(Q_SR & SR_BUSY)) return true;
    return false;
}

/* One command: instruction on one line, then n data bytes on one line,
 * read into rx or written from tx (no address phase) */
static bool q_cmd(uint8_t instr, uint8_t *rx, const uint8_t *tx, uint32_t n) {
    if (!q_idle()) return false;
    Q_FCR = FCR_ALL;
    if (n) Q_DLR = n - 1U;
    Q_CCR = instr | LQ_IMODE(1) | (n ? LQ_DMODE(1) : 0U) | (rx ? FMODE_IR : FMODE_IW);
    for (uint32_t i = 0; i < n; i++) {           /* the command started with CCR */
        if (!q_wait(SR_FTF | SR_TCF)) return false;
        if (rx) rx[i] = Q_DR8;
        else    Q_DR8 = tx[i];
    }
    if (!q_wait(SR_TCF)) return false;
    Q_FCR = FCR_ALL;
    return q_idle();
}

/* Leave continuous-read mode, in case the OS was reset in the middle of
 * running: a Fast Read Quad I/O without the instruction, with mode bits
 * that aren't Axh. If the flash isn't in that mode it sees instruction
 * 00h (all lines low during address 0 and mode 00h), which it ignores. */
static bool q_leave_continuous(void) {
    if (!q_idle()) return false;
    Q_FCR = FCR_ALL;
    Q_DLR = 0;
    Q_ABR = 0;
    Q_CCR = LQ_ADMODE(3) | LQ_ADSIZE_24 | LQ_ABMODE(3) | LQ_ABSIZE_8 |
            LQ_DCYC(4) | LQ_DMODE(3) | FMODE_IR;
    Q_AR = 0;                                    /* starts it */
    if (!q_wait(SR_FTF | SR_TCF)) return false;
    (void)Q_DR8;
    if (!q_wait(SR_TCF)) return false;
    Q_FCR = FCR_ALL;
    return q_idle();
}

/* Wait for a program or erase (or the reset interrupted one). False if
 * the flash doesn't answer, or is still busy after the timeout. */
static bool q_ready(void) {
    uint32_t ms = 0, last = DWT_CYCCNT;
    for (;;) {
        uint8_t sr1 = 0xFF;
        if (!q_cmd(CMD_RDSR1, &sr1, 0, 1)) return false;
        if (sr1 == 0xFF) return false;           /* nothing driving the line */
        if (!(sr1 & SR1_BUSY)) return true;
        while (DWT_CYCCNT - last >= 1000U * CYCLES_PER_US) {   /* whole ms passed */
            last += 1000U * CYCLES_PER_US;
            if (++ms > LOADER_BUSY_TIMEOUT_MS) return false;
        }
    }
}

/* Returns false if the flash doesn't answer */
static bool qspi_start(void) {
    RCC_AHB1ENR |= PORTS_A_TO_E;
    RCC_AHB3ENR |= RCC_QSPI;
    (void)RCC_AHB3ENR;
    RCC_AHB3RSTR |= RCC_QSPI;
    RCC_AHB3RSTR &= ~RCC_QSPI;
    for (unsigned i = 0; i < sizeof(QSPI_PINS) / sizeof(QSPI_PINS[0]); i++) {
        uint32_t p = QSPI_PINS[i].port, n = QSPI_PINS[i].pin;
        GPIO_AFR(p, n >> 3) = (GPIO_AFR(p, n >> 3) & ~(0xFU << ((n & 7U) * 4U))) |
                              ((uint32_t)QSPI_PINS[i].af << ((n & 7U) * 4U));
        GPIO_OSPEEDR(p) |= 3U << (n * 2U);       /* very high speed */
        GPIO_PUPDR(p) &= ~(3U << (n * 2U));
        GPIO_MODER(p) = (GPIO_MODER(p) & ~(3U << (n * 2U))) | (2U << (n * 2U));
    }
    Q_DCR = LQ_DCR;
    Q_CR = LQ_CR;

    /* A busy flash ignores the release command, but then it can't be
     * in deep power-down either */
    if (!q_leave_continuous()) return false;
    if (!q_cmd(CMD_RELEASE_DPD, 0, 0, 0)) return false;
    delay_us(30);                                /* tRES1 = 3 us */
    if (!q_ready()) return false;
    uint8_t id[3];
    if (!q_cmd(CMD_RDID, id, 0, 3)) return false;
    if (id[0] == 0x00 || id[0] == 0xFF) return false;   /* nothing answering */

    /* Quad Enable: set by NumWorks' firmware, so normally already on.
     * Status register 2 is written back with only QE added. */
    uint8_t sr2 = 0;
    if (!q_cmd(CMD_RDSR2, &sr2, 0, 1)) return false;
    if (!(sr2 & SR2_QE)) {
        uint8_t v = (uint8_t)(sr2 | SR2_QE);
        if (q_cmd(CMD_WREN, 0, 0, 0) && q_cmd(CMD_WRSR2, 0, &v, 1) && q_ready())
            q_cmd(CMD_RDSR2, &sr2, 0, 1);
    }
    bool quad = (sr2 & SR2_QE) != 0;

    /* Memory-mapped mode. ST errata ES0360: after an indirect read,
     * clear AR and abort before switching, or mapped reads can return
     * wrong data. */
    Q_AR = 0;
    Q_CR |= CR_ABORT;
    for (uint32_t n = 0; n < 100000U && (Q_CR & CR_ABORT); n++) {}
    if (!q_idle()) return false;
    Q_ABR = quad ? LQ_ABR_QUAD : LQ_ABR_SINGLE;
    Q_CCR = quad ? LQ_CCR_QUAD : LQ_CCR_SINGLE;
    return true;
}

/* ── 4. The OS ─────────────────────────────────────────────────── */
void Reset_Handler(void) {
    timer_start();
    if (recovery_key_held()) recovery();
    if (!qspi_start()) recovery();
    const volatile uint32_t *vt = (const volatile uint32_t *)QSPI_MAP;
    uint32_t sp = vt[0], pc = vt[1];
    if (!(sp > RAM_START && sp <= RAM_END && !(sp & 3U) &&
          (pc & 1U) && pc > QSPI_MAP && pc < OS_CODE_END))
        recovery();                              /* nothing that looks like a program */
    timer_stop();
    jump(QSPI_MAP, sp, pc);
}

/* ── Vectors ───────────────────────────────────────────────────── */
static void Fault_Handler(void) {
    led_red();
    for (;;) {}
}

extern uint32_t _estack[];
__attribute__((section(".vectors"), used))
static void (*const VECTORS[16])(void) = {
    (void (*)(void))_estack,
    Reset_Handler,
    Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler,  /* NMI .. UsageFault */
    0, 0, 0, 0,
    Fault_Handler, Fault_Handler, 0, Fault_Handler, Fault_Handler,             /* SVC .. SysTick */
};
