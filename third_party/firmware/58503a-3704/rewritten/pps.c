/* The 1PPS: GPT input capture 1's interrupt, which ticks the clock and
 * the time code and wakes the tasks waiting at the pps exchange,
 * written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `services/pps`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The registers are mc68331.h's; the kernel's
 * services docs/firmware/psos.md's. */

/* Creates the pps exchange, "1pps", clears its message and turns input
 * capture 1's interrupt on; returns create_x's error (0x12fd2). */
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

/* Each 1PPS: ticks the clock and the time code, signals the front
 * panel's task, keeps the GPS time and date at the pulse in the
 * exchange's message and broadcasts it to the tasks waiting there
 * (0x12f5e in the 58503A 3704; the Z3805A's, 0x12f5e, signals no front
 * panel).  Kept hand-written where the rest of the changed functions
 * are the decompiler's, since its call of psos_call is one C can make
 * only through psos_broadcast_x.  pps_interrupt is this, keeping the
 * scratch registers for the interrupt that calls it. */
static __attribute__((used, noipa)) void pps_interrupt_kept(void) {
  clock_tick();
  ticks_since_pps = 0;
  timecode_update();
  front_panel_signal_pps();
  pps_message.gps_time = get_gps_time();
  pps_message.gps_date = get_gps_date();
  if (pps_exchange != 0) {
    psos_broadcast_x(pps_exchange, &pps_message, 0, NULL, NULL);
  }
}

CPU_KEEPING_SCRATCH(pps_interrupt, pps_interrupt_kept);

/* Deletes the pps exchange and turns input capture 1's interrupt off;
 * returns delete_x's error, or 4 with no exchange (0x1301a). */
int32_t pps_interrupt_stop(void) {
  static const int32_t NO_EXCHANGE = 4;
  int32_t status;

  if (pps_exchange == 0) {
    return NO_EXCHANGE;
  }
  status = psos_delete_x(pps_exchange);
  gpt.tflg &= ~TFLG_ICF1;
  gpt.tmsk &= ~TMSK_ICI1;
  return status;
}

/* The GPS time and date at the last 1PPS, either pointer NULL to skip
 * it, waiting up to 1200 ticks for the next first if wait is set;
 * returns request_x's error, or 0 (0x1304c). */
int32_t pps_wait_time(uint32_t* time, uint32_t* date, int32_t wait) {
  static const uint32_t TIMEOUT_TICKS = 1200;

  if (wait != 0) {
    int32_t status =
        psos_request_x(pps_exchange, &pps_message, 0, TIMEOUT_TICKS);

    if (status != 0) {
      return status;
    }
  }
  if (time != NULL) {
    *time = pps_message.gps_time;
  }
  if (date != NULL) {
    *date = pps_message.gps_date;
  }
  return 0;
}
