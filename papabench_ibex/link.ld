/* PapaBench on Ibex linker script.

   Same layout as Secure-Ibex sw/Standard_Only_SW/Standard_Tests/common/link.ld
   (vectors at the start of RAM, entry at +0x80, SimCtrl tohost), but sized for
   the whole 128 KiB RAM of the SoC (reference_system_core.sv MEM_SIZE) instead
   of the stock 56 KiB + 8 KiB: code and data 112 KiB, stack 16 KiB. The
   Autopilot plus the peripheral models does not fit the stock layout. */

OUTPUT_ARCH(riscv)

MEMORY
{
    ram         : ORIGIN = 0x00100000, LENGTH = 0x1C000 /* 112 KiB */
    stack       : ORIGIN = 0x0011C000, LENGTH = 0x4000  /* 16 KiB */
}

_min_stack      = 0x1000;   /* 4K - minimum stack space to reserve */
_stack_len     = LENGTH(stack);
_stack_start   = ORIGIN(stack) + LENGTH(stack);

_entry_point = _vectors_start + 0x80;
ENTRY(_entry_point)

/* SIM_CTRL_CTRL: a write ends the simulation (see the stock link.ld) */
tohost   = 0x20008;
fromhost = _stack_start + 0x10;

SECTIONS
{
    .vectors :
    {
        . = ALIGN(4);
        _vectors_start = .;
        KEEP(*(.vectors))
        _vectors_end = .;
    } > ram

    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
    }  > ram

    .rodata : {
        . = ALIGN(4);
        *(.srodata)
        *(.srodata.*)
        *(.rodata);
        *(.rodata.*)
    } > ram

    .data : {
        . = ALIGN(4);
        *(.sdata)
        *(.sdata.*)
        *(.data);
        *(.data.*)
    } > ram

    .bss :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > ram

    .stack (NOLOAD): {
        . = ALIGN(4);
        . = . + _min_stack ;
        . = ALIGN(4);
        stack = . ;
        _stack = . ;
    } > stack
}
