/* installer_duarta_transmit_interrupt: this image's own, split from
 * common/installer/installer_duarta.c where it differs between the images
 * (rewrite.md, "Shared C", rule 6). */

/* Channel A's transmit interrupt: turns it off, the installer sending
 * channel A nothing. */
void installer_duarta_transmit_interrupt(void) {
  installer_duart_shadow.imr &= ~IMR_TXRDYA;
  duart.isr_imr = installer_duart_shadow.imr;
}
