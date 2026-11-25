/*
 * an505.ld -- linker script for QEMU's mps2-an505 machine (Cortex-M33).
 *
 * Everything (code, data, heap, stack) lives in SSRAM1, which QEMU maps
 * at 0x10000000 (secure alias), 4 MB. The CPU starts in secure state and
 * QEMU sets the secure vector table base (VTOR_S) to 0x10000000, so the
 * vector table goes first.
 *
 * This is a test bench, not the STM32 memory map (the STM32H563 has 2 MB
 * flash at 0x08000000 and 640 KB SRAM at 0x20000000). Instruction counts
 * do not depend on where the code is placed.
 */
ENTRY(Reset_Handler)

MEMORY
{
    SSRAM1 (rwx) : ORIGIN = 0x10000000, LENGTH = 4M
}

_estack = ORIGIN(SSRAM1) + LENGTH(SSRAM1);
_stack_size = 64K;

SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > SSRAM1

    .text : {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > SSRAM1

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > SSRAM1

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SSRAM1
    _sidata = LOADADDR(.data);      /* same as _sdata: we run from RAM */

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SSRAM1

    /* newlib's heap (used only by printf in this test bench, never by the
     * VM or the hooks) grows up from `end` towards the stack. */
    . = ALIGN(8);
    end = .;
    _end = .;

    ASSERT(end + _stack_size <= _estack, "SSRAM1 overflow")
}
