/* src/platform/stm32f1/stm32f103c8tx.ld */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Define the top of stack pointer based on RAM end */
_estack = ORIGIN(RAM) + LENGTH(RAM);

_Min_Heap_Size = 0x200;      /* Required amount of heap for malloc if used */
_Min_Stack_Size = 0x400;     /* Required amount of stack space */

SECTIONS
{
    /* Interrupt Vector Table must be at the very beginning of Flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code and Read-Only Data */
    .text :
    {
        . = ALIGN(4);
        *(.text)           /* All code functions */
        *(.text*)          /* Any other text sections */
        *(.rodata)         /* Constant data */
        *(.rodata*)        /* Other constant data */
        
        /* Ensure alignment for LDR instructions or specific HW requirements */
        . = ALIGN(4);
    } > FLASH

    /* Initialized Global/Static Variables (.data section) */
    /* These are stored in Flash but copied to RAM at startup by Reset_Handler */
    _sidata = LOADADDR(.data);
    
    .data :
    {
        . = ALIGN(4);
        _sdata = .;        /* Start address in RAM */
        *(.data)           /* Initialized variables */
        *(.data*)          /* Other initialized variables */
        . = ALIGN(4);
        _edata = .;        /* End address in RAM */
    } > RAM AT > FLASH

    /* Uninitialized Global/Static Variables (.bss section) */
    /* Zero-initialized at startup by Reset_Handler */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;         /* Start address in RAM */
        *(.bss)            /* Uninitialized variables */
        *(.bss*)           /* Other uninitialized variables */
        *(COMMON)          /* Common symbols */
        . = ALIGN(4);
        _ebss = .;         /* End address in RAM */
    } > RAM

    /* Heap and Stack reservation */
    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM
    
    /* Discard unused debug sections to save space if needed later */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }
}