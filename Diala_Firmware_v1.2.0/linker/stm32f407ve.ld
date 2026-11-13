/* STM32F407VET6: 512 KB flash, 128 KB SRAM (SRAM1 + SRAM2). CCM RAM unused. */
ENTRY(Reset_Handler)
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x2000;

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
  .text : { . = ALIGN(4); *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  /* v1.2: last word of the image = CRC-32 of everything before it, written by tools/patch_crc.py */
  .fw_crc : { . = ALIGN(4); _fw_crc = .; LONG(0xFFFFFFFF) } > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  ._stack_check (NOLOAD) : { . = ALIGN(8); . = . + _min_stack; } > RAM
}
