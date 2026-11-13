/* From the Z3815A 4010's rewritten/installer/installer_leds.c, the Z3817A
 * 3752's own beside common/'s: of its functions, those the Z3817A's code
 * carries identical or relocated (docs/firmware/z3817a-3752.md, "The
 * function check").  Its comments' addresses are the Z3817A's, but where
 * they name another image.  Each keeps its generated prototype. */

/* Sets the latch's bits given, or clears them, and writes it
 * (installer::0x100fbe). */
void installer_latch_bits_set(uint16_t bits, char set) {
  if (set != 0) {
    boot_selftest.latch |= bits;
  } else {
    boot_selftest.latch &= ~bits;
  }
  output_latch = boot_selftest.latch;
}

/* Clears the latch's bits 6 and 7 that clear gives and sets those set
 * gives, the rest kept, and writes it (installer::0x100fe4); the root
 * task clears bit 6 with it (installer::0x100d90). */
void installer_latch_bits_write(uint8_t clear, uint8_t set) {
  static const uint16_t BITS = 0xc0;

  boot_selftest.latch = (boot_selftest.latch & ~(clear & BITS)) | (set & BITS);
  output_latch = boot_selftest.latch;
}
