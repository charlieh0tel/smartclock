/* From the Z3817A 3752's pps.c, the 58503B 1.01.04's own beside common/'s:
 * its functions, each the same in both images, but for the image's own 1PPS
 * interrupt (docs/firmware/58503b-1.01.04.md, "The export and the checks").
 * Its comments' addresses are the 58503B's.  With them, from other images'
 * rewritten C, of its functions, those the 58503B's code carries identical or
 * relocated, with the static helpers they use
 * (docs/firmware/58503b-1.01.04.md, "The link list"); their comments'
 * addresses are the 58503B's, but where they name another image. */

/* Each 1PPS: ticks the clock, sends the time code if the SCI is
 * released for it (timecode_pps_send), signals the front panel's task,
 * pulses the FPGA's seconds phase bit on an odd GPS second
 * (pps_sync_pulse), keeps the GPS time and date at the pulse in the
 * exchange's message and broadcasts it to the tasks waiting there
 * (0x22f28): the 58503A 3704's front panel signal and the Z3815A 4010's
 * pulse both, where the Z3817A's calls timecode_update alone.
 * Hand-written since its call of psos_call is one C can make only
 * through psos_broadcast_x.  pps_interrupt is this, keeping the scratch
 * registers for the interrupt that calls it. */
static __attribute__((used, noipa)) void pps_interrupt_kept(void) {
  clock_tick();
  ticks_since_pps = 0;
  timecode_pps_send();
  front_panel_signal_pps();
  pps_sync_pulse();
  pps_message.gps_time = get_gps_time();
  pps_message.gps_date = get_gps_date();
  if (pps_exchange != 0) {
    psos_broadcast_x(pps_exchange, &pps_message, 0, NULL, NULL);
  }
}

CPU_KEEPING_SCRATCH(pps_interrupt, pps_interrupt_kept);

/* The 1PPS's sync: on an odd GPS second, while the loop runs, the FPGA's
 * seconds phase bit pulsed, which pps_interrupt calls each 1PPS where the
 * 58503A 3704's signals its front panel (0x4af88). */
void pps_sync_pulse(void) {
  uint32_t time = get_clock_time();

  if ((gps_seconds(get_clock_date(), time) & 1) != 0 && pll.loop_running != 0) {
    fpga_set_bit(FPGA_BIT_SECONDS_PHASE, 1);
    fpga_set_bit(FPGA_BIT_SECONDS_PHASE, 0);
  }
}
