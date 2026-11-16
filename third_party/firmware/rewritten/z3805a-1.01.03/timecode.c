/* From the 58503B 1.01.04's rewritten/timecode.c and the Z3815A 4010's
 * rewritten/timecode.c, the Z3805A 1.01.03's own beside common/'s: of its
 * functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Clears the time code's and the ES message's lengths, es_ascii and
 * es_enabled, and sets the :DIAGnostic:TCODe masks to 0xff and 0
 * (0x1420a). */
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

/* The GPT ticks after the 1PPS capture at which timecode_update and
 * es_build send their messages (0x141f6, 0x13f30). */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* Asks for a time code of a format unless channel B is released for one
 * already: the format kept and the channel released, its result; 0 for
 * a request refused (0x13cf6). */
char tcode_request(timecode_format format) {
  if (drtb_timecode_released() != 0) {
    return 0;
  }
  tcode_format = format;
  return drtb_release_for_timecode();
}

/* The ES message's status bits for a date, and the GPS-UTC offset, 0
 * when there is none (0x13d3a). */
es_status_bit es_status(int16_t* offset, uint32_t date) {
  es_status_bit status = 0;

  if (!get_gps_utc_offset(offset)) {
    *offset = 0;
    status |= ES_STATUS_TIME_INVALID;
  }
  if (!time_valid()) {
    status |= ES_STATUS_TIME_INVALID;
  }
  if (nv_settings.leap_pending && nv_settings.leap_date == date) {
    status |= ES_STATUS_LEAP_PENDING;
    if (nv_settings.leap_minute_length > CLOCK_SECONDS_PER_MINUTE) {
      status |= ES_STATUS_LEAP_LONGER;
    }
  }
  if (pll.stage == PLL_STAGE_HOLDOVER ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    status |= ES_STATUS_HOLDOVER;
  }
  return status;
}

/* A two-digit value's tens and ones. */
static uint8_t timecode_tens(uint32_t value) { return value / 10; }

static uint8_t timecode_ones(uint32_t value) {
  return value - timecode_tens(value) * 10;
}

/* Builds es_message for a date and time and schedules it: the year, day of
 * the year, time and GPS-UTC offset, as one digit a byte or, while
 * es_ascii, as text; es_status's bits; and status_summary.  The offset's
 * tens are taken of it as unsigned, as the image takes them (0x13daa). */
void es_build(uint32_t date, uint32_t time) {
  /* The ES message as digits, one a byte, and as ASCII. */
  static const uint8_t ES_DIGITS_LENGTH = 16;
  static const uint8_t ES_ASCII_LENGTH = 18;
  static const uint32_t CENTURY = 100;
  int16_t offset;

  es_length = 0;
  const uint16_t status = es_status(&offset, date);
  const uint8_t summary = status_summary;
  const uint16_t day = day_of_year(date);
  const uint8_t hour = time >> 16;
  const uint8_t minute = time >> 8;
  const uint8_t second = time;
  const uint16_t year = date >> 16;

  if (es_ascii != 0) {
    sprintf(es_message, "%02d%03d%02d%02d%02d%02d%02X%02X\r",
            (int32_t)year % (int32_t)CENTURY, (uint32_t)day, (uint32_t)hour,
            (uint32_t)minute, (uint32_t)second, (int32_t)offset,
            (uint32_t)status, (uint32_t)summary);
    es_length = ES_ASCII_LENGTH;
  } else {
    const uint32_t year_ones = (int32_t)year % (int32_t)CENTURY;
    const uint32_t day_ones = day % CENTURY;

    es_message[0] = timecode_tens(year_ones);
    es_message[1] = timecode_ones(year_ones);
    es_message[2] = day / CENTURY;
    es_message[3] = timecode_tens(day_ones);
    es_message[4] = timecode_ones(day_ones);
    es_message[5] = timecode_tens(hour);
    es_message[6] = timecode_ones(hour);
    es_message[7] = timecode_tens(minute);
    es_message[8] = timecode_ones(minute);
    es_message[9] = timecode_tens(second);
    es_message[10] = timecode_ones(second);
    es_message[11] = timecode_tens((uint32_t)offset);
    es_message[12] = timecode_ones((uint32_t)offset);
    es_message[13] = status;
    es_message[14] = summary;
    es_message[15] = '\r';
    es_length = ES_DIGITS_LENGTH;
  }
  es_schedule(TIMECODE_DELAY);
}

/* Whether an output compare's match, compare, is past: the timer's count,
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
 * already past sends it at once, while channel B is released for the
 * time code, or takes one wrap off (0x142f0). */
