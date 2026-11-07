/* timecode.c's functions that are this image's own: the rest are
 * common/timecode.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  The ES message: while :PTIMe:TCODe:CONTinuous is set,
 * timecode_update starts it on even seconds, and output compare 4
 * builds it a delay after the 1PPS capture, sends it on DUART channel B,
 * and a period later takes channel B back (docs/firmware/z3815a-4010.md;
 * structures.md, "The time code").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* The binary ES message's bytes: two marks, the time (es_time_seconds'
 * or es_time_digits'), the status and error bytes, a carriage return,
 * and es_checksum's sum of the time to the return, high byte first.  An
 * array index, so #define. */
#define ES_BINARY_MARK_1 0
#define ES_BINARY_MARK_2 1
#define ES_BINARY_TIME 2
#define ES_BINARY_STATUS 15
#define ES_BINARY_ERROR 16
#define ES_BINARY_RETURN 17

/* The bytes es_time_seconds writes: zeros, so that it takes the digits'
 * thirteen, then the GPS seconds and the GPS-UTC offset, high bytes
 * first. */
#define ES_SECONDS_PAD 7

/* The ES message's digits' base, signed, since the GPS-UTC offset's
 * digits are, and the years es_build and es_time_digits take the year's
 * last two digits by. */
static const int32_t ES_DIGIT_BASE = 10;
static const uint32_t YEARS_PER_CENTURY = 100;

/* The first output compare's delay after the 1PPS capture, and how long
 * channel B's asynchronous sends stay allowed after it, in GPT ticks
 * (0x23dd0, 0x23dc4). */
static const uint32_t ES_DELAY = 0x51eb;
static const uint32_t ES_PERIOD = 0x147ae;

/* The free-running GPT count's span, which a compare past its wrap
 * adds. */
static const uint32_t TIMECODE_TIMER_WRAP = 0x10000;

/* Whether an output compare is already past: the free-running count,
 * taken past the count it was set from across its wrap, has reached it.
 * The count it was set from is read after the free-running count, as
 * the image reads it. */
static bool es_compare_passed(uint32_t compare,
                              const volatile uint16_t* since) {
  uint32_t now = gpt.tcnt;

  if (now < *since) {
    now += TIMECODE_TIMER_WRAP;
  }
  return compare <= now;
}

/* Starts an ES message: channel B's asynchronous sends allowed, the
 * period set, and output compare 4 set a delay after the 1PPS capture;
 * timecode_update calls it on even GPS seconds while tcode_continuous is
 * set (0x23dbe). */
void es_start(void) {
  drtb_async_allow();
  es_period_set(ES_PERIOD);
  es_schedule(ES_DELAY);
}

/* Appends the 16-bit sum of so many bytes after them, high byte first
 * (0x23dde). */
void es_checksum(uint8_t* bytes, uint32_t length) {
  uint16_t sum = 0;

  for (uint32_t i = 0; i < length; i++) {
    sum += bytes[i];
  }
  bytes[length] = sum >> CPU_BYTE_BITS;
  bytes[length + 1] = sum;
}

/* The binary ES message's time as GPS seconds: seven zeros, then the
 * seconds and the GPS-UTC offset, high bytes first (0x23e16). */
void es_time_seconds(uint8_t* to, uint32_t date, uint32_t time,
                     int16_t offset) {
  const int32_t seconds = gps_seconds(date, time);

  for (uint8_t i = 0; i < ES_SECONDS_PAD; i++) {
    *to++ = 0;
  }
  for (int32_t shift = (CPU_LONG_BYTES - 1) * CPU_BYTE_BITS; shift >= 0;
       shift -= CPU_BYTE_BITS) {
    *to++ = seconds >> shift;
  }
  *to++ = (uint16_t)offset >> CPU_BYTE_BITS;
  *to = offset;
}

/* Writes a value's tens and units digits, one a byte, signed as the
 * image divides the GPS-UTC offset; returns the byte after them. */
