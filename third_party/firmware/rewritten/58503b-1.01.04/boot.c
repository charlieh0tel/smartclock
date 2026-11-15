/* From the Z3815A 4010's rewritten/boot.c, the 58503B 1.01.04's own beside
 * common/'s: of its functions, those the 58503B's code carries identical or
 * relocated (docs/firmware/58503b-1.01.04.md, "The link list").  Its
 * comments' addresses are the 58503B's, but where they name another image.
 * Each keeps its generated prototype. */

/* Copies the installer's records, installer_records' 43243 bytes at
 * 0x8000, into RAM (0x8f2). */
uint32_t boot_unpack_installer(void) {
  return boot_unpack(installer_records, sizeof installer_records, false);
}

/* The primary's copy of boot_unpack_installer, with memcpy, pointed at
 * ram_routine_records, whose records, unlike the other images', it
 * unpacks: flash_ram_program_word and flash_ram_erase_block, which the
 * flash test runs (0x5b6b0). */
uint32_t primary_unpack_records(void) {
  return boot_unpack(ram_routine_records, sizeof ram_routine_records, true);
}
