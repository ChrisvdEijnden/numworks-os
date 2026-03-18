/* ================================================================
 * NumWorks OS — ARM Cortex-M7 Startup / Vector Table
 * File: bootloader/startup_stm32f730.s
 * ================================================================ */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .section .isr_vector,"a",%progbits
    .type g_vectors, %object
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
    /* External interrupts — only ones we use */
    .rept   256
    .word   Default_Handler
    .endr

/* ── Reset handler ──────────────────────────────────────────── */
    .text
    .thumb_func
    .global Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:
    /* Set MSP to top of DTCM stack */
    ldr     r0, =_estack
    msr     msp, r0

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
    blt     copy_data_loop

    /* Zero .bss */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
    b       zero_bss_chk
zero_bss_loop:
    str     r2, [r0], #4
zero_bss_chk:
    cmp     r0, r1
    blt     zero_bss_loop

    /* Enable FPU (CPACR — full access CP10/CP11) */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    /* Jump to C bootloader */
    bl      boot_main
    b       .

/* ── Default / fault handlers ───────────────────────────────── */
    .thumb_func
    .global Default_Handler
Default_Handler:
HardFault_Handler:
MemManage_Handler:
BusFault_Handler:
UsageFault_Handler:
    b       .   /* Spin — attach debugger to read fault registers */

    .thumb_func
NMI_Handler:
SVC_Handler:
DebugMon_Handler:
PendSV_Handler:
    bx      lr

    .weak   SysTick_Handler
    .thumb_func
SysTick_Handler:
    bx      lr

    .end