static char* es_digit_pair(char* to, int32_t value) {
  *to++ = value / ES_DIGIT_BASE;
  *to++ = value % ES_DIGIT_BASE;
  return to;
}

/* The binary ES message's time as digits, one a byte: the year's last
 * two, the day of the year's three, the hour's, minute's and second's
 * two, and the GPS-UTC offset's two (0x23e5c). */
void es_time_digits(char* to, uint32_t year, uint32_t day, time_of_day* time,
                    int16_t offset) {
  static const uint32_t ES_HUNDREDS = ES_DIGIT_BASE * ES_DIGIT_BASE;

  to = es_digit_pair(to, year % YEARS_PER_CENTURY);
  *to++ = day / ES_HUNDREDS;
  to = es_digit_pair(to, day % ES_HUNDREDS);
  to = es_digit_pair(to, time->hour);
  to = es_digit_pair(to, time->minute);
  to = es_digit_pair(to, time->second);
  es_digit_pair(to, offset);
}

/* Sets es_error from the output's state, the antenna current, the
 * :STATus:OPERation:HARDware condition, holdover_duration_exceeded and
 * switch_word_bit_10 and clock_deviation_alarm; backp_task calls it each pass
 * (0x23f3a). */
void es_error_update(void) {
  /* The :STATus:OPERation:HARDware condition's bit 6, the EFC near its
   * limit (STATUS_EFC_NEAR_LIMIT_SET). */
  static const uint16_t STATUS_HARDWARE_EFC_NEAR_LIMIT = 0x40;
  const uint16_t hardware = status_hardware_condition();
  const output_state state = output_state_get();
  es_error_bit error = 0;

  if (switch_word_bit_10 != 0) {
    error |= ES_ERROR_SWITCH_WORD_BIT_10;
  }
  if (state == OUTPUT_ALARM) {
    error |= ES_ERROR_ALARM;
  }
  if (hmon_check_antenna_current() != 0) {
    error |= ES_ERROR_ANTENNA_CURRENT;
  }
  if ((hardware & STATUS_HARDWARE_EFC_NEAR_LIMIT) != 0) {
    error |= ES_ERROR_EFC_NEAR_LIMIT;
  }
  if (holdover_duration_exceeded != 0) {
    error |= ES_ERROR_HOLDOVER_DURATION_EXCEEDED;
  }
  if (state == OUTPUT_BLOCKED) {
    error |= ES_ERROR_BLOCKED;
  }
  if (clock_deviation_alarm_get()) {
    error |= ES_ERROR_CLOCK_DEVIATION;
  }
  es_error = error;
}

/* The ES message's status byte for a date: the time not valid or no
 * GPS-UTC offset, a leap second pending on the date, its minute longer
 * or shorter, the output standby or active, the loop in holdover or its
 * recovery, GPS time, and channel B sending (0x23fb2). */
es_status_bit es_status(uint32_t date) {
  es_status_bit status = 0;
  const output_state state = output_state_get();

  if (!time_valid() || !get_gps_utc_offset(NULL)) {
    status |= ES_STATUS_TIME_INVALID;
  }
  if (nv_settings.leap_pending != 0 && nv_settings.leap_date == date) {
    status |= nv_settings.leap_minute_length > CLOCK_SECONDS_PER_MINUTE
                  ? ES_STATUS_LEAP_LONGER
                  : ES_STATUS_LEAP_SHORTER;
  }
  if (state == OUTPUT_STANDBY) {
    status |= ES_STATUS_STANDBY;
  }
  if (state == OUTPUT_ACTIVE) {
    status |= ES_STATUS_ACTIVE;
  }
  if (pll.stage == PLL_STAGE_HOLDOVER ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    status |= ES_STATUS_HOLDOVER;
  }
  if (nv_settings.gps_utc == 0) {
    status |= ES_STATUS_GPS_TIME;
  }
  if (drtb_sending()) {
    status |= ES_STATUS_DRTB_SENDING;
  }
  return status;
}

