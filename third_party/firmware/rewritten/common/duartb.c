/* The functions of duartb.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/duartb.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The loopback test's transmit on channel B. */
void duart_test_tx_b(void) { duart_test_transmit(&duart.b, IMR_TXRDYB); }

/* A no-op echo, a bare rts: duart_init and duartb_reconfigure put it in
 * duartb.echo when the settings' echo is off. */
void duartb_echo_none(char c) {}

/* The done hook of channel B's setting descriptors and its PRESet:
 * reconfigures device 0. */
void duartb_settings_done(void) { psos_io_control(IO_DEVICE_DUART_B, 0, 0); }
