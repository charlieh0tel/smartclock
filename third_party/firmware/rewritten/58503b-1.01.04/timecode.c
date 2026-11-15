/* The 58503B 1.01.04's own time code requests, written by hand in place
 * of the decompiler's reading (docs/firmware/58503b-1.01.04.md, "The link
 * list"): the request is a byte, tcode_format, which timecode_pps_send
 * hands timecode_update each 1PPS while the SCI is released for the time
 * code, where the other images keep a pending flag and a sender. */

/* Asks for a time code of a format unless the SCI is released for one
 * already: the format kept and the SCI released, its result; 0 for a
 * request refused (0x23da6). */
char tcode_request(timecode_format format) {
  if (sci_timecode_released() != 0) {
    return 0;
  }
  tcode_format = format;
  return sci_release_for_timecode();
}

/* Whether output compare 3's match, compare, is past: the timer's count,
 * a wrap on if it is behind the 1PPS capture, has reached it. */
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
 * (timecode_compare_wraps), which its interrupt counts down.  A compare
 * already past sends it at once, while the SCI is released for the time
 * code, or takes one wrap off (0x24140).  The other images send through
 * tcode_send, and only a message of some length. */
void timecode_schedule(uint32_t delay) {
  timecode_compare_wraps = delay >> 16;
  uint32_t compare = gpt.tic[0] + (delay & 0xffff);

  gpt.toc[2] = compare;
  gpt.tflg &= ~TFLG_OCF3;
  if (timecode_compare_passed(compare)) {
    if (timecode_compare_wraps == 0) {
      if (sci_timecode_released() != 0) {
        sci_send_async(tcode_message, tcode_length);
      }
      return;
    }
    timecode_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF3;
  }
  gpt.tmsk |= TMSK_OCI3;
}

/* The time code of the format requested, sent while the SCI is released
 * for it; pps_interrupt's each 1PPS (0x23dea). */
void timecode_pps_send(void) {
  if (sci_timecode_released() != 0) {
    timecode_update((char*)&tcode_format, 0);
  }
}

/* Clears the time code's length, the Z3805A's ES message's remnants, and
 * sets the :DIAGnostic:TCODe masks, which only their SCPI handlers read,
 * to 0xff and 0 (0x24082).  The Z3817A 3752's, but that this image keeps
 * no pending flag. */
void timecode_init(void) {
  static const uint8_t TCODE_AND_MASK_ALL = 0xff;

  tcode_length = 0;
  es_length = 0;
  es_ascii = 0;
  es_enabled = 0;
  tcode_error_and_mask = TCODE_AND_MASK_ALL;
  tcode_error_or_mask = 0;
  tcode_status_and_mask = TCODE_AND_MASK_ALL;
  tcode_status_or_mask = 0;
}

/* A time code's block, which timecode_update builds in place: tcode_format
 * (0x104502), then tcode_length and the message, which 0x24140 sends to
 * the SCI from 0x104504 (0x24198). */
typedef struct timecode_block {
  timecode_format format; /* +0x00 */
  uint8_t length;         /* +0x01 */
  char message[];         /* +0x02 */
} timecode_block;

/* The output compare's delay after the 1PPS capture at which 0x24140
 * sends the time code: 0 wraps and 0x28f5 counts. */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* An array size, so #define: the checksum's two digits, CR, LF and
 * NUL. */
#define TIMECODE_CHECKSUM_SIZE 5

/* The message of a format with none, an empty line. */
static const char TIMECODE_EMPTY[] = "\r\n";

/* A time code's message's length, its checksum and CR LF appended
 * first. */
static uint8_t timecode_finish(char* message) {
  char checksum[TIMECODE_CHECKSUM_SIZE];
  uint8_t length = 0;

  sprintf(checksum, "%02X\r\n", (uint8_t)tcode_checksum(message));
  strcat(message, checksum);
  while (message[length] != 0) {
    length++;
  }
  return length;
}

/* Builds a time code in a block of the format it holds: T1 of the GPS
 * seconds at the next 1PPS, or T2 of the clock's next second, with or
 * without the test's prefix, each with its t, f, l, r and v and checksum;
 * a format of 6 keeps the length passed, any other an empty line; then
 * has 0x24140 send it after TIMECODE_DELAY (0x23e0c).  The 58503A 3704's
 * builds tcode_message itself, while tcode_pending, and schedules the
 * send only for a message. */
void timecode_update(char* request, char length) {
  timecode_block* block = (timecode_block*)request;

  block->length = 0;
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
  char service = status_request_service ? '1' : '0';
  char valid = time_valid() ? '0' : '1';

  switch (block->format) {
    case TIMECODE_T1: {
      uint32_t gps_date = get_gps_date();
      uint32_t gps_time = get_gps_time();

      sprintf(block->message, "T1#H%08X%01d%01d%1c%1c%1c",
              gps_seconds(gps_date, gps_time) + 1, (uint32_t)tfom_digit,
              (uint32_t)ffom, (int32_t)leap, (int32_t)service, (int32_t)valid);
      length = timecode_finish(block->message);
      break;
    }
    case TIMECODE_T2_TEST:
    case TIMECODE_T2: {
      uint32_t next_date = get_clock_date();
      /* Even, since gcc stores it as a long, misaligned at an odd address
       * (MC68331UM, 4.4.4). */
      time_of_day __attribute__((aligned(2))) next =
          clock_time_of(get_clock_time());

      time_next_second(&next_date, &next);
      uint32_t year = next_date >> 16;
      uint32_t month = (uint8_t)(next_date >> 8);
      uint32_t day = (uint8_t)next_date;

      if (block->format == TIMECODE_T2_TEST) {
        sprintf(block->message,
                "rtcm? %u,T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
                0xffffffff, year, month, day, (uint32_t)next.hour,
                (uint32_t)next.minute, (uint32_t)next.second,
                (uint32_t)tfom_digit, (uint32_t)ffom, (int32_t)leap,
                (int32_t)service, (int32_t)valid);
      } else {
        sprintf(block->message, "T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
                year, month, day, (uint32_t)next.hour, (uint32_t)next.minute,
                (uint32_t)next.second, (uint32_t)tfom_digit, (uint32_t)ffom,
                (int32_t)leap, (int32_t)service, (int32_t)valid);
      }
      length = timecode_finish(block->message);
      break;
    }
    case TIMECODE_LENGTH_KEPT:
      break;
    default:
      libc_copy(block->message, TIMECODE_EMPTY);
      length = sizeof TIMECODE_EMPTY - 1;
      break;
  }
  block->length = length;
  timecode_schedule(TIMECODE_DELAY);
}
