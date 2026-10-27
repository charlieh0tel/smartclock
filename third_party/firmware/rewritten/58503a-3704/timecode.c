/* The time code's checksum and TFOM digit, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The time code").  At each 1PPS
 * timecode_update (0x13da8) builds the :PTIMe:TCODe? message
 * (097-z3801-01, "Detailed Description of the Two Time Code Formats") in
 * tcode_message, which timecode_schedule (0x140d0) sends a delay after
 * the 1PPS's capture, output compare 3 counting it off, through the
 * routine tcode_request leaves in tcode_send, drtb_send_async (0x13d6a);
 * the 58503A has no ES message, which the Z3805A's sends so.  The
 * exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* The delay after the 1PPS capture, in GPT ticks, at which
 * timecode_update sends the time code (0x13ff6), as the Z3805A's es_build
 * does its ES message. */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* An array size, so #define: the checksum's two digits, CR, LF and NUL. */
#define TIMECODE_CHECKSUM_SIZE 5

/* The digits of a byte's tens and ones. */
static uint8_t timecode_tens(uint32_t value) { return value / 10; }

static uint8_t timecode_ones(uint32_t value) {
  return value - timecode_tens(value) * 10;
}

/* The time code's length, and its checksum and CR LF appended (0x14068);
 * the Z3805A's appends the checksum alone. */
static uint8_t timecode_finish(void) {
  char checksum[TIMECODE_CHECKSUM_SIZE];
  uint8_t length = 0;

  sprintf(checksum, "%02X\r\n", (uint8_t)tcode_checksum(tcode_message));
  strcat(tcode_message, checksum);
  while (tcode_message[length] != 0) {
    length++;
  }
  return length;
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

/* Asks for a time code of a format, 0 to 2, unless one is pending: the
 * format is kept, tcode_send set to drtb_send_async and tcode_pending set
 * once drtb_release_for_timecode frees the port; its result, 0 for a
 * request refused (0x13d28). */
char tcode_request(timecode_format format) {
  if (tcode_pending != 0 || format > TIMECODE_T2) {
    return 0;
  }
  tcode_format = format;
  char released = drtb_release_for_timecode();

  tcode_send = drtb_send_async;
  if (released != 0) {
    tcode_pending = 1;
  }
  return released;
}

/* At each 1PPS, the time code asked for: T1 of the GPS seconds at the next
 * 1PPS or T2 of the clock's next second, with its t, f, l, r and v and
 * checksum (0x13da8).  The 58503A has no ES message. */
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

/* Clears the time code's state, the Z3805A's ES message's remnants, and
 * sets the :DIAGnostic:TCODe masks, which nothing else reads, to 0xff
 * and 0 (0x14010). */
void timecode_init(void) {
  static const uint8_t TCODE_AMASK_ALL = 0xff;

  tcode_length = 0;
  tcode_pending = 0;
  es_length = 0;
  es_ascii = 0;
  es_enabled = 0;
  tcode_error_amask = TCODE_AMASK_ALL;
  tcode_error_omask = 0;
  tcode_status_amask = TCODE_AMASK_ALL;
  tcode_status_omask = 0;
}

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which its interrupt counts down.  A compare
 * already past sends it at once, through tcode_send, or takes one wrap off
 * (0x140d0). */
void timecode_schedule(uint32_t delay) {
  timecode_compare_wraps = delay >> 16;
  uint32_t compare = gpt.tic[0] + (delay & 0xffff);

  gpt.toc[2] = compare;
  gpt.tflg &= ~TFLG_OCF3;
  if (timecode_compare_passed(compare)) {
    if (timecode_compare_wraps == 0) {
      if (tcode_length != 0) {
        tcode_send(tcode_message, tcode_length);
      }
      return;
    }
    timecode_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF3;
  }
  gpt.tmsk |= TMSK_OCI3;
}