/* Builds es_message from the GPS date and time and sends it on channel B:
 * binary, two marks and the time as GPS seconds or, while
 * nv_settings.gps_utc, as digits, or, while es_ascii, as text; with
 * es_status's bits and es_error, each through the time code's AND and OR
 * masks (0x24050). */
void es_build(void) {
  static const uint8_t ES_MARK_1 = 0xaa;
  static const uint8_t ES_MARK_2 = 0xcc;
  /* What es_checksum sums, from the time to the return. */
  static const uint32_t ES_CHECKSUMMED = 0x10;
  /* The ES message's lengths: binary, and as text in GPS seconds and in
   * a date and time. */
  static const uint8_t ES_BINARY_LENGTH = 20;
  static const uint8_t ES_SECONDS_LENGTH = 19;
  static const uint8_t ES_DATE_LENGTH = 26;
  uint32_t date;
  /* Even, since gcc stores it as a long, misaligned at an odd address
   * (MC68331UM, 4.4.4). */
  time_of_day __attribute__((aligned(2))) time;
  int16_t offset;

  es_length = 0;
  get_gps_datetime(&date, &time);
  if (!get_gps_utc_offset(&offset)) {
    offset = 0;
  }
  const uint8_t status =
      tcode_status_or_mask | (tcode_status_and_mask & es_status(date));
  const uint8_t error = tcode_error_or_mask | (tcode_error_and_mask & es_error);
  const uint16_t year = date >> 16;

  if (es_ascii == 0) {
    es_message[ES_BINARY_MARK_1] = ES_MARK_1;
    es_message[ES_BINARY_MARK_2] = ES_MARK_2;
    if (nv_settings.gps_utc != 0) {
      es_time_digits(&es_message[ES_BINARY_TIME], year,
                     (uint16_t)day_of_year(date), &time, offset);
    } else {
      es_time_seconds((uint8_t*)&es_message[ES_BINARY_TIME], date,
                      clock_time_bits(time), offset);
    }
    es_message[ES_BINARY_STATUS] = status;
    es_message[ES_BINARY_ERROR] = error;
    es_message[ES_BINARY_RETURN] = '\r';
    es_checksum((uint8_t*)&es_message[ES_BINARY_TIME], ES_CHECKSUMMED);
    es_length = ES_BINARY_LENGTH;
  } else if (nv_settings.gps_utc != 0) {
    sprintf(es_message, "%02d %03d %02d:%02d:%02d %02d %02X %02X\r\n",
            (uint32_t)year % YEARS_PER_CENTURY,
            (uint32_t)(uint16_t)day_of_year(date), (uint32_t)time.hour,
            (uint32_t)time.minute, (uint32_t)time.second, (int32_t)offset,
            (uint32_t)status, (uint32_t)error);
    es_length = ES_DATE_LENGTH;
  } else {
    sprintf(es_message, "%08X %02X %02X %02X\r\n",
            gps_seconds(date, clock_time_bits(time)), (int32_t)offset,
            (uint32_t)status, (uint32_t)error);
    es_length = ES_SECONDS_LENGTH;
  }
  drtb_send_async(es_message, es_length);
  clock_deviation_alarm_clear();
}

/* Sets es_period, the GPT ticks from es_send's match to es_end's
 * (0x24662). */
void es_period_set(uint32_t period) { es_period = period; }

/* Ends the ES message's period: channel B's asynchronous sends forbidden
 * and output compare 4's flag and interrupt cleared (0x2466c). */
void es_end(void) {
  drtb_async_forbid();
  gpt.tflg &= ~TFLG_OCF4;
  gpt.tmsk &= ~TMSK_OCI4;
}

