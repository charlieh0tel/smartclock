/* pps.c's functions that are this image's own: the rest are
 * common/pps.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The 1PPS's sync: on an odd GPS second, while the loop runs, the FPGA's
 * sync bit pulsed, which pps_interrupt calls each 1PPS where the 58503A
 * 3704's signals its front panel (0x516de). */
void pps_sync_pulse(void) {
  uint32_t time = get_clock_time();

  if ((gps_seconds(get_clock_date(), time) & 1) != 0 && pll.loop_running != 0) {
    fpga_set_bit(FPGA_BIT_SYNC, 1);
    fpga_set_bit(FPGA_BIT_SYNC, 0);
  }
}

/* Each 1PPS: ticks the clock and the time code, pulses the FPGA's sync
 * bit on an odd GPS second (pps_sync_pulse), keeps the GPS time and date
 * at the pulse in the exchange's message and broadcasts it to the tasks
 * waiting there (0x22f52).  The 58503A's signals its front panel's task
 * where this calls pps_sync_pulse.  Hand-written since its
 * call of psos_call is one C can make only through psos_broadcast_x.
 * pps_interrupt is this, keeping the scratch registers for the interrupt
 * that calls it. */
static __attribute__((used, noipa)) void pps_interrupt_kept(void) {
  clock_tick();
  ticks_since_pps = 0;
  timecode_update();
  pps_sync_pulse();
  pps_message.gps_time = get_gps_time();
  pps_message.gps_date = get_gps_date();
  if (pps_exchange != 0) {
    psos_broadcast_x(pps_exchange, &pps_message, 0, NULL, NULL);
  }
}

CPU_KEEPING_SCRATCH(pps_interrupt, pps_interrupt_kept);