void timecode_schedule(uint32_t delay) {
  timecode_compare_wraps = delay >> 16;
  uint32_t compare = gpt.tic[0] + (delay & 0xffff);

  gpt.toc[2] = compare;
  gpt.tflg &= ~TFLG_OCF3;
  if (timecode_compare_passed(compare)) {
    if (timecode_compare_wraps == 0) {
      if (drtb_timecode_released() != 0) {
        drtb_send_async(tcode_message, tcode_length);
      }
      return;
    }
    timecode_compare_wraps--;
    gpt.tflg &= ~TFLG_OCF3;
  }
  gpt.tmsk |= TMSK_OCI3;
}

/* The same for the ES message, output compare 4, es_compare_wraps and
 * sci_send_async (0x143c0). */
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

/* The done hook of :SOURce:PTIMe:TCODe:CONTinuous's setting descriptor
 * (0x361be): the SCI's asynchronous sends follow the setting
 * (0x14240). */
void tcode_continuous_done(void) { sci_async_allowed_update(); }

/* Each 1PPS: while the time code is continuous and the es output is
 * on, builds the next on even seconds; then, once the SCPI task has
 * released channel B, sends the time code (0x13f44). */
void timecode_pps_send(void) {
  if (tcode_continuous != 0) {
    const uint32_t time = get_clock_time();

    if (es_enabled != 0 && (time & 1) == 0) {
      es_build(get_clock_date(), time);
    }
  }
  if (drtb_timecode_released() != 0) {
    timecode_update((char*)&tcode_format, 0);
  }
}

/* The time code's checksum, two digits, its line's end and a NUL, the
 * original's 5 bytes: an array size, so #define. */
#define TIMECODE_CHECKSUM_SIZE 5

/* The time code's checksum and line end appended; its length. */
static uint8_t timecode_finish(char* text) {
  char checksum[TIMECODE_CHECKSUM_SIZE];
  uint8_t length = 0;

  sprintf(checksum, "%02X\r\n", (uint8_t)tcode_checksum(text));
  strcat(text, checksum);
  while (text[length] != 0) {
    length++;
  }
  return length;
}

/* Builds a request's time code and schedules its send: T1 of the GPS
 * seconds at the next 1PPS or T2 of the clock's next second, with its
 * t, f, l, r and v, its checksum and a line end; a PREBUILT request's
 * text is left, at the length given; any other format's is a bare line
 * end (0x13f94).  T2's test prefix goes by the format too. */
void timecode_update(char* param_1, char param_2) {
  timecode_request* request = (timecode_request*)param_1;
  uint8_t length = (uint8_t)param_2;

  request->length = 0;
  /* tfom_code takes the float's bits. */
  union {
    float value;
    uint32_t bits;
  } tfom = {.value = pll.tfom};
  uint8_t tfom_digit = (uint8_t)tfom_code(tfom.bits);
  frequency_figure_of_merit ffom;

  get_ffom(&ffom);
  char leap = '0';

  if (nv_settings.leap_pending) {
    leap =
        nv_settings.leap_minute_length == CLOCK_LEAP_MINUTE_SHORT ? '-' : '+';
  }
  char service = status_request_service ? '1' : '0';
  char valid = time_valid() ? '0' : '1';

  switch (request->format) {
    case TIMECODE_T1: {
      uint32_t gps_date = get_gps_date();
      uint32_t gps_time = get_gps_time();

      sprintf(request->text, "T1#H%08X%01d%01d%1c%1c%1c",
              gps_seconds(gps_date, gps_time) + 1, (uint32_t)tfom_digit,
              (uint32_t)ffom, (int32_t)leap, (int32_t)service, (int32_t)valid);
      length = timecode_finish(request->text);
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

      if (request->format == TIMECODE_T2_TEST) {
        sprintf(request->text,
                "rtcm? %u,T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
                0xffffffff, year, month, day, (uint32_t)next.hour,
                (uint32_t)next.minute, (uint32_t)next.second,
                (uint32_t)tfom_digit, (uint32_t)ffom, (int32_t)leap,
                (int32_t)service, (int32_t)valid);
      } else {
        sprintf(request->text, "T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
                year, month, day, (uint32_t)next.hour, (uint32_t)next.minute,
                (uint32_t)next.second, (uint32_t)tfom_digit, (uint32_t)ffom,
                (int32_t)leap, (int32_t)service, (int32_t)valid);
      }
      length = timecode_finish(request->text);
      break;
    }
    case TIMECODE_PREBUILT:
      break;
    default:
      libc_copy(request->text, "\r\n");
      length = sizeof "\r\n" - 1;
      break;
  }
  request->length = length;
  timecode_schedule(TIMECODE_DELAY);
}