/* Output compare 4's first match: builds and sends es_message, then sets
 * the compare es_period past this one, the period's high word the
 * compare's wraps to let pass, which primary_user_87 counts down; at its
 * match es_end takes channel B back.  A compare already past ends it at
 * once, or takes one wrap off.  The image compares the count in D0
 * under es_build's result's high word, 0 on each of its paths (0x24684). */
void es_send(void) {
  es_build();
  es_sent = 1;
  es_compare_wraps = es_period >> 16;
  const uint16_t last = gpt.toc[3];
  const uint32_t compare = (es_period & 0xffff) + last;

  gpt.toc[3] = compare;
  gpt.tflg &= ~TFLG_OCF4;
  if (es_compare_passed(compare, &last)) {
    if (es_compare_wraps == 0) {
      es_end();
      return;
    }
    es_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF4;
  }
  gpt.tmsk |= TMSK_OCI4;
}

/* Sets output compare 4 to send the ES message a delay, in GPT ticks,
 * after the 1PPS capture, input capture 1: the delay's high word the
 * compare's wraps to let pass (es_compare_wraps), which primary_user_87
 * counts down.  A compare already past sends it at once, or takes one
 * wrap off (0x246fa). */
void es_schedule(uint32_t delay) {
  es_sent = 0;
  es_compare_wraps = delay >> 16;
  const uint32_t compare = (delay & 0xffff) + gpt.tic[0];

  gpt.toc[3] = compare;
  gpt.tflg &= ~TFLG_OCF4;
  if (es_compare_passed(compare, &gpt.tic[0])) {
    if (es_compare_wraps == 0) {
      es_send();
      return;
    }
    es_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF4;
  }
  gpt.tmsk |= TMSK_OCI4;
}

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

/* Clears the time code's state, the Z3805A's ES message's remnants, and
 * sets the :DIAGnostic:TCODe masks, which only their SCPI handlers read,
 * to 0xff and 0 (0x24494). */
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

/* The done hook of :SOURce:PTIMe:TCODe:CONTinuous's setting descriptor
 * (0x48942): channel B's asynchronous sends follow the setting
 * (0x244d0). */
void tcode_continuous_done(void) { drtb_async_allowed_update(); }

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which its interrupt counts down.  A compare
 * already past sends it at once, through tcode_send, or takes one wrap off
 * (0x2459a). */
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

/* Asks for a time code of a format, T1 to FL2, where the 58503A's takes
 * T1 to T2 (0x23d34), unless one is pending: the
 * format is kept, and DUART channel B, or for sci_port set the SCI, freed
 * for it, tcode_send set to the port's asynchronous send and
 * tcode_pending set once the port is free; its result, 0 for a request
 * refused.  The 58503A's has channel B alone (0x23d20). */
char tcode_request(timecode_format format, char sci_port) {
  if (tcode_pending != 0 || (uint8_t)format > TIMECODE_FL2) {
    return 0;
  }
  tcode_format = format;
  char released;

  if (sci_port == 0) {
    released = drtb_release_for_timecode();
    tcode_send = drtb_send_async;
  } else {
    released = sci_release_for_timecode();
    tcode_send = sci_send_async;
  }
  if (released != 0) {
    tcode_pending = 1;
  }
  return released;
}

/* The delay after the 1PPS capture at which the time code goes out, in
 * GPT ticks (0x2447a). */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* The time code's checksum, two digits, its line's end and a NUL, the
 * original's 5 bytes: an array size, so #define. */
#define TIMECODE_CHECKSUM_SIZE 5

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

/* At each 1PPS: the ES message started on even GPS seconds while
 * tcode_continuous, and the time code asked for, T1 of the GPS seconds at
 * the next 1PPS or T2 of the clock's next second, with its t, f, l, r and
 * v, its checksum and a line end, which the Z3805A's leaves off; the
 * Z3805A's builds the ES message itself on even clock seconds while
 * es_enabled (0x24212). */
void timecode_update(void) {
  if (tcode_continuous != 0 && (get_gps_time() & 1) == 0) {
    es_start();
  }
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
