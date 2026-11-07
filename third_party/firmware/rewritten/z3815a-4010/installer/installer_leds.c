/* The installer's output latch writers, on the latch's shadow in
 * boot_selftest.latch, as the primary's leds.c's are on selftest.latch,
 * without its test mode.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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
