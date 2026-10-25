/* The installer's copies of DUART channel A's driver (`devices/duarta`;
 * duarta.c has the primary's), which keep the installer's driver records and
 * call its routines. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Channel A's transmit interrupt: turns it off, the installer sending
 * channel A nothing (installer::0x103c02). */
void installer_duarta_transmit_interrupt(void) {
  installer_duart_shadow.imr &= ~IMR_TXRDYA;
  duart.isr_imr = installer_duart_shadow.imr;
}
