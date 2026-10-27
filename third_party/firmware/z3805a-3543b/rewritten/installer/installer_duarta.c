/* The installer's copies of DUART channel A's driver (`devices/duarta`;
 * duarta.c has the primary's), which keep the installer's driver records and
 * call its routines. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Empties channel A's receiver into its ring, noting the ring's least
 * room and an overrun; unlike duarta_isr it signals no task, the
 * installer having no GPS task (installer::0x103a44). */
void installer_duarta_receive(void) {
  uint8_t free_bytes;

  while ((duart.a.sr_csr & SR_RXRDY) != 0) {
    installer_duarta_ring.data[installer_duarta_ring.write++] = duart.a.rb_tb;
  }
  free_bytes = installer_duarta_ring.read - installer_duarta_ring.write;
  if (free_bytes < installer_duarta.least_free) {
    installer_duarta.least_free = free_bytes;
  }
  if ((duart.a.sr_csr & SR_OVERRUN) != 0) {
    installer_duarta.overrun = 1;
    duart.a.cr = CR_RESET_ERROR;
  }
}
