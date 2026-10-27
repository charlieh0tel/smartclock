/* The time code, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/structures.md,
 * "The time code").  At each 1PPS timecode_update builds the
 * :PTIMe:TCODe? message (097-z3801-01, "Detailed Description of the Two
 * Time Code Formats") in tcode_message, which goes out a delay after the
 * 1PPS's capture, which an output compare counts off.  This image has
 * no ES message: timecode_update, which differs from the Z3805A's, is
 * the decompiler's.  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype. */

/* Asks for a time code of a format, 0 to 2, unless one is pending: the
 * format is kept and tcode_pending set once drtb_release_for_timecode
 * frees the port; its result, 0 for a request refused (0x139f6). */
char tcode_request(timecode_format format) {
  if (tcode_pending != 0 || format > TIMECODE_T2) {
    return 0;
  }
  tcode_format = format;
  char released = drtb_release_for_timecode();

  if (released != 0) {
    tcode_pending = 1;
  }
  return released;
}

/* Clears the time code's and the ES message's state (0x13cd2). */
void timecode_init(void) {
  tcode_length = 0;
  tcode_pending = 0;
  es_length = 0;
  es_ascii = 0;
  es_enabled = 0;
}

/* Whether an output compare at a delay after the 1PPS capture, input
 * capture 1, is already past: the free-running count, taken past the
 * capture across its wrap, has reached it. */
static bool timecode_compare_passed(uint32_t compare) {
  static const uint32_t TIMER_WRAP = 0x10000;
  uint32_t now = gpt.tcnt;

  if (now < gpt.tic[0]) {
    now += TIMER_WRAP;
  }
  return compare <= now;
}

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which primary_user_86 counts down.  A compare
 * already past sends it at once, or takes one wrap off (0x13d72). */
void timecode_schedule(uint32_t delay) {
  timecode_compare_wraps = delay >> 16;
  uint32_t compare = gpt.tic[0] + (delay & 0xffff);

  gpt.toc[2] = compare;
  gpt.tflg &= ~TFLG_OCF3;
  if (timecode_compare_passed(compare)) {
    if (timecode_compare_wraps == 0) {
      if (tcode_length != 0) {
        drtb_send_async(tcode_message, tcode_length);
      }
      return;
    }
    timecode_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF3;
  }
  gpt.tmsk |= TMSK_OCI3;
}
