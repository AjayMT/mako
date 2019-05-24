
ENTRY(loader)

SECTIONS {
    . = 0xC0100000;
    kernel_start = .;
    .multiboot : AT(ADDR(.multiboot) - 0xC0000000)
    {
        *(.multiboot)
    }
    .text : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }
    .rodata ALIGN (0x1000) : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata*)
    }
    .data ALIGN (0x1000) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }
    .bss : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
    }
    kernel_end = .;
}