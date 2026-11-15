/* boot.c's functions that are this image's own: the rest are common/boot.c's,
 * the same in every image (docs/firmware/rewrite.md, "Shared C").  From the
 * 58503A 3704's rewritten/boot.c: of its functions, those the Z3816A's code
 * carries (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Copies the installer's records, installer_records' 0xa811 bytes at
 * 0xbc4, into RAM (0xaa8). */
uint32_t boot_unpack_installer(void) {
  return boot_unpack(installer_records, sizeof installer_records, false);
}
/* The primary's copy of boot_unpack_installer, with memcpy, pointed at
 * 11 bytes at address 0, boot_vectors, whose first byte, the initial
 * stack pointer's high one, is no S: it always returns
 * BOOT_UNPACK_NO_START. */
uint32_t primary_unpack_records(void) {
  static const int32_t RECORDS_SIZE = 0xb;

  return boot_unpack((const uint8_t*)&boot_vectors, RECORDS_SIZE, true);
}
