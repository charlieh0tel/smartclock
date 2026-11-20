/* From the Z3815A 4010's rewritten/gps_nmea.c, the Z3816A 4001's own beside
 * common/'s: of its functions, those the Z3816A's code carries (identical or
 * relocated by image_diff, or changed with the same instructions), with the
 * static helpers they use, each the same here but where its comment says
 * otherwise (docs/firmware/z3816a-4001.md, "The link check").  Its comments'
 * addresses are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* An angle's hemisphere, 'N' or 'S' for a latitude, 'E' or 'W' for a
 * longitude, by its sign (0x4ba6e). */
uint32_t nmea_hemisphere(int32_t mas, char longitude) {
  if (longitude == 0) {
    return mas < 0 ? 'S' : 'N';
  }
  return mas < 0 ? 'W' : 'E';
}

/* A field's character: a letter, digit, punctuation or space, but none
 * of NMEA's reserved "$*,!\^~" (0x54f74). */
uint32_t nmea_field_char(uint8_t c) {
  static const uint8_t PRINTING = LIBC_CTYPE_UPPER | LIBC_CTYPE_LOWER |
                                  LIBC_CTYPE_DIGIT | LIBC_CTYPE_PUNCTUATION |
                                  LIBC_CTYPE_BLANK;

  if ((ctype_table[c + 1] & PRINTING) == 0) {
    return 0;
  }
  for (const char* reserved = "$*,!\\^~"; *reserved != c; reserved++) {
    if (*reserved == '\0') {
      return 1;
    }
  }
  return 0;
}

/* The length of the field at text: its field characters to the first
 * that is not one (0x54fa6). */
int32_t nmea_field_length(uint8_t* text) {
  uint8_t* end = text;

  while (*end != '\0' && nmea_field_char(*end) != 0) {
    end++;
  }
  return end - text;
}

/* 1 if two fields are the same and not empty, each of the length
 * given, or of nmea_field_length's for a negative one (0x54fca). */
uint32_t nmea_field_equal(uint8_t* a, uint16_t a_length, uint8_t* b,
                          uint16_t b_length) {
  int16_t left = a_length;

  if (left < 0) {
    left = (uint8_t)nmea_field_length(a);
  }
  if ((int16_t)b_length < 0) {
    b_length = (uint8_t)nmea_field_length(b);
  }
  if (left != (int16_t)b_length || left == 0) {
    return 0;
  }
  while (*a == *b) {
    if (--left <= 0) {
      return 1;
    }
    a++;
    b++;
  }
  return 0;
}

/* Whether a height sent, four bytes high first, is one the Oncore
 * takes, -100000 to 1800000 cm, and within 2 cm of the one it answered:
 * 1 if so, else 0 (0x55030).  gps_response_match calls it for @@Af's and
 * @@Au's heights and @@As's held one (0x56544, 0x5656e; gps_oncore.c), where
 * the 58503A 3704's makes the test in line, for its @@As echo. */
uint32_t oncore_hold_heights_match(uint8_t* sent, uint8_t* response) {
  /* The heights the Oncore takes, in cm (VPCommands.pdf, Position Hold
   * Position), and how far the answer may be from the height sent. */
  static const int32_t HOLD_HEIGHT_LOWEST = -100000;
  static const int32_t HOLD_HEIGHT_HIGHEST = 1800000;
  static const int32_t HOLD_HEIGHT_TOLERANCE = 2;
  int32_t sent_height;
  int32_t response_height;

  /* bytes_to_long takes the long as unsigned. */
  bytes_to_long(sent, (uint32_t*)&sent_height);
  bytes_to_long(response, (uint32_t*)&response_height);
  return sent_height >= HOLD_HEIGHT_LOWEST &&
         sent_height <= HOLD_HEIGHT_HIGHEST &&
         labs(sent_height - response_height) <= HOLD_HEIGHT_TOLERANCE;
}

/* Whether the leap second tps announces is this month's: the time, two
 * hours back, before the leap's, and in its month, or the month before
 * for a leap on a 1st, which ends the month before (0x557f6). */
uint32_t nmea_leap_second_due(oncore_date_fields date, time_of_day time,
                              oncore_date_fields leap_date,
                              time_of_day leap_time) {
  static const uint8_t TWO_HOURS = 2;
  static const uint8_t FIRST_DAY = 1;
  packed_datetime now = {.time = time, .date = oncore_date_pack(date)};
  packed_datetime leap = {.time = leap_time,
                          .date = oncore_date_pack(leap_date)};
  packed_datetime back = {.time = {.hour = TWO_HOURS}, .date = 0};

  datetime_subtract(&back, &now);
  if (datetime_compare((uint32_t*)&now, (uint32_t*)&leap) >= 0) {
    return 0;
  }
  uint16_t year = now.date >> (2 * RECORDS_FIELD_BITS);
  uint8_t month = now.date >> RECORDS_FIELD_BITS;

  if ((uint8_t)leap.date == FIRST_DAY && ++month > CLOCK_DECEMBER) {
    month = 1;
    year++;
  }
  return month == (uint8_t)(leap.date >> RECORDS_FIELD_BITS) &&
         year == (uint16_t)(leap.date >> (2 * RECORDS_FIELD_BITS));
}
