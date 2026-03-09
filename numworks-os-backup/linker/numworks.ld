/* ================================================================
 * NumWorks OS — Linker Script
 * STM32F730R8T6: 512K Flash, 256K RAM, 64K DTCM
 * Flash layout:
 *   0x0800_0000 – 0x0800_FFFF  (64K)  Bootloader + vector table
 *   0x0801_0000 – 0x0806_FFFF  (384K) Kernel + apps
 *   0x0807_0000 – 0x0807_FFFF  (64K)  Internal Flash Filesystem
 * RAM layout:
 *   0x2000_0000 – 0x2003_DFFF  (248K) Main RAM
 *   0x2003_E000 – 0x2003_FFFF  (8K)   Heap
 *   0x2004_0000 – 0x2004_FFFF  (64K)  DTCM (stack)
 * ================================================================ */

MEMORY
{
  FLASH_BOOT (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  FLASH_KERN (rx) : ORIGIN = 0x08010000, LENGTH = 384K
  FLASH_FS   (rw) : ORIGIN = 0x08070000, LENGTH = 64K
  RAM        (rwx): ORIGIN = 0x20000000, LENGTH = 248K
  HEAP_RAM   (rw) : ORIGIN = 0x2003E000, LENGTH = 8K
  DTCM       (rwx): ORIGIN = 0x20040000, LENGTH = 64K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >FLASH_BOOT
  .boot_text  : { *bootloader/*.o(.text* .rodata*) } >FLASH_BOOT

  .text :
  {
    *(.text*) *(.rodata*)
    *(.glue_7 .glue_7t)
    KEEP(*(.init .fini))
  } >FLASH_KERN

  .ARM.extab : { *(.ARM.extab*) } >FLASH_KERN
  .ARM.exidx : { *(.ARM.exidx*) } >FLASH_KERN

  _sidata = LOADADDR(.data);
  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } >RAM AT>FLASH_KERN

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss* COMMON)
    _ebss = .;
  } >RAM

  .heap (NOLOAD) :
  {
    _sheap = .;
    . = ORIGIN(HEAP_RAM) + LENGTH(HEAP_RAM);
    _eheap = .;
  } >HEAP_RAM

  .stack (NOLOAD) :
  {
    _sstack = .;
    . = ORIGIN(DTCM) + LENGTH(DTCM) - 4;
    _estack = .;
  } >DTCM

  _flash_fs_start = ORIGIN(FLASH_FS);
  _flash_fs_end   = ORIGIN(FLASH_FS) + LENGTH(FLASH_FS);

  /DISCARD/ : { *(.eh_frame) *(.note*) }
}
