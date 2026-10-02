/* NumWorks OS — internal-flash loader: first 16 KB sector of the
 * STM32F730's internal flash. No .data or .bss (no startup code). */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K   /* DTCM */
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata*)
  } > FLASH
  .data : { *(.data*) } > RAM AT > FLASH
  .bss  : { *(.bss*) *(COMMON) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "loader: no .data/.bss allowed (no startup code)")
ASSERT(SIZEOF(.text) <= 16K, "loader: must fit in the first 16 KB sector")
