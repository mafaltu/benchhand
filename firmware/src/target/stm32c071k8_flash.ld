/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* Linker script for STM32C071K8 (64 KB flash, 24 KB SRAM), USB-DEBUG-CTRL r1 firmware.
 * Hand-written for this project (Task 9); symbol names follow startup_stm32c071xx.s.
 *
 * RAM layout (low -> high):
 *   .noinit  NOLOAD, at ORIGIN(RAM) so its address is stable across builds (boot counter, bootloader record);
 *            never zeroed or copied by the startup code.
 *   .data    initialised from flash by Reset_Handler.
 *   .bss     zeroed by Reset_Handler.
 *   (gap)    must hold at least _Min_Stack_Size; the stack grows down from _estack = end of RAM.
 * No heap: the firmware does not use malloc. */

ENTRY(Reset_Handler)

_Min_Stack_Size = 0x800; /* 2 KB, checked against the RAM end below (m-5) */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM   (rw)  : ORIGIN = 0x20000000, LENGTH = 24K /* no code runs from RAM */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM (READONLY) :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array (READONLY) : /* READONLY: keeps the flash segment R-X (binutils >= 2.38) */
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* No-init RAM: survives a system reset (not a power cycle); contents are validated by magic words. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    _snoinit = .;
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
    _enoinit = .;
  } >RAM

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* newlib-nano/nosys sbrk symbol; no heap is reserved (malloc is not used). */
  PROVIDE(end = _ebss);
  PROVIDE(_end = _ebss);

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

/* .bss is the last RAM section (.noinit sits below .data), so _ebss bounds all static RAM (M11, m-5). */
ASSERT(_enoinit <= _sdata, "RAM layout: .noinit must sit below .data")
ASSERT(_ebss + _Min_Stack_Size <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow: .noinit + .data + .bss + 2 KB stack > 24 KB")
