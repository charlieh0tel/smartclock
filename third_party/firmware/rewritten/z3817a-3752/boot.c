/* From the Z3815A 4010's rewritten/boot.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image.
 * Each keeps its generated prototype. */

/* Copies the installer's records, installer_records' 43243 bytes at
 * 0x8000, into RAM (0x8f2). */
uint32_t boot_unpack_installer(void) {
  return boot_unpack(installer_records, sizeof installer_records, false);
}

/* The primary's copy of boot_unpack_installer, with memcpy, pointed at
 * ram_routine_records, whose records, as the Z3815A's and unlike the
 * 58503A's and the Z3805A's, it unpacks: flash_ram_program_word and
 * flash_ram_erase_block, which the flash test runs (0x58cf8). */
uint32_t primary_unpack_records(void) {
  return boot_unpack(ram_routine_records, sizeof ram_routine_records, true);
}
