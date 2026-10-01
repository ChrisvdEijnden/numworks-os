/* ================================================================
 * NumWorks OS — Linker Script (QSPI execute-in-place build)
 *
 * Target: STM32F730V8T6, code runs from QSPI XIP at 0x90000000
 * Internal flash is left to the bootloader — we don't link into it.
 *
 * STM32F730 RAM is 256 KB in total, contiguous from 0x20000000:
 *   DTCM   0x20000000   64 KB
 *   SRAM1  0x20010000  176 KB
 *   SRAM2  0x2003C000   16 KB
 * There is no RAM at or above 0x20040000.
 *
 * DTCM layout:
 *   0x20000000  .stack      16 KB (grows down from 0x20004000)
 *   0x20004000  .mp_heap    48 KB
 *   The stack sits at the bottom of DTCM so an overflow runs into
 *   unmapped memory below 0x20000000 and faults, instead of silently
 *   corrupting the MicroPython heap.
 *
 * SRAM1+SRAM2 layout:
 *   0x20010000  .framebuf   153,600 B  (320*240*2)
 *   then        .data, .bss
 *   then        newlib _sbrk heap up to 0x20040000
 * ================================================================ */

MEMORY
{
  QSPI  (rx)  : ORIGIN = 0x90000000, LENGTH = 8M
  DTCM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
  RAM   (rwx) : ORIGIN = 0x20010000, LENGTH = 192K
}

ENTRY(Reset_Handler)

_stack_size   = 16K;
_mp_heap_size = 48K;

SECTIONS
{
  /* ── QSPI: vector table ──────────────────────────────────────── */
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >QSPI

  /* ── QSPI: code + read-only data ─────────────────────────────── */
  .text :
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7 .glue_7t)
    KEEP(*(.init .fini))
  } >QSPI

  .ARM.extab : { *(.ARM.extab*) } >QSPI
  .ARM.exidx : { *(.ARM.exidx*) } >QSPI

  /* ── DTCM: stack (grows down to 0x20000000) ──────────────────── */
  .stack (NOLOAD) :
  {
    _sstack = .;
    . = . + _stack_size;
    . = ALIGN(8);
    _estack = .;            /* initial SP — AAPCS needs 8-byte alignment */
  } >DTCM

  /* ── DTCM: MicroPython heap ──────────────────────────────────── */
  .mp_heap (NOLOAD) :
  {
    _smp_heap = .;
    KEEP(*(.mp_heap*))
    . = MAX(., _smp_heap + _mp_heap_size);
    _emp_heap = .;
  } >DTCM

  /* ── RAM: framebuffer pinned at base ─────────────────────────── */
  .framebuf (NOLOAD) :
  {
    _s_framebuf = .;
    *(.framebuf*)
    . = MAX(., _s_framebuf + 153600);
    _e_framebuf = .;
  } >RAM

  /* ── RAM: initialised data (copied from QSPI at startup) ──────── */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>QSPI
  _sidata = LOADADDR(.data);

  /* ── RAM: zero-init ──────────────────────────────────────────── */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss* COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* newlib _sbrk uses leftover RAM */
  _sheap = ALIGN(_ebss, 8);
  _eheap = ORIGIN(RAM) + LENGTH(RAM);

  ASSERT(_estack - _sstack >= 8K, "stack is smaller than 8 KB")
  ASSERT(_eheap - _sheap >= 4K, "less than 4 KB left for the newlib heap")

  /DISCARD/ : { *(.eh_frame) *(.note*) }
}
