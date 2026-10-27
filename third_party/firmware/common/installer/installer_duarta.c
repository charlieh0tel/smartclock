/* The functions of installer/installer_duarta.c the images share, the same in
 * each (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/installer/installer_duarta.c describes the module and holds the
 * rest.  Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Channel A's transmit interrupt: turns it off, the installer sending
 * channel A nothing. */
void installer_duarta_transmit_interrupt(void) {
  installer_duart_shadow.imr &= ~IMR_TXRDYA;
  duart.isr_imr = installer_duart_shadow.imr;
}
