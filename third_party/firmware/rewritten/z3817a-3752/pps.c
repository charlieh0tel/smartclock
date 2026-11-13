/* pps.c's functions that are this image's own: the rest are
 * common/pps.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  The 1PPS interrupt, the Z3815A 4010's but for the FPGA's
 * seconds phase pulse (docs/firmware/z3817a-3752.md, "The export and the
 * checks").  Its comments' addresses are the Z3817A's. */

/* Each 1PPS: ticks the clock and the time code, keeps the GPS time and
 * date at the pulse in the exchange's message and broadcasts it to the
 * tasks waiting there (0x22f48): the image's own, the Z3815A 4010's but
 * for the FPGA's seconds phase pulse it gives between the time code and
 * the message (pps_sync_pulse).  Hand-written since its call of
 * psos_call is one C can make only through psos_broadcast_x.
 * pps_interrupt is this, keeping the scratch registers for the interrupt
 * that calls it. */
static __attribute__((used, noipa)) void pps_interrupt_kept(void) {
  clock_tick();
  ticks_since_pps = 0;
  timecode_update();
  pps_message.gps_time = get_gps_time();
  pps_message.gps_date = get_gps_date();
  if (pps_exchange != 0) {
    psos_broadcast_x(pps_exchange, &pps_message, 0, NULL, NULL);
  }
}

CPU_KEEPING_SCRATCH(pps_interrupt, pps_interrupt_kept);
