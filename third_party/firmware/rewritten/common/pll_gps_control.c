/* The functions of pll_gps_control.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_gps_control.c describes the module and holds the rest.  Written
 * by hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* Whether the awaited acknowledgment has come, no longer awaiting it
 * once it has. */
uint8_t gps_ack_received(void) {
  if (gps_ack_ready != 0) {
    gps_ack_awaited = GPS_ACK_NONE;
  }
  return gps_ack_ready;
}
