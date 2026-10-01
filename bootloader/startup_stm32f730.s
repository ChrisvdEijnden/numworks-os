/* ================================================================
 * NumWorks OS — ARM Cortex-M7 Startup / Vector Table
 * File: bootloader/startup_stm32f730.s
 * ================================================================ */
    .syntax unified
    .cpu cortex-m7
    .thumb

/* IRQ numbers (STM32F7 reference manual, vector table) */
    .equ    IRQ_TIM6_DAC, 54
    .equ    IRQ_OTG_FS,   67
    .equ    IRQ_USART6,   71
    .equ    IRQ_SLOTS,    240       /* covers every STM32F7 IRQ */

    .section .isr_vector,"a",%progbits
    .type   g_vectors, %object
    .global g_vectors
g_vectors:
    .word   _estack                 /* 00: Initial stack pointer    */
    .word   Reset_Handler           /* 01: Reset                    */
    .word   NMI_Handler             /* 02: NMI                      */
    .word   HardFault_Handler       /* 03: Hard fault               */
    .word   MemManage_Handler       /* 04: MPU fault                */
    .word   BusFault_Handler        /* 05: Bus fault                */
    .word   UsageFault_Handler      /* 06: Usage fault              */
    .word   0                       /* 07-10: Reserved              */
    .word   0
    .word   0
    .word   0
    .word   SVC_Handler             /* 11: SVCall                   */
    .word   DebugMon_Handler        /* 12: Debug monitor            */
    .word   0                       /* 13: Reserved                 */
    .word   PendSV_Handler          /* 14: PendSV                   */
    .word   SysTick_Handler         /* 15: SysTick                  */

    /* External interrupts — entry N is IRQ N. Handlers we implement
     * are named here so the linker keeps them; the rest are unused. */
    .rept   IRQ_TIM6_DAC
    .word   Default_Handler
    .endr
    .word   TIM6_DAC_IRQHandler                 /* IRQ 54 */
    .rept   IRQ_OTG_FS - IRQ_TIM6_DAC - 1
    .word   Default_Handler
    .endr
    .word   OTG_FS_IRQHandler                   /* IRQ 67 */
    .rept   IRQ_USART6 - IRQ_OTG_FS - 1
    .word   Default_Handler
    .endr
    .word   USART6_IRQHandler                   /* IRQ 71 */
    .rept   IRQ_SLOTS - IRQ_USART6 - 1
    .word   Default_Handler
    .endr
    .size   g_vectors, . - g_vectors

/* ── Reset handler ──────────────────────────────────────────── */
    .text
    .thumb_func
    .global Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:
    /* We are normally started by a bootloader: keep interrupts masked
     * until our vector table, .data and .bss are in place. */
    cpsid   i

    /* Set MSP to the top of the DTCM stack */
    ldr     r0, =_estack
    msr     msp, r0

    /* Point VTOR at our own vector table (SCB->VTOR) */
    ldr     r0, =0xE000ED08
    ldr     r1, =g_vectors
    str     r1, [r0]
    dsb
    isb

    /* Disable and un-pend every IRQ the bootloader may have left on;
     * otherwise they would fire into Default_Handler. */
    ldr     r0, =0xE000E180         /* NVIC->ICER[0] */
    ldr     r1, =0xE000E280         /* NVIC->ICPR[0] */
    mov     r2, #-1
    mov     r3, #8
clear_nvic_loop:
    str     r2, [r0], #4
    str     r2, [r1], #4
    subs    r3, r3, #1
    bne     clear_nvic_loop
    dsb
    isb

    /* Copy .data from flash to RAM */
    ldr     r0, =_sidata
    ldr     r1, =_sdata
    ldr     r2, =_edata
    b       copy_data_chk
copy_data_loop:
    ldr     r3, [r0], #4
    str     r3, [r1], #4
copy_data_chk:
    cmp     r1, r2
    blo     copy_data_loop

    /* Zero .bss */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
    b       zero_bss_chk
zero_bss_loop:
    str     r2, [r0], #4
zero_bss_chk:
    cmp     r0, r1
    blo     zero_bss_loop

    /* Enable FPU (CPACR — full access CP10/CP11) */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    /* Runtime state is ready: interrupts may now be taken */
    cpsie   i

    /* Jump to C bootloader */
    bl      boot_main
    b       .
    .size   Reset_Handler, . - Reset_Handler

/* ── Default / fault handlers ───────────────────────────────────
 * Every vector must have its Thumb bit set, so handlers are declared
 * with .thumb_func / .thumb_set rather than as bare labels.
 *
 * Faults and unexpected interrupts go to fault_report(frame, ipsr) in
 * hal/fault.c, which shows a crash screen and never returns. If the
 * stack is (nearly) used up it gets a fresh one at the top first; the
 * stacked frame is then far below it, so it isn't overwritten.     */
    .thumb_func
    .global Default_Handler
    .type   Default_Handler, %function
Default_Handler:
    tst     lr, #4                  /* EXC_RETURN: which stack was in use */
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mrs     r1, ipsr
    ldr     r2, =_sstack + 1024
    cmp     r0, r2
    bhs     1f
    ldr     r2, =_estack
    msr     msp, r2
1:  b       fault_report
    .size   Default_Handler, . - Default_Handler

    .thumb_func
    .type   Ignore_Handler, %function
Ignore_Handler:
    bx      lr
    .size   Ignore_Handler, . - Ignore_Handler

    .macro  weak_alias name, target
    .weak   \name
    .thumb_set \name, \target
    .endm

    weak_alias HardFault_Handler,   Default_Handler
    weak_alias MemManage_Handler,   Default_Handler
    weak_alias BusFault_Handler,    Default_Handler
    weak_alias UsageFault_Handler,  Default_Handler
    weak_alias NMI_Handler,         Ignore_Handler
    weak_alias SVC_Handler,         Ignore_Handler
    weak_alias DebugMon_Handler,    Ignore_Handler
    weak_alias PendSV_Handler,      Ignore_Handler
    weak_alias SysTick_Handler,     Ignore_Handler
    weak_alias TIM6_DAC_IRQHandler, Default_Handler
    weak_alias OTG_FS_IRQHandler,   Default_Handler
    weak_alias USART6_IRQHandler,   Default_Handler

    .end
