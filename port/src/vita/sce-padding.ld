/* vita-elf-create appends module/import metadata to the executable segment.
 * Leave address space before the constructor/data segment even when the code
 * ends close to its next alignment boundary. This creates no output section.
 */
SECTIONS
{
    . = . + 0x2000;
}
INSERT BEFORE .init_array;
