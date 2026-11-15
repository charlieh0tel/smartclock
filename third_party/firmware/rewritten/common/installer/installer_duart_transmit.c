/* The installer's DUART as it sends, the same in each image that includes
 * this file (docs/firmware/rewrite.md, "Shared C", "A file for some
 * images"): channel A's transmit interrupt, which it turns off, and
 * channel B's polled write.  An image whose installer speaks through the
 * SCI leaves the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Channel A's transmit interrupt: turns it off, the installer sending
 * channel A nothing. */
void installer_duarta_transmit_interrupt(void) {
  installer_duart_shadow.imr &= ~IMR_TXRDYA;
  duart.isr_imr = installer_duart_shadow.imr;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on. */
uint8_t installer_duartb_putc_polled(uint8_t c) {
  return duart_b_putc_polled(c);
}
