/* timecode.c's functions that are this image's own: the rest are
 * common/timecode.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/timecode.c: of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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
 * format is kept, tcode_send set to sci_send_async and tcode_pending set
 * once sci_release_for_timecode frees the port; its result, 0 for a
 * request refused (0x23b3c). */
char tcode_request(timecode_format format) {
  if (tcode_pending != 0 || format > TIMECODE_T2) {
    return 0;
  }
  tcode_format = format;
  char released = sci_release_for_timecode();

  tcode_send = sci_send_async;
  if (released != 0) {
    tcode_pending = 1;
  }
  return released;
}

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which its interrupt counts down.  A compare
 * already past sends it at once, through tcode_send, or takes one wrap off
 * (0x24146). */
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

/* The done hook of :SOURce:PTIMe:TCODe:CONTinuous's descriptor: the SCI's
 * asynchronous sends allowed or not by it, where the Z3815A's updates
 * channel B's (0x24090). */
void tcode_continuous_done(void) { sci_async_allowed_update(); }

/* The same for the ES message, output compare 4 and es_compare_wraps
 * (0x2420e).  The Z3805A 3543B's. */
void es_schedule(uint32_t delay) {
  es_compare_wraps = delay >> 16;
  uint32_t compare = gpt.tic[0] + (delay & 0xffff);

  gpt.toc[3] = compare;
  gpt.tflg &= ~TFLG_OCF4;
  if (timecode_compare_passed(compare)) {
    if (es_compare_wraps == 0) {
      if (es_length != 0) {
        sci_send_async(es_message, es_length);
      }
      return;
    }
    es_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF4;
  }
  gpt.tmsk |= TMSK_OCI4;
}

/* The delay after the 1PPS capture, in GPT ticks, at which
 * timecode_update and es_build send their messages (0x23dc6,
 * 0x24056). */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* An array size, so #define: two hex digits, CR, LF and the NUL. */
#define TIMECODE_CHECKSUM_SIZE 5

/* The digits of a byte's tens and ones. */
static uint8_t timecode_tens(uint32_t value) { return value / 10; }

static uint8_t timecode_ones(uint32_t value) {
  return value - timecode_tens(value) * 10;
}

/* Builds es_message for a date and time and schedules it: the year, day
 * of the year, time, GPS-UTC offset and time zone, as one digit a byte
 * or, while es_ascii, as text; the status bits; and status_summary.  The
 * offset's tens are taken of it as unsigned, as the image takes them
 * (0x23bbc).  The Z3805A 3543B's but for the time zone, the status bits
 * of an unknown offset, a time zone and UTC, and the message's lengths. */
void es_build(uint32_t date, uint32_t time) {
  /* The ES message's status bits. */
  static const uint16_t ES_TIME_UNSET = 0x01; /* the clock or the offset */
  static const uint16_t ES_LEAP_ADD = 0x02;
  static const uint16_t ES_LEAP_TODAY = 0x04;
  static const uint16_t ES_HOLDOVER = 0x10;
  static const uint16_t ES_TIME_ZONE = 0x20;
  static const uint16_t ES_UTC = 0x40;
  /* The ES message as digits, one a byte, and as ASCII. */
  static const uint8_t ES_DIGITS_LENGTH = 18;
  static const uint8_t ES_ASCII_LENGTH = 24;
  int16_t offset;

  es_length = 0;
  bool offset_valid = get_gps_utc_offset(&offset);

  if (!offset_valid) {
    offset = 0;
  }
  uint16_t status = !offset_valid ? ES_TIME_UNSET : 0;
  uint16_t day = day_of_year(date);

  if (time_valid() == 0) {
    status = ES_TIME_UNSET;
  }
  if (nv_settings.leap_pending && nv_settings.leap_date == date) {
    status |= ES_LEAP_TODAY;
    if (nv_settings.leap_minute_length > CLOCK_SECONDS_PER_MINUTE) {
      status |= ES_LEAP_ADD;
    }
  }
  if (pll.stage == PLL_STAGE_HOLDOVER ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    status |= ES_HOLDOVER;
  }
  if (nv_settings.time_zone[0] != 0 || nv_settings.time_zone[1] != 0) {
    status |= ES_TIME_ZONE;
  }
  if (nv_settings.gps_utc != 0) {
    status |= ES_UTC;
  }
  uint8_t second = time;
  uint8_t minute = time >> 8;
  uint8_t hour = time >> 16;
  uint16_t year = date >> 16;

  if (es_ascii == 0) {
    uint32_t day_ones = day % 100;
    uint8_t offset_tens = (uint32_t)offset / 10;

    es_message[0] = timecode_tens(year % 100);
    es_message[1] = timecode_ones(year % 100);
    es_message[2] = day / 100;
    es_message[3] = timecode_tens(day_ones);
    es_message[4] = timecode_ones(day_ones);
    es_message[5] = timecode_tens(hour);
    es_message[6] = timecode_ones(hour);
    es_message[7] = timecode_tens(minute);
    es_message[8] = timecode_ones(minute);
    es_message[9] = timecode_tens(second);
    es_message[10] = timecode_ones(second);
    es_message[11] = offset_tens;
    es_message[12] = (uint8_t)offset - offset_tens * 10;
    es_message[13] = nv_settings.time_zone[0];
    es_message[14] = nv_settings.time_zone[1];
    es_message[15] = status;
    es_message[16] = status_summary;
    es_message[17] = '\r';
    es_length = ES_DIGITS_LENGTH;
  } else {
    sprintf(es_message, "%02d%03d%02d%02d%02d%02d% 03d% 03d%02X%02X\r",
            (uint32_t)year % 100, (uint32_t)day, (uint32_t)hour,
            (uint32_t)minute, (uint32_t)second, (int32_t)offset,
            nv_settings.time_zone[0], nv_settings.time_zone[1],
            (uint32_t)status, (uint32_t)status_summary);
    es_length = ES_ASCII_LENGTH;
  }
  es_schedule(TIMECODE_DELAY);
}

/* The time code's length, with its checksum, CR and LF appended. */
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

/* At each 1PPS: the ES message while the time code is sent continuously
 * and es_enabled, and the time code asked for, T1 of the GPS seconds at
 * the next 1PPS or T2 of the clock's next second, with its t, f, l, r
 * and v and checksum (0x23dda).  The Z3805A 3543B's but that the ES
 * message waits on :PTIMe:TCODe:CONTinuous, not an even second, and the
 * checksum is followed by CR LF. */
void timecode_update(void) {
  if (tcode_continuous != 0) {
    uint32_t date = get_clock_date();
    uint32_t time = get_clock_time();

    if (es_enabled != 0) {
      es_build(date, time);
    }
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

/* Clears the time code's and the ES message's state (0x24070).  The
 * Z3805A 3543B's. */
void timecode_init(void) {
  tcode_length = 0;
  tcode_pending = 0;
  es_length = 0;
  es_ascii = 0;
  es_enabled = 0;
}
