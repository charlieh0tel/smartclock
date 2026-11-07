/* The boot code's routines (reset at 0x550, its own program ahead of the
 * primary) that are this image's own: the rest are common/boot.c's
 * (docs/firmware/rewrite.md, "Shared C"), and boot_duart_early_init is
 * left as the decompiler's.  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `platform/boot`); each keeps its
 * generated prototype. */

/* Copies the installer's records, installer_records' 43243 bytes at
 * 0x8000, into RAM (0x8f2). */
uint32_t boot_unpack_installer(void) {
  return boot_unpack(installer_records, sizeof installer_records, false);
}

/* The primary's copy of boot_unpack_installer, with memcpy, pointed at
 * ram_routine_records, whose records, unlike the other images', it
 * unpacks: flash_ram_program_word and flash_ram_erase_block, which the
 * flash test runs (0x5ec34). */
uint32_t primary_unpack_records(void) {
  return boot_unpack(ram_routine_records, sizeof ram_routine_records, true);
}
