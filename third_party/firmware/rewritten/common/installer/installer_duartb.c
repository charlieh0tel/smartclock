/* The functions of installer/installer_duartb.c the images share, the same in
 * each (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/installer/installer_duartb.c describes the module and holds the
 * rest, installer_console_write_polled among them, which polls the SCI
 * in the Z3815A 4010 (its installer/installer_sci.c).  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* The done hook of channel B's setting descriptors: reconfigures device
 * 0. */
void installer_duartb_settings_done(void) {
  installer_psos_io_control(IO_DEVICE_DUART_B, 0, 0);
}
