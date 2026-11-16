/* The Z3805A 3543B's installer/installer_duarta.c, the Z3805A 1.01.03's
 * own beside common/'s, whose installer is the 3543B's byte for byte
 * (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of its
 * functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_duarta.c's.  Its comments'
 * installer addresses are the 3543B's, which are this image's; a primary
 * address is moved to this image's.  Each keeps its generated prototype. */

/* The functions of installer/installer_duarta.c the images share, the same in
 * each (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/installer/installer_duarta.c describes the module and holds the
 * rest.  Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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
