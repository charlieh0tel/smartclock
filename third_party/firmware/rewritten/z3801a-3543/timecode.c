/* The time code, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/structures.md,
 * "The time code").  At each 1PPS timecode_update builds the
 * :PTIMe:TCODe? message (097-z3801-01, "Detailed Description of the Two
 * Time Code Formats") in tcode_message, which goes out a delay after the
 * 1PPS's capture, which an output compare counts off.  This image has
 * no ES message.  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype. */

/* The delay after the 1PPS capture, in GPT ticks, at which
 * timecode_update sends the time code (0x13cb8). */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* An array size, so #define. */
#define TIMECODE_CHECKSUM_SIZE 3

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

/* The time code's length, and its checksum appended. */
static uint8_t timecode_finish(void) {
  char checksum[TIMECODE_CHECKSUM_SIZE];
  uint8_t length = 0;

  sprintf(checksum, "%02X", (uint8_t)tcode_checksum(tcode_message));
  strcat(tcode_message, checksum);
  while (tcode_message[length] != 0) {
    length++;
  }
  return length;
}

/* At each 1PPS, the time code asked for: T1 of the GPS seconds at the
 * next 1PPS or T2 of the clock's next second, with its t, f, l, r and v
 * and checksum.  The Z3805A's also builds the ES message here (0x13a6a). */
void timecode_update(void) {
  if (tcode_pending == 0) {
    return;
  }
  tcode_length = 0;
  /* tfom_code takes the float's bits. */
  union {
    float value;
    uint32_t bits;
  } tfom = {.value = pll.tfom};
  uint8_t tfom_digit = tfom_code(tfom.bits);
  frequency_figure_of_merit ffom;

  get_ffom(&ffom);
  char leap = '0';

  if (nv_settings.leap_pending) {
    leap =
        nv_settings.leap_minute_length == CLOCK_LEAP_MINUTE_SHORT ? '-' : '+';
  }
  char request = status_request_service ? '1' : '0';
  char valid = time_valid() ? '0' : '1';

  if (tcode_format == TIMECODE_T1) {
    uint32_t gps_date = get_gps_date();
    uint32_t gps_time = get_gps_time();

    sprintf(tcode_message, "T1#H%08X%01d%01d%1c%1c%1c",
            gps_seconds(gps_date, gps_time) + 1, (uint32_t)tfom_digit,
            (uint32_t)ffom, (int32_t)leap, (int32_t)request, (int32_t)valid);
    tcode_length = timecode_finish();
  } else if (tcode_format == TIMECODE_T2_TEST || tcode_format == TIMECODE_T2) {
    uint32_t next_date = get_clock_date();
    /* Even, since gcc stores it as a long, misaligned at an odd address
     * (MC68331UM, 4.4.4). */
    time_of_day __attribute__((aligned(2))) next =
        clock_time_of(get_clock_time());

    time_next_second(&next_date, &next);
    uint32_t year = next_date >> 16;
    uint32_t month = (uint8_t)(next_date >> 8);
    uint32_t day = (uint8_t)next_date;

    if (tcode_format == TIMECODE_T2_TEST) {
      sprintf(
          tcode_message, "rtcm? %u,T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
          0xffffffff, year, month, day, (uint32_t)next.hour,
          (uint32_t)next.minute, (uint32_t)next.second, (uint32_t)tfom_digit,
          (uint32_t)ffom, (int32_t)leap, (int32_t)request, (int32_t)valid);
    } else {
      sprintf(tcode_message, "T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
              year, month, day, (uint32_t)next.hour, (uint32_t)next.minute,
              (uint32_t)next.second, (uint32_t)tfom_digit, (uint32_t)ffom,
              (int32_t)leap, (int32_t)request, (int32_t)valid);
    }
    tcode_length = timecode_finish();
  } else {
    tcode_length = 0;
  }
  if (tcode_length != 0) {
    timecode_schedule(TIMECODE_DELAY);
  }
  tcode_pending = 0;
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
