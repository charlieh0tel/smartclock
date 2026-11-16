/* From the 58503B 1.01.04's pps.c, the Z3805A 1.01.03's until it
 * shares common/: its functions, each the same in both images but
 * pps_init, whose code here enables input capture 1's interrupt through
 * a routine of its own, pps_interrupt_start, and the image's own 1PPS
 * interrupt (docs/firmware/z3805a-1.01.03.md, "The export and the
 * checks"). */

/* Clears input capture 1's flag and turns its interrupt on (0x12fb4). */
void pps_interrupt_start(void) {
  gpt.tflg &= ~TFLG_ICF1;
  gpt.tmsk |= TMSK_ICI1;
}

/* Creates the pps exchange, "1pps", clears its message and turns input
 * capture 1's interrupt on; returns create_x's error (0x12f76). */
int32_t pps_init(void) {
  int32_t status;

  pps_exchange = 0;
  status = psos_create_x("1pps", 0, 0, &pps_exchange);
  if (status == 0) {
    mutex_clear((mutex*)&pps_message);
    pps_interrupt_start();
  }
  return status;
}

/* Each 1PPS: ticks the clock, sends the time code if the port is
 * released for it (timecode_pps_send), keeps the GPS time and date at
 * the pulse in the exchange's message and broadcasts it to the tasks
 * waiting there (0x12f4a): the Z3805A 3543B's, relocated, which the
 * 58503B 1.01.04's extends with its front panel signal and the FPGA's
 * seconds phase pulse.  Hand-written since its call of psos_call is one C can
 * make only through psos_broadcast_x.  pps_interrupt is this, keeping the
 * scratch registers for the interrupt that calls it. */
static __attribute__((used, noipa)) void pps_interrupt_kept(void) {
  clock_tick();
  ticks_since_pps = 0;
  timecode_pps_send();
  pps_message.gps_time = get_gps_time();
  pps_message.gps_date = get_gps_date();
  if (pps_exchange != 0) {
    psos_broadcast_x(pps_exchange, &pps_message, 0, NULL, NULL);
  }
}

CPU_KEEPING_SCRATCH(pps_interrupt, pps_interrupt_kept);
