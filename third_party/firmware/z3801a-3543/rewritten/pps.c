/* pps.c's functions that are this image's own: the rest are
 * common/pps.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Each 1PPS: ticks the clock and the time code, keeps the GPS time and
 * date at the pulse in the exchange's message and broadcasts it to the
 * tasks waiting there (0x12db6).  pps_interrupt is this, keeping the
 * scratch registers for the interrupt that calls it. */
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
