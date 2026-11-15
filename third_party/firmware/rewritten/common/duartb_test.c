/* DUART channel B's part of the loopback self-test, the same in each image
 * that includes this file (docs/firmware/rewrite.md, "Shared C", "A file
 * for some images").  An image without the DUART leaves the file out.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The loopback test's transmit on channel B. */
void duart_test_tx_b(void) { duart_test_transmit(&duart.b, IMR_TXRDYB); }
