/*
 * Linker script for STM32F407G-DISC1 (STM32F407VGTx MCU).
 *
 * Memory layout:
 *   FLASH  : 1024 KB @ 0x08000000  (program code, read-only)
 *   RAM    :  128 KB @ 0x20000000  (SRAM1 + SRAM2 contiguous)
 *   CCMRAM :   64 KB @ 0x10000000  (tightly-coupled, CPU-only, not DMA-accessible)
 *
 * Stack grows downward from end of RAM. Heap is zero-size (ARCH-02: no dynamic alloc).
 */

ENTRY(Reset_Handler)

/* End-of-RAM = initial stack pointer (MSP). Stack grows downward from here. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Min stack/heap sizes — loader checks at link time that these fit in RAM. */
_Min_Heap_Size  = 0x0;     /* 0: dynamic alloc disabled (ARCH-02). */
_Min_Stack_Size = 0x1000;  /* 4 KB main stack. */

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
    /* 1. Interrupt Vector Table — must sit at the very start of Flash. */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* 2. Program code and read-only data. */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)          /* ARM-Thumb interworking glue */
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

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /* 3. C++ static constructors / destructors. Startup code walks these tables. */
    .preinit_array :
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH

    /* 4. Initialized data — lives in RAM at runtime, initial values copied from Flash by Reset_Handler. */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    /* 5. Zero-initialized data — Reset_Handler zeroes this region. */
    . = ALIGN(4);
    .bss :
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* 6. Panic block — NOT zeroed by Reset_Handler (SAF-06 post-mortem across resets). */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } >RAM

    /* 7. User heap + stack check. If these don't fit in remaining RAM, link fails. */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /* 8. Drop compiler-generated sections we don't use. */
    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
