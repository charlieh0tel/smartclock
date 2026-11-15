/* pps.c's functions that are this image's own: the rest are common/pps.c's,
 * the same in every image (docs/firmware/rewrite.md, "Shared C").  The 1PPS
 * interrupt, the image's own (docs/firmware/z3816a-4001.md, "The export and
 * the checks").  Its comments' addresses are the Z3816A's. */

/* Each 1PPS: ticks the clock and the time code, keeps the GPS time and
 * date at the pulse in the exchange's message and broadcasts it to the
 * tasks waiting there (0x22ec0), as the Z3805A 3543B's, with no front
 * panel to signal and no seconds phase pulse.  Hand-written since its call of
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

/* Creates the pps exchange, "1pps", clears its message and turns input
 * capture 1's interrupt on; returns create_x's error. */
int32_t pps_init(void) {
  int32_t status;

  pps_exchange = 0;
  status = psos_create_x("1pps", 0, 0, &pps_exchange);
  if (status == 0) {
    mutex_clear((mutex*)&pps_message);
    gpt.tflg &= ~TFLG_ICF1;
    gpt.tmsk |= TMSK_ICI1;
  }
  return status;
}
