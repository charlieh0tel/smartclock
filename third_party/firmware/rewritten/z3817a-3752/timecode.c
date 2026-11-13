/* From the Z3815A 4010's rewritten/timecode.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"), and
 * tcode_request and timecode_update, the Z3815A's written as this image's.  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* The free-running GPT count's span, which a compare past its wrap
 * adds. */
static const uint32_t TIMECODE_TIMER_WRAP = 0x10000;

/* Whether an output compare at a delay after the 1PPS capture, input
 * capture 1, is already past: the free-running count, taken past the
 * capture across its wrap, has reached it. */
static bool timecode_compare_passed(uint32_t compare) {
  uint32_t now = gpt.tcnt;

  if (now < gpt.tic[0]) {
    now += TIMECODE_TIMER_WRAP;
  }
  return compare <= now;
}

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which its interrupt counts down.  A compare
 * already past sends it at once, through tcode_send, or takes one wrap off
 * (0x24074). */
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

/* Clears the time code's state, the Z3805A's ES message's remnants, and
 * sets the :DIAGnostic:TCODe masks, which only their SCPI handlers read,
 * to 0xff and 0 (0x23fb0). */
void timecode_init(void) {
  static const uint8_t TCODE_AND_MASK_ALL = 0xff;

  tcode_length = 0;
  tcode_pending = 0;
  es_length = 0;
  es_ascii = 0;
  es_enabled = 0;
  tcode_error_and_mask = TCODE_AND_MASK_ALL;
  tcode_error_or_mask = 0;
  tcode_status_and_mask = TCODE_AND_MASK_ALL;
  tcode_status_or_mask = 0;
}

/* Asks for a time code of a format, T1 to FL3 (0x23cb4), unless one is
 * pending: the format is kept, the SCI freed for it whatever sci_port
 * gives, tcode_send set to the SCI's asynchronous send, and tcode_pending
 * set once the port is free; its result, 0 for a request refused.  The
 * Z3815A's takes T1 to FL2 and frees DUART channel B for sci_port 0. */
char tcode_request(timecode_format format, char sci_port) {
  if (tcode_pending != 0 || (uint8_t)format > TIMECODE_FL3) {
    return 0;
  }
  tcode_format = format;
  const char released = sci_release_for_timecode();

  tcode_send = sci_send_async;
  if (released != 0) {
    tcode_pending = 1;
  }
  return released;
}

/* The delay after the 1PPS capture at which the time code goes out, in
 * GPT ticks (0x23f96). */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* The time code's checksum, two digits, its line's end and a NUL, the
 * original's 5 bytes: an array size, so #define. */
#define TIMECODE_CHECKSUM_SIZE 5

/* The line end alone timecode_update sends for FL1 to FL3 (0x24070). */
static const char TIMECODE_LINE_END[] = "\r\n";

/* The time code's checksum and line end appended; its length. */
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

/* At each 1PPS, the time code asked for: T1 of the GPS seconds at the
 * next 1PPS or T2 of the clock's next second, with its t, f, l, r and v,
 * its checksum and a line end; for FL1 to FL3, and any format past them,
 * a line end alone (0x23d3a, its jump table at 0x23dcc).  The Z3815A's
 * also starts the ES message on even GPS seconds and sends nothing for
 * FL1 and FL2. */
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
  uint8_t length;

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
    length = timecode_finish();
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
    length = timecode_finish();
  } else {
    for (uint32_t i = 0; i < sizeof(TIMECODE_LINE_END); i++) {
      tcode_message[i] = TIMECODE_LINE_END[i];
    }
    length = sizeof(TIMECODE_LINE_END) - 1;
  }
  tcode_length = length;
  if (tcode_length != 0) {
    timecode_schedule(TIMECODE_DELAY);
  }
  tcode_pending = 0;
}
