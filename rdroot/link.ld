OUTPUT_FORMAT("binary")    /* output flat binary */
SECTIONS
{
    . = 0;                 /* relocate to address 0 */
    .text ALIGN(4):
    {
        *(.text)           /* include all other .text sections */
    }
    .data ALIGN(4):
    {
        *(.data)
    }
    .rodata ALIGN(4):
    {
        *(.rodata*)
    }
}
