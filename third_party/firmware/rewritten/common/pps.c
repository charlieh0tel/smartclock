/* The 1PPS: GPT input capture 1's interrupt, which ticks the clock and
 * the time code and wakes the tasks waiting at the pps exchange,
 * written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `services/pps`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The registers are mc68331.h's; the kernel's
 * services docs/firmware/psos.md's. */

/* Deletes the pps exchange and turns input capture 1's interrupt off;
 * returns delete_x's error, or 4 with no exchange. */
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
 * returns request_x's error, or 0. */
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
