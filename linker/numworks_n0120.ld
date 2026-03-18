/* ================================================================
 * NumWorks OS — Linker Script for N0120 (QSPI boot)
 *
 * Target: STM32F730V8T6 in rescue mode
 * Code runs from QSPI XIP at 0x90000000 (8MB external flash)
 * Internal flash is locked — we don't touch it.
 *
 * Memory layout:
 *   QSPI  0x90000000  8MB   code + rodata (XIP)
 *   RAM   0x20000000  240K  framebuffer + data + bss
 *   DTCM  0x20040000  64K   mp_heap + stack
 *
 * RAM layout:
 *   0x20000000  .framebuf   153,600 B  (320*240*2)
 *   0x20025800  .data       ~3 KB
 *   0x20026600  .bss        ~22 KB
 *   0x2002B800  free        ~83 KB
 *
 * DTCM layout:
 *   0x20040000  .mp_heap    48 KB
 *   0x2004C000  .stack      16 KB (grows down from 0x2004FFFC)
 * ================================================================ */

MEMORY
{
  QSPI  (rx)  : ORIGIN = 0x90000000, LENGTH = 8M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 240K
  DTCM  (rwx) : ORIGIN = 0x20040000, LENGTH = 64K
}

ENTRY(Reset_Handler)

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

  /* ── RAM: framebuffer pinned at base ─────────────────────────── */
  .framebuf (NOLOAD) :
  {
    _s_framebuf = .;
    *(.framebuf*)
    . = _s_framebuf + 153600;
    _e_framebuf = .;
  } >RAM

  /* ── RAM: initialised data (copied from QSPI at startup) ──────── */
  _sidata = LOADADDR(.data);
  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } >RAM AT>QSPI

  /* ── RAM: zero-init ──────────────────────────────────────────── */
  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss* COMMON)
    _ebss = .;
  } >RAM

  /* ── DTCM: MicroPython heap ──────────────────────────────────── */
  .mp_heap (NOLOAD) :
  {
    _smp_heap = .;
    . = _smp_heap + 48K;
    _emp_heap = .;
  } >DTCM

  /* ── DTCM: stack (grows down from top) ───────────────────────── */
  .stack (NOLOAD) :
  {
    _sstack = .;
    . = ORIGIN(DTCM) + LENGTH(DTCM) - 4;
    _estack = .;
  } >DTCM

  /* newlib _sbrk uses leftover RAM */
  _sheap = _ebss;
  _eheap = ORIGIN(RAM) + LENGTH(RAM);

  /DISCARD/ : { *(.eh_frame) *(.note*) }
}
