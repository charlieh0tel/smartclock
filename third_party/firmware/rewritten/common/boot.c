/* The functions of boot.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/boot.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Copies so many bytes; returns the destination (0xb9e). */
uint8_t* boot_memcpy(uint8_t* to, uint8_t* from, int32_t count) {
  uint8_t* cursor = to;

  while (count-- != 0) {
    *cursor++ = *from++;
  }
  return to;
}
