/* From the Z3815A 4010's and the 58503B 1.01.04's rewritten/gps_nmea.c,
 * the Z3805A 1.01.03's own beside common/'s: of their functions, those the
 * Z3805A 1.01.03's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list").  Its comments'
 * addresses are the Z3805A 1.01.03's, but where they name another image.
 * Each keeps its generated prototype. */

/* An NMEA angle's degrees' digits: a latitude's, then a longitude's. */
static const int32_t GPS_NMEA_LATITUDE_DIGITS = 2;

static const int32_t GPS_NMEA_LONGITUDE_DIGITS = 3;

/* Copies a field's characters to out, to its NUL; past them. */
static char* gps_nmea_copy(const char* field, char* out) {
  while (*field != '\0') {
    *out++ = *field++;
  }
  return out;
}

/* Copies length characters of a field to out; past them. */
static char* gps_nmea_copy_length(const char* field, uint8_t length,
                                  char* out) {
  for (uint8_t i = 0; i < length; i++) {
    *out++ = field[i];
  }
  return out;
}

/* An angle as an NMEA field, its degrees, two digits for a latitude and
 * three for a longitude, and its minutes to 0.0001, then a comma and its
 * hemisphere, which takes the NUL's place; its length (0x493c2). */
uint8_t nmea_angle_format(int32_t mas, char longitude, char* text) {
  int32_t rest = labs(mas);
  const uint16_t degrees = libc_divide(&rest, GPS_MAS_PER_DEGREE);
  const float minutes =
      float_divide(integer_to_float(rest), (float)GPS_MAS_PER_MINUTE);
  const int8_t digits =
      longitude == 0 ? GPS_NMEA_LATITUDE_DIGITS : GPS_NMEA_LONGITUDE_DIGITS;

  sprintf(text, "%0*d%07.4f,", (int32_t)digits, (uint32_t)degrees,
          float_to_double(minutes));
  const uint8_t length = libc_length(text);

  text[length] = (char)nmea_hemisphere(mas, longitude);
  return length + 1;
}

/* The argument at index, an angle in mas, as GLL's latitude or
 * longitude and its hemisphere. */
static char* gps_nmea_encode_angle(int32_t* arguments, uint8_t index,
                                   char longitude, char* out) {
  char field[14];
  const uint8_t length =
      nmea_angle_format(long_array_get(arguments, index), longitude, field);

  return gps_nmea_copy_length(field, length, out);
}

/* GLL's latitude (0x52a7c). */
char* nmea_encode_latitude(int32_t* arguments, uint8_t index, char* out) {
  return gps_nmea_encode_angle(arguments, index, 0, out);
}

/* GLL's longitude (0x52acc). */
char* nmea_encode_longitude(int32_t* arguments, uint8_t index, char* out) {
  return gps_nmea_encode_angle(arguments, index, 1, out);
}

/* The argument at index, a height in cm, less gps.gga.height_offset unless
 * the next argument is set, in m as "%+08.1f", its + made a 0
 * (0x52b1e). */
char* nmea_encode_height(int32_t* arguments, uint8_t index, char* out) {
  static const double CM_PER_M = 100.0;
  char field[10];
  int32_t height = long_array_get(arguments, index);

  if ((uint8_t)long_array_get(arguments, index + 1) == 0) {
    height -= gps.gga.height_offset;
  }
  sprintf(field, "%+08.1f",
          double_divide_reversed(CM_PER_M, integer_to_double(height)));
  if (field[0] == '+') {
    field[0] = '0';
  }
  return gps_nmea_copy(field, out);
}

/* A decoder's place in the text: a field's two digits, and the
 * date's and the time's three of them. */
static const uint8_t NMEA_PAIR = 2;

static const uint8_t NMEA_PAIRS = 3;

/* Three pairs of digits from text into parts, to the first not good,
 * which is the status; text past those read. */
static nmea_field_status gps_nmea_pairs(char** text, uint8_t* parts) {
  nmea_field_status status = NMEA_FIELD_GOOD;

  for (uint8_t i = 0; i < NMEA_PAIRS && status == NMEA_FIELD_GOOD; i++) {
    parts[i] = nmea_2_digits_parse(*text, &status);
    *text += NMEA_PAIR;
  }
  return status;
}

/* A date, yymmdd, and a time, hhmmss, each stored if its digits are good
 * and, to validate, in range: years 80 to 99 of 1900 and 0 to 40 of
 * 2000.  The date's status in the high four bits, the time's in the low,
 * each 0 for one stored or out of range (0x52f5c). */
uint8_t nmea_date_time_parse(char* text, char validate,
                             oncore_date_fields* date, time_of_day* time) {
  static const uint8_t STATUS_BITS = 4;
  static const uint8_t CENTURY_FIRST = 80;
  static const uint8_t CENTURY_LAST = 99;
  static const uint8_t NEXT_CENTURY_LAST = 40;
  static const uint16_t CENTURY = 1900;
  static const uint16_t NEXT_CENTURY = 2000;
  static const uint8_t MONTHS = 12;
  static const uint8_t DAYS = 31;
  static const uint8_t DAY_HOURS = 24;
  static const uint8_t HOUR_MINUTES = 60;
  static const uint8_t LEAP_SECOND = 60;
  uint8_t parts[3];
  uint8_t result = 0;
  nmea_field_status status = gps_nmea_pairs(&text, parts);

  if (status == NMEA_FIELD_GOOD &&
      (validate == 0 ||
       (((parts[0] >= CENTURY_FIRST && parts[0] <= CENTURY_LAST) ||
         parts[0] <= NEXT_CENTURY_LAST) &&
        parts[1] >= 1 && parts[1] <= MONTHS && parts[2] >= 1 &&
        parts[2] <= DAYS))) {
    date->year =
        parts[0] + (parts[0] <= NEXT_CENTURY_LAST ? NEXT_CENTURY : CENTURY);
    date->month = parts[1];
    date->day = parts[2];
  } else {
    result = status << STATUS_BITS;
  }
  status = gps_nmea_pairs(&text, parts);
  if (status == NMEA_FIELD_GOOD &&
      (validate == 0 || (parts[0] < DAY_HOURS && parts[1] < HOUR_MINUTES &&
                         parts[2] <= LEAP_SECOND))) {
    time->hour = parts[0];
    time->minute = parts[1];
    time->second = parts[2];
  } else {
    result |= status;
  }
  return result;
}

/* tps's date and time, validated (0x53096). */
void nmea_decode_tps_date_time(char* field) {
  nmea_date_time_parse(field, 1, &gps.tps.date, &gps.tps.time);
}

/* tps's leap second's date and time (0x530b4). */
void nmea_decode_tps_leap_date_time(char* field) {
  nmea_date_time_parse(field, 0, &gps.tps.leap_date, &gps.tps.leap_time);
}

/* tps's ninth field's date and time (0x530d0). */
void nmea_decode_tps_date_time_9(char* field) {
  nmea_date_time_parse(field, 0, &gps.tps.date_9, &gps.tps.time_9);
}

/* anc's date and time, validated (0x5344a). */
void nmea_decode_anc_date_time(char* field) {
  nmea_date_time_parse(field, 1, &gps.anc.date, &gps.anc.time);
}

/* An angle's hemisphere, 'N' or 'S' for a latitude, 'E' or 'W' for a
 * longitude, by its sign (0x492f6). */
uint32_t nmea_hemisphere(int32_t mas, char longitude) {
  if (longitude == 0) {
    return mas < 0 ? 'S' : 'N';
  }
  return mas < 0 ? 'W' : 'E';
}

/* A field's character: a letter, digit, punctuation or space, but none
 * of NMEA's reserved "$*,!\^~" (0x51dd4). */
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
 * that is not one (0x51e06). */
int32_t nmea_field_length(uint8_t* text) {
  uint8_t* end = text;

  while (*end != '\0' && nmea_field_char(*end) != 0) {
    end++;
  }
  return end - text;
}

/* 1 if two fields are the same and not empty, each of the length
 * given, or of nmea_field_length's for a negative one (0x51e2a). */
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

/* Whether an @@As position hold height sent, four bytes high first, is
 * one the Oncore takes, -100000 to 1800000 cm, and within 2 cm of the
 * one it answered: 1 if so, else 0 (0x51e90).  The 58503A 3704's
 * gps_response_match makes the test in line, for its @@As echo
 * (gps_oncore.c); nothing calls this one. */
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
 * for a leap on a 1st, which ends the month before (0x52672). */
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

/* An empty field: out, the only argument it reads, as the Z3815A's
 * (0x52cdc). */
uint32_t nmea_encode_nothing(uint32_t out) { return out; }

/* The framer's room: a sentence's bytes, those framed compared with it
 * as a byte. */
static const uint8_t NMEA_FRAME_ROOM = sizeof((nmea_frame*)0)->text;

/* A long's hexadecimal digits, set,U's satellite mask's, and a digit's
 * bits. */
static const int8_t GPS_NMEA_HEX_DIGITS = 8;

static const uint32_t GPS_NMEA_NIBBLE_MASK = 0xf;

static const uint32_t GPS_NMEA_NIBBLE_BITS = 4;

/* A decimal value's base, and a hexadecimal one's. */
static const int32_t NMEA_DECIMAL = 10;

static const int32_t NMEA_HEXADECIMAL = 16;

/* Whether a character is a decimal digit, by the ctype table. */
static bool gps_nmea_digit(char c) {
  return (ctype_table[(int8_t)c + 1] & LIBC_CTYPE_DIGIT) != 0;
}

/* The digits after a field's '.', counted to the field's end. */
static int32_t gps_nmea_decimals(const char* point) {
  return (int32_t)libc_length(point + 1);
}

/* nmea_field_equal's length for a field to be measured. */
static const uint16_t NMEA_FIELD_MEASURED = 0xffff;

/* The bytes framed so far, as the framer compares them, a byte. */
static uint8_t gps_nmea_framed(const nmea_frame* frame) {
  return (uint8_t)((char*)frame->cursor - frame->text);
}

/* Whether two names are the same, both ended together, as the image
 * compares them in line. */
static bool gps_nmea_same_name(const char* a, const char* b) {
  while ((*a | *b) != 0) {
    if (*a++ != *b++) {
      return false;
    }
  }
  return true;
}

/* Where the checksum's '*' is, read before the test as the image does,
 * so that a bad frame faults in each copy alike. */
static uint8_t* gps_nmea_checksum_end(nmea_frame* frame) {
  const uint8_t digits = *(volatile uint8_t*)&frame->checksum_digits;
  char* const cursor = *(char* volatile*)&frame->cursor;

  return (uint8_t*)cursor - digits - 2;
}

/* A framer state's transition: the first whose predicate takes the
 * character runs its action, if any, and moves to its state; each
 * state's last takes any character. */
typedef uint8_t nmea_frame_predicate(uint8_t c, nmea_frame* frame);

typedef void nmea_frame_action(uint8_t c, nmea_frame* frame);

typedef struct nmea_frame_transition {
  nmea_frame_predicate* test;
  nmea_frame_action* action; /* none for NULL */
  gps_framing_state next;
} nmea_frame_transition;

/* A '$' starts a sentence in every state. */
#define NMEA_FRAME_ON_START(action)                                        \
  {(nmea_frame_predicate*)nmea_frame_is_start, (nmea_frame_action*)action, \
   GPS_FRAMING_FIELD}

/* A character no other transition takes: a frame error, and a wait for
 * the next '$'. */
#define NMEA_FRAME_OTHERWISE                                      \
  {(nmea_frame_predicate*)nmea_frame_any, nmea_frame_store_error, \
   GPS_FRAMING_IDLE}

/* The states' transitions, the image's lists at 0x54a48 to 0x54a90 by
 * its table at 0x54aa0, each predicate and action by index into its
 * tables at 0x54ab4 and 0x54adc. */
static const nmea_frame_transition gps_nmea_idle[] = {
    NMEA_FRAME_ON_START(nmea_frame_start),
    {(nmea_frame_predicate*)nmea_frame_any, NULL, GPS_FRAMING_IDLE}};

static const nmea_frame_transition gps_nmea_field[] = {
    NMEA_FRAME_ON_START(nmea_frame_restart),
    {(nmea_frame_predicate*)nmea_frame_field_char, nmea_frame_store,
     GPS_FRAMING_FIELD},
    {(nmea_frame_predicate*)nmea_frame_is_comma, nmea_frame_field_next,
     GPS_FRAMING_FIELD_START},
    {(nmea_frame_predicate*)nmea_frame_is_star, nmea_frame_field_end,
     GPS_FRAMING_CHECKSUM},
    {(nmea_frame_predicate*)nmea_frame_is_cr, nmea_frame_field_end,
     GPS_FRAMING_LINE_END},
    NMEA_FRAME_OTHERWISE};

static const nmea_frame_transition gps_nmea_field_start[] = {
    NMEA_FRAME_ON_START(nmea_frame_start),
    {(nmea_frame_predicate*)nmea_frame_field_char, nmea_frame_store,
     GPS_FRAMING_FIELD_START},
    {(nmea_frame_predicate*)nmea_frame_is_comma, nmea_frame_field_next,
     GPS_FRAMING_FIELD_START},
    {(nmea_frame_predicate*)nmea_frame_is_star, nmea_frame_field_end,
     GPS_FRAMING_CHECKSUM},
    {(nmea_frame_predicate*)nmea_frame_is_cr, nmea_frame_field_end,
     GPS_FRAMING_LINE_END},
    NMEA_FRAME_OTHERWISE};

static const nmea_frame_transition gps_nmea_checksum[] = {
    NMEA_FRAME_ON_START(nmea_frame_start),
    {(nmea_frame_predicate*)nmea_frame_checksum_digit,
     nmea_frame_checksum_store, GPS_FRAMING_CHECKSUM},
    {(nmea_frame_predicate*)nmea_frame_is_cr_after_checksum,
     nmea_frame_field_end, GPS_FRAMING_LINE_END},
    NMEA_FRAME_OTHERWISE};

static const nmea_frame_transition gps_nmea_line_end[] = {
    NMEA_FRAME_ON_START(nmea_frame_start),
    {(nmea_frame_predicate*)nmea_frame_is_lf_good, nmea_frame_store,
     GPS_FRAMING_COMPLETE},
    {(nmea_frame_predicate*)nmea_frame_is_lf_bad, nmea_frame_store_bad_checksum,
     GPS_FRAMING_IDLE},
    NMEA_FRAME_OTHERWISE};

static const nmea_frame_transition* const gps_nmea_states[] = {
    gps_nmea_idle, gps_nmea_field, gps_nmea_field_start, gps_nmea_checksum,
    gps_nmea_line_end};

/* A sentence as text, its CR LF shown as "<cr><lf>", or empty unless it
 * ends with them; the text's length (0x5289a). */
int32_t nmea_sentence_text(char* sentence, uint8_t length, char* text) {
  const int32_t body = (int32_t)length - 2;

  if (sentence[0] != '\0' && sentence[body] == '\r' &&
      sentence[body + 1] == '\n') {
    memcpy(text, sentence, body);
    text[body] = '\0';
    strcat(text, "<cr><lf>");
  } else {
    text[0] = '\0';
  }
  return libc_length(text);
}

/* A value to digits digits, "%.*0d", or with its sign, "%+.*0d"
 * (0x52908). */
void nmea_decimal_format(int32_t value, char sign, uint8_t digits, char* text) {
  sprintf(text, sign == 0 ? "%.*0d" : "%+.*0d", (uint32_t)digits, value);
}

/* A long as eight uppercase hexadecimal digits and a NUL (0x5293c). */
void nmea_hex_format(uint32_t value, char* text) {
  static const uint8_t DECIMAL = 10;

  text[GPS_NMEA_HEX_DIGITS] = '\0';
  for (int8_t i = GPS_NMEA_HEX_DIGITS - 1; i >= 0; i--) {
    const uint8_t digit = value & GPS_NMEA_NIBBLE_MASK;

    text[i] = digit + (digit < DECIMAL ? '0' : 'A' - DECIMAL);
    value >>= GPS_NMEA_NIBBLE_BITS;
  }
}

/* The argument at index to digits digits, of which the first digits
 * characters go to out (0x52982). */
char* nmea_encode_digits(int32_t* arguments, uint8_t index, uint8_t digits,
                         char* out) {
  char field[12];

  nmea_decimal_format(long_array_get(arguments, index), 0, digits, field);
  return gps_nmea_copy_length(field, digits, out);
}

/* The argument at index as one digit (0x529d4). */
char* nmea_encode_digit(int32_t* arguments, uint8_t index, char* out) {
  *out++ = (char)long_array_get(arguments, index) + '0';
  return out;
}

/* The argument at index to two digits (0x529f8). */
char* nmea_encode_2_digits(int32_t* arguments, uint8_t index, char* out) {
  return nmea_encode_digits(arguments, index, 2, out);
}

/* The argument at index to six digits, ZDA's time (0x52a12). */
char* nmea_encode_6_digits(int32_t* arguments, uint8_t index, char* out) {
  return nmea_encode_digits(arguments, index, 6, out);
}

/* The argument at index's low word as ZDA's year, and its zone, zero
 * (0x52a2c). */
char* nmea_encode_year(int32_t* arguments, uint8_t index, char* out) {
  char field[12];

  sprintf(field, "%04d,+00,00",
          (uint32_t)(uint16_t)long_array_get(arguments, index));
  return gps_nmea_copy(field, out);
}

/* The argument at index as eight hexadecimal digits, set,U's satellite
 * mask (0x52bc0). */
char* nmea_encode_hex(int32_t* arguments, uint8_t index, char* out) {
  char field[10];

  nmea_hex_format(long_array_get(arguments, index), field);
  return gps_nmea_copy_length(field, GPS_NMEA_HEX_DIGITS, out);
}

/* The argument at index as one digit counted from 1 (0x52c06). */
char* nmea_encode_digit_from_1(int32_t* arguments, uint8_t index, char* out) {
  *out++ = (char)long_array_get(arguments, index) + '1';
  return out;
}

/* The argument at index, the cable delay, and the engine's correction,
 * negated, as "%+07d" (0x52c2a). */
char* nmea_encode_cable_delay(int32_t* arguments, uint8_t index, char* out) {
  char field[8];

  sprintf(field, "%+07d",
          -(long_array_get(arguments, index) + gps.cable_delay_correction));
  return gps_nmea_copy(field, out);
}

/* The argument at index times 10, to three digits; the argument is put
 * back as the product divided by 10, unsigned (0x52c82). */
char* nmea_encode_tenths(int32_t* arguments, uint8_t index, char* out) {
  static const uint32_t TEN = 10;
  const int32_t value = long_array_get(arguments, index);
  const uint32_t product = (uint32_t)value * TEN;

  long_array_set(product, arguments, index);
  char* past = nmea_encode_digits(arguments, index, 3, out);

  long_array_set(product / TEN, arguments, index);
  return past;
}

/* The value of a field in decimal, its first length characters or, for
 * 0, all of it, the character after put back; 0, and status
 * NMEA_FIELD_EMPTY for an empty field and NMEA_FIELD_BAD for one that is
 * not all digits (0x52ce2). */
int32_t nmea_decimal_parse(char* text, uint8_t length,
                           nmea_field_status* status) {
  char saved;
  char* end;

  if (*text == '\0') {
    *status = NMEA_FIELD_EMPTY;
    return 0;
  }
  if (length != 0) {
    saved = text[length];
    text[length] = '\0';
  }
  int32_t value = strtol(text, &end, NMEA_DECIMAL);

  if ((int32_t)(uint8_t)libc_length(text) > end - text) {
    *status = NMEA_FIELD_BAD;
    value = 0;
  } else {
    *status = NMEA_FIELD_GOOD;
  }
  if (length != 0) {
    text[length] = saved;
  }
  return value;
}

/* Two decimal digits' value; 0, and NMEA_FIELD_EMPTY for an empty field
 * and NMEA_FIELD_BAD for any other not two digits (0x52d64). */
uint8_t nmea_2_digits_parse(char* text, nmea_field_status* status) {
  if (!gps_nmea_digit(text[0]) || !gps_nmea_digit(text[1])) {
    *status = text[0] == '\0' ? NMEA_FIELD_EMPTY : NMEA_FIELD_BAD;
    return 0;
  }
  *status = NMEA_FIELD_GOOD;
  return (text[0] - '0') * NMEA_DECIMAL + (text[1] - '0');
}

/* A hexadecimal digit's value, either case; 0, and NMEA_FIELD_EMPTY for
 * any other character (0x52dc0). */
uint8_t nmea_hex_digit_parse(char* text, nmea_field_status* status) {
  static const uint8_t LOWER_CASE = 'a' - 'A';

  if ((ctype_table[(int8_t)text[0] + 1] & LIBC_CTYPE_HEX) == 0) {
    *status = NMEA_FIELD_EMPTY;
    return 0;
  }
  *status = NMEA_FIELD_GOOD;
  const uint8_t c = text[0];

  if (c <= '9') {
    return c - '0';
  }
  return (c & ~LOWER_CASE) - ('A' - NMEA_DECIMAL);
}

/* The field's character (0x52e12). */
nmea_field_status nmea_decode_char(char* field, char* out) {
  if (out != NULL) {
    *out = *field;
  }
  return NMEA_FIELD_GOOD;
}

/* A hexadecimal digit (0x52e26). */
nmea_field_status nmea_decode_hex_digit(char* field, uint8_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;

  if (out != NULL) {
    const uint8_t value = nmea_hex_digit_parse(field, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
  }
  return status;
}

/* A digit, unchecked (0x52e5e). */
nmea_field_status nmea_decode_digit(char* field, uint8_t* out) {
  if (out != NULL) {
    *out = *field - '0';
  }
  return NMEA_FIELD_GOOD;
}

/* Two digits (0x52e76). */
nmea_field_status nmea_decode_2_digits(char* field, uint8_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;

  if (out != NULL) {
    const uint8_t value = nmea_2_digits_parse(field, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
  }
  return status;
}

/* A decimal value, a byte of it (0x52eae). */
nmea_field_status nmea_decode_byte(char* field, uint8_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;

  if (out != NULL) {
    const uint8_t value = nmea_decimal_parse(field, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
  }
  return status;
}

/* A decimal value, a word of it (0x52eea). */
nmea_field_status nmea_decode_word(char* field, uint16_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;

  if (out != NULL) {
    const uint16_t value = nmea_decimal_parse(field, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
  }
  return status;
}

/* A decimal value (0x52f26). */
nmea_field_status nmea_decode_long(char* field, int32_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;

  if (out != NULL) {
    const int32_t value = nmea_decimal_parse(field, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
  }
  return status;
}

/* count fields of two digits, each a GPS satellite added to the set if
 * valid; the set is stored unless one is bad (0x530ec). */
nmea_field_status nmea_satellites_parse(char* text, uint8_t count,
                                        uint32_t* out) {
  nmea_field_status status = NMEA_FIELD_EMPTY;
  uint32_t set = 0;

  for (; count != 0; count--) {
    const uint8_t id = nmea_2_digits_parse(text, &status);

    if (status == NMEA_FIELD_BAD) {
      return status;
    }
    if (satellite_id_valid(SATELLITE_GPS, id) != 0) {
      satellite_add_to_set(SATELLITE_GPS, id, &set);
    }
    text += (uint8_t)libc_length(text) + 1;
  }
  *out = set;
  return status;
}

/* GSA's twelve satellites (0x53166). */
void nmea_decode_12_satellites(char* field, uint32_t* out) {
  static const uint8_t SATELLITES = 12;

  nmea_satellites_parse(field, SATELLITES, out);
}

/* rrm's four (0x5317c). */
void nmea_decode_4_satellites(char* field, uint32_t* out) {
  static const uint8_t SATELLITES = 4;

  nmea_satellites_parse(field, SATELLITES, out);
}

/* A value of two decimals in hundredths, the '.' moved past them and
 * back (0x53192). */
nmea_field_status nmea_decode_hundredths(char* field, uint16_t* out) {
  static const int32_t DECIMALS = 2;
  nmea_field_status status = NMEA_FIELD_EMPTY;
  char* end = field + (uint8_t)nmea_field_length((uint8_t*)field);
  const char saved = *end;

  *end = '\0';
  char* point = strchr(field, '.');

  if (point != NULL && gps_nmea_decimals(point) == DECIMALS) {
    point[0] = point[1];
    point[1] = point[2];
    point[2] = '\0';
    const uint16_t value = nmea_decimal_parse(field, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value;
    }
    point[2] = point[1];
    point[1] = point[0];
    point[0] = '.';
  }
  *end = saved;
  return status;
}

/* An angle, its degrees and its minutes to four decimals, ddmm.mmmm or
 * dddmm.mmmm, in mas; and GGA's angle_decoded set (0x53224). */
nmea_field_status nmea_decode_angle(char* field, int32_t* out) {
  static const int32_t DECIMALS = 4;
  static const int32_t MAS_PER_MINUTE_DECIMAL = GPS_MAS_PER_MINUTE / 10000;
  nmea_field_status status = NMEA_FIELD_EMPTY;
  char* end = field + (uint8_t)nmea_field_length((uint8_t*)field);
  const char saved = *end;

  *end = '\0';
  char* point = strchr(field, '.');

  if (point != NULL && gps_nmea_decimals(point) == DECIMALS) {
    point[0] = point[-1];
    point[-1] = point[-2];
    point[-2] = '\0';
    const int32_t minutes = nmea_decimal_parse(point - 1, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      const int32_t degrees = nmea_decimal_parse(field, 0, &status);

      if (status == NMEA_FIELD_GOOD) {
        *out = degrees * GPS_MAS_PER_DEGREE + minutes * MAS_PER_MINUTE_DECIMAL;
        gps.gga.angle_decoded = 1;
      }
    }
    point[-2] = point[-1];
    point[-1] = point[0];
    point[0] = '.';
  }
  *end = saved;
  return status;
}

/* A value of one decimal, in m, as cm (0x532ec). */
nmea_field_status nmea_decode_tenths(char* field, int32_t* out) {
  static const int32_t DECIMALS = 1;
  static const int32_t CM_PER_TENTH = 10;
  nmea_field_status status = NMEA_FIELD_EMPTY;
  char* end = field + (uint8_t)nmea_field_length((uint8_t*)field);
  const char saved = *end;

  *end = '\0';
  char* point = strchr(field, '.');

  if (point != NULL && gps_nmea_decimals(point) == DECIMALS) {
    point[0] = point[1];
    point[1] = '\0';
    const int32_t value = nmea_decimal_parse(field, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      *out = value * CM_PER_TENTH;
    }
    point[1] = point[0];
    point[0] = '.';
  }
  *end = saved;
  return status;
}

/* tst's software: the part number, its first seven digits, a long, and
 * the version, the rest, a word after it (0x5337a). */
nmea_field_status nmea_decode_software(char* field, uint32_t* out) {
  static const uint8_t PART_DIGITS = 7;
  nmea_field_status status;
  const uint32_t part = nmea_decimal_parse(field, PART_DIGITS, &status);

  if (status == NMEA_FIELD_GOOD) {
    const uint16_t version =
        nmea_decimal_parse(field + PART_DIGITS, 0, &status);

    if (status == NMEA_FIELD_GOOD) {
      out[0] = part;
      *(uint16_t*)&out[1] = version;
    }
  }
  return status;
}

/* ssd's height after its H (0x53518). */
nmea_field_status nmea_decode_ssd_height(char* field, int32_t* out) {
  if (*field != 'H') {
    return NMEA_FIELD_EMPTY;
  }
  return nmea_decode_tenths(field + 1, out);
}

/* ssd's mask angle after its M (0x53536). */
nmea_field_status nmea_decode_ssd_mask_angle(char* field, uint8_t* out) {
  if (*field != 'M') {
    return NMEA_FIELD_EMPTY;
  }
  return nmea_decode_2_digits(field + 1, out);
}

/* ssd's digit after its Z (0x53554). */
nmea_field_status nmea_decode_ssd_z(char* field, uint8_t* out) {
  if (*field != 'Z') {
    return NMEA_FIELD_EMPTY;
  }
  return nmea_decode_digit(field + 1, out);
}

/* ssd's ignored satellites after its U, hexadecimal (0x53572). */
bool nmea_decode_ssd_ignored(char* field, uint32_t* out) {
  const char letter = *field;

  if (letter == 'U') {
    *out = strtoul(field + 1, NULL, NMEA_HEXADECIMAL);
  }
  return letter != 'U';
}

/* ssd's cable delay after its t: kept as the engine gives it, and with
 * gps.cable_delay_correction (0x5359e). */
nmea_field_status nmea_decode_ssd_cable_delay(char* field, int32_t* out) {
  nmea_field_status status = NMEA_FIELD_GOOD;

  if (*field != 't' || out == NULL) {
    return NMEA_FIELD_EMPTY;
  }
  const int32_t delay = nmea_decimal_parse(field + 1, 0, &status);

  if (status == NMEA_FIELD_GOOD) {
    gps.pps_cable_delay_given = delay;
    *out = gps.cable_delay_correction + delay;
  }
  return status;
}

/* GSV's incoming satellites cleared (0x5360c). */
void nmea_gsv_incoming_clear(nmea_gsv_fields* gsv) {
  for (uint8_t i = 0; i < sizeof gsv->incoming / sizeof gsv->incoming[0]; i++) {
    gsv->incoming[i].satellite = 0;
  }
}

/* GSV's sentence number: the first restarts the satellites gathered
 * (0x53622). */
nmea_field_status nmea_decode_gsv_sentence(char* field, nmea_gsv_fields* gsv) {
  if (gsv != NULL) {
    gsv->sentence = *field - '0';
    if (gsv->sentence == 1) {
      gsv->count = 0;
      nmea_gsv_incoming_clear(gsv);
    }
  }
  return NMEA_FIELD_GOOD;
}

/* GSV's last decoder: the sentence's satellites gathered after those
 * before, each above the last gathered, the count with them; their place
 * cleared unless the list reaches it (0x5364a).  The list runs into
 * incoming, unbounded, as the image's does. */
nmea_field_status nmea_gsv_gather(char* field, nmea_gsv_fields* gsv) {
  nmea_gsv_satellite* in = gsv->incoming;
  nmea_gsv_satellite* out = &gsv->satellites[gsv->count];

  for (uint8_t i = 1; i <= sizeof gsv->incoming / sizeof gsv->incoming[0];
       i++, in++) {
    if (in->satellite != 0 &&
        (gsv->count == 0 || in->satellite > out[-1].satellite)) {
      if (in > out) {
        *out = *in;
      }
      out++;
      gsv->count++;
    }
  }
  if (out <= gsv->incoming) {
    nmea_gsv_incoming_clear(gsv);
  }
  return NMEA_FIELD_GOOD;
}

/* An angle's sign by its hemisphere: -1 for 'S' and 'W' (0x536d8). */
int8_t nmea_hemisphere_sign(char hemisphere) {
  return hemisphere == 'S' || hemisphere == 'W' ? -1 : 1;
}

/* The field at index in a framed sentence, past its NUL'ed commas: by
 * its offset for the first 19, counted along the text for the rest;
 * NULL past the fields framed (0x542a2). */
char* nmea_frame_field(int8_t index, nmea_frame* frame) {
  static const int8_t OFFSETS = 19;

  if (index >= (int8_t)frame->fields) {
    return NULL;
  }
  if (index < OFFSETS) {
    return (char*)frame + frame->field_starts[index];
  }
  char* field;
  char* next = (char*)frame;

  for (; index >= 0; index--) {
    field = next + 1;
    next = field + (uint8_t)libc_length(field);
  }
  return field;
}

/* A sentence's address, after its '$', an optional "PFEC" field and
 * "GP": its name, which must begin with a field character; NULL for
 * another (0x54238). */
char* nmea_sentence_address(nmea_frame* frame) {
  static const char PROPRIETARY[] = "PFEC";
  static const char TALKER[] = "GP";
  char* address = frame->text + 1;

  if (strncmp(address, PROPRIETARY, sizeof PROPRIETARY - 1) == 0 &&
      (address[sizeof PROPRIETARY - 1] == '\0' ||
       address[sizeof PROPRIETARY - 1] == ',')) {
    address += sizeof PROPRIETARY;
  }
  if (address != NULL && strncmp(address, TALKER, sizeof TALKER - 1) == 0 &&
      (uint8_t)nmea_field_char(address[sizeof TALKER - 1]) != 0) {
    return address + sizeof TALKER - 1;
  }
  return NULL;
}

/* Whether a sentence of fields commas is GSV's with up to 20, a multiple
 * of 4, or any other's (0x542f8). */
bool nmea_sentence_fields_allowed(nmea_sentence_entry* sentence,
                                  uint8_t fields) {
  static const uint8_t GSV_FIELDS = 20;
  static const uint8_t GSV_SATELLITE_FIELDS = 4;

  if (!gps_nmea_same_name("GSV", sentence->name)) {
    return true;
  }
  return fields <= GSV_FIELDS && fields % GSV_SATELLITE_FIELDS == 0;
}

/* Whether a framed sentence is the one named, its address stored
 * (0x54330). */
uint32_t nmea_sentence_is(nmea_frame* frame, uint8_t* name, uint8_t** address) {
  *address = (uint8_t*)nmea_sentence_address(frame);
  return *address != NULL &&
         (uint8_t)nmea_field_equal(*address, NMEA_FIELD_MEASURED, name,
                                   NMEA_FIELD_MEASURED) != 0;
}

/* The framer's predicate for any character (0x53ebc). */
uint32_t nmea_frame_any(void) { return 1; }

/* The framer's predicate for a sentence's start, '$' (0x53ec0). */
bool nmea_frame_is_start(char c) { return c == '$'; }

/* The framer's predicate for a field's character with room for it
 * (0x53ece). */
uint32_t nmea_frame_field_char(uint8_t c, nmea_frame* frame) {
  return gps_nmea_framed(frame) < NMEA_FRAME_ROOM && nmea_field_char(c) != 0;
}

/* The framer's predicate for the character expected, past the '$' and a
 * character more and with room left (0x53ef4). */
uint32_t nmea_frame_end_is(char c, char expected, nmea_frame* frame) {
  if (c != expected) {
    return 0;
  }
  const uint8_t framed = gps_nmea_framed(frame);

  return framed > 1 && framed < NMEA_FRAME_ROOM;
}

/* A field's end, ',' (0x53f22). */
uint32_t nmea_frame_is_comma(char c, nmea_frame* frame) {
  return nmea_frame_end_is(c, ',', frame);
}

/* The checksum's start, '*' (0x53f36). */
uint32_t nmea_frame_is_star(char c, nmea_frame* frame) {
  return nmea_frame_end_is(c, '*', frame);
}

/* A checksum digit: hexadecimal and uppercase, one of the two, with room
 * (0x53f4a). */
uint32_t nmea_frame_checksum_digit(uint8_t c, nmea_frame* frame) {
  static const uint8_t CHECKSUM_DIGITS = 2;

  if ((ctype_table[c + 1] & LIBC_CTYPE_HEX) == 0 || (c >= 'a' && c <= 'f') ||
      frame->checksum_digits >= CHECKSUM_DIGITS) {
    return 0;
  }
  return gps_nmea_framed(frame) < NMEA_FRAME_ROOM;
}

/* The sentence's CR, without a checksum (0x53f9c). */
uint32_t nmea_frame_is_cr(char c, nmea_frame* frame) {
  return nmea_frame_end_is(c, '\r', frame);
}

/* The sentence's CR after both checksum digits (0x53fb0). */
uint32_t nmea_frame_is_cr_after_checksum(char c, nmea_frame* frame) {
  static const uint8_t CHECKSUM_DIGITS = 2;

  return nmea_frame_end_is(c, '\r', frame) != 0 &&
         frame->checksum_digits == CHECKSUM_DIGITS;
}

/* Whether the sentence's checksum, if it has one, is the XOR of its
 * bytes between '$' and '*', end; the framer has put a NUL for each
 * comma, so an odd count of them makes it differ by a comma
 * (0x53fe0). */
bool nmea_frame_checksum_good(nmea_frame* frame, uint8_t* end) {
  static const uint8_t CHECKSUM_DIGITS = 2;
  static const int32_t HEXADECIMAL = 16;
  char digits[3];

  if (frame->checksum_digits == 0) {
    return true;
  }
  if (frame->checksum_digits != CHECKSUM_DIGITS) {
    return false;
  }
  uint8_t sum = oncore_checksum((uint8_t*)frame->text + 1, end);

  if ((int8_t)frame->fields % 2 != 0) {
    sum ^= ',';
  }
  strncpy(digits, (char*)end + 1, CHECKSUM_DIGITS);
  digits[CHECKSUM_DIGITS] = '\0';
  return (uint8_t)strtoul(digits, NULL, HEXADECIMAL) == sum;
}

/* The sentence's LF, its checksum good (0x54076). */
uint32_t nmea_frame_is_lf_good(char c, nmea_frame* frame) {
  uint8_t* end = gps_nmea_checksum_end(frame);

  return nmea_frame_end_is(c, '\n', frame) != 0 &&
         nmea_frame_checksum_good(frame, end);
}

/* The sentence's LF, its checksum bad (0x540ba). */
uint32_t nmea_frame_is_lf_bad(char c, nmea_frame* frame) {
  uint8_t* end = gps_nmea_checksum_end(frame);

  return nmea_frame_end_is(c, '\n', frame) != 0 &&
         !nmea_frame_checksum_good(frame, end);
}

/* The framer's action storing a character (0x54104). */
void nmea_frame_store(uint8_t c, nmea_frame* frame) { *frame->cursor++ = c; }

/* The framer's action starting a sentence with its '$' (0x54116). */
void nmea_frame_start(uint8_t c, nmea_frame* frame) {
  frame->cursor = (uint8_t*)frame->text;
  nmea_frame_store(c, frame);
  frame->fields = 0;
  frame->field_starts[0] = 1;
  frame->checksum_digits = 0;
}

/* A '$' inside a sentence: a frame error, and a new sentence
 * (0x54192). */
void nmea_frame_restart(uint8_t c, nmea_frame* frame) {
  nmea_frame_start(c, frame);
  gps_counts.frame_errors++;
}

/* A character out of place: stored while there is room, a frame error
 * (0x541a8). */
void nmea_frame_store_error(uint8_t c, nmea_frame* frame) {
  if (gps_nmea_framed(frame) < NMEA_FRAME_ROOM) {
    nmea_frame_store(c, frame);
  }
  gps_counts.frame_errors++;
}

/* The LF of a sentence whose checksum is bad: stored, a checksum error
 * (0x541cc). */
void nmea_frame_store_bad_checksum(uint8_t c, nmea_frame* frame) {
  nmea_frame_store(c, frame);
  gps_counts.checksum_errors++;
}

/* A field's end: a NUL in the comma's place, one field more
 * (0x5413e). */
void nmea_frame_field_end(uint8_t c, nmea_frame* frame) {
  nmea_frame_store(0, frame);
  frame->fields++;
}

/* A field's end and the next's start (0x54154). */
void nmea_frame_field_next(uint8_t c, nmea_frame* frame) {
  nmea_frame_field_end(c, frame);
  frame->field_starts[(int8_t)frame->fields] = gps_nmea_framed(frame);
}

/* A checksum digit (0x5417a). */
void nmea_frame_checksum_store(uint8_t c, nmea_frame* frame) {
  nmea_frame_store(c, frame);
  frame->checksum_digits++;
}

/* Frames the sentences the engine sends into gps_response_buffer, one
 * character at a time, until none is left or one is complete; the
 * framing state (0x4d502).  The response's index is none, as the image
 * leaves it. */
gps_framing_state gps_receive(void) {
  nmea_frame* frame = (nmea_frame*)&gps_response_buffer;

  while (gps_receive_state != GPS_FRAMING_COMPLETE && duarta_rx_pending()) {
    const uint8_t c = drta_get_byte(NULL);
    const nmea_frame_transition* transition =
        gps_nmea_states[(int8_t)gps_receive_state];

    while (transition->test(c, frame) == 0) {
      transition++;
    }
    if (transition->action != NULL) {
      transition->action(c, frame);
    }
    gps_receive_state = transition->next;
  }
  gps_response_index = (gps_request_code_word)GPS_REQUEST_NONE;
  return gps_receive_state;
}

/* 1 while the engine has time RAIM, field_592's mark: the test of the
 * descriptors of rrs, int,rrm and the second zzz (0x527da). */
uint8_t gps_time_raim_supported(void) {
  return (gps.field_592 & GPS_FIELD_592_TIME_RAIM) != 0;
}

/* ssd's mask angle, ignored satellites and cable delay, negated
 * (0x539c6). */
uint32_t nmea_ssd_commit(char* name, gps_state* record) {
  const nmea_ssd_fields* ssd = &gps.ssd;

  record->mask_angle = ssd->mask_angle;
  record->ignored_satellites = ssd->ignored_satellites;
  record->pps_cable_delay = -ssd->cable_delay;
  return 0;
}

/* rsd's time RAIM settings (0x53be8). */
uint32_t nmea_rsd_commit(char* name, gps_state* record) {
  const nmea_rsd_fields* rsd = &gps.rsd;

  record->raim.raim_on = rsd->raim_on;
  record->raim.alarm_limit = rsd->alarm_limit;
  record->raim.pps_mode = rsd->pps_mode;
  return 0;
}

/* An Oncore request, below the GT-74's codes, as the GT-74's: its code
 * and argument mode by gps_request_translations, GPS_REQUEST_NONE where
 * it has none; and two scripts by others' (0x541e2).  The second never
 * applies: GPS_REQUEST_DEFAULTS is below the GT-74's codes, so the
 * table's entry takes it first. */
void gps_request_translate(gps_request_code_word* code,
                           gps_argument_mode* argument_mode) {
  if (*code < GPS_NMEA_MASK_ANGLE) {
    const gps_request_translation* entry = gps_request_translations;

    while (entry->code != GPS_REQUEST_NONE &&
           (entry->code != *code || entry->argument_mode != *argument_mode)) {
      entry++;
    }
    *code = entry->to_code;
    *argument_mode = entry->to_argument_mode;
  } else if (*code == GPS_SCRIPT_UTC_SET) {
    *code = GPS_STEP_UTC_SELECT;
  } else if (*code == GPS_REQUEST_DEFAULTS) {
    *code = GPS_SCRIPT_DEFAULTS;
  }
}

/* GGA's position and satellites (0x536f0). */
uint32_t nmea_gga_commit(char* name, gps_state* record) {
  const nmea_gga_fields* gga = &gps.gga;
  oncore_status_fields* status = &record->status;

  status->latitude =
      nmea_hemisphere_sign(gga->latitude_hemisphere) * gga->latitude;
  status->longitude =
      nmea_hemisphere_sign(gga->longitude_hemisphere) * gga->longitude;
  status->height_msl = gga->height;
  status->height_gps = gga->height + gga->height_offset;
  status->tracked_count = gga->satellites;
  return 0;
}

/* tps's time into the status record: GPS time from its week and seconds
 * where the engine gives GPS time and the record UTC is off, else its
 * date and time; the fraction 0 (0x533d2). */
void nmea_tps_time_commit(nmea_tps_fields* tps, gps_state* record) {
  static const int32_t SECONDS_PER_WEEK = 604800;
  oncore_status_fields* status = &record->status;
  uint32_t date;

  if (record->utc_time == ONCORE_TIME_GPS &&
      tps->time_mode == NMEA_TIME_MODE_GPS) {
    gps_seconds_to_date_time(tps->week * SECONDS_PER_WEEK + tps->seconds, &date,
                             (uint32_t*)&tps->gps_time);
    const uint32_t packed = oncore_date_unpack(date);

    *(uint32_t*)&tps->gps_date = packed;
    *(uint32_t*)&status->date = packed;
    status->time = tps->gps_time;
  } else {
    status->date = tps->date;
    status->time = tps->time;
  }
  status->fraction = 0;
}

/* anc's and ssd's satellites into the visible satellites' health: 1 for
 * one ssd ignores, 2 for one anc marks; 1 without the visibility
 * record (0x53db2). */
uint32_t nmea_anc_commit(char* name, gps_state* record) {
  if (record->visibility_received == 0) {
    return 1;
  }
  oncore_visible_satellite* visible = record->visibility.satellites;
  oncore_visible_satellite* const end =
      &record->visibility.satellites[record->visibility.count];

  for (; visible < end; visible++) {
    if (satellite_id_valid(SATELLITE_GPS, visible->satellite) != 0) {
      const uint32_t bit = 1u << (uint8_t)(visible->satellite - 1);

      visible->health =
          ((gps.ssd.ignored_satellites & bit) != 0 ? ONCORE_HEALTHY_REMOVED
                                                   : ONCORE_HEALTHY) |
          ((gps.anc.satellites & bit) != 0 ? ONCORE_UNHEALTHY : ONCORE_HEALTHY);
    }
  }
  return 0;
}

/* Points gps_response_cursor back at gps_response_buffer's start and
 * starts the framer idle (0x4d4f0). */
void gps_response_reset(void) {
  gps_response_cursor = (uint8_t*)gps_response_buffer;
  gps_receive_state = GPS_FRAMING_IDLE;
}

/* ssd's field after its E, two digits (0x535ee). */
nmea_field_status nmea_decode_ssd_e(char* field, uint8_t* out) {
  if (*field != 'E') {
    return NMEA_FIELD_EMPTY;
  }
  return nmea_decode_2_digits(field + 1, out);
}

/* The rsd and rrm sentences' commit: with a record, rsd's, keeps in
 * gps.field_592's bit 2 whether its PPS mode is on; 0 (0x536ae). */
uint32_t nmea_pps_mode_commit(char* name, nmea_rsd_fields* record) {
  if (record != NULL) {
    gps.field_592 =
        (gps.field_592 & ~GPS_FIELD_592_BIT_2) |
        (record->pps_mode == ONCORE_PPS_ON ? GPS_FIELD_592_BIT_2 : 0);
  }
  return 0;
}

/* A request's sentence descriptor: an Oncore request's, below
 * GPS_NMEA_MASK_ANGLE, by its translation's code, whatever its argument
 * mode, then the table's entry 0x20 bytes on from 0x5469e for each code
 * (0x52890), outside gps_sentence_descriptors for a code it does not
 * hold, as the image's (0x5286a). */
gps_sentence_descriptor* gps_request_descriptor(gps_request_code_word code) {
  if (code < GPS_NMEA_MASK_ANGLE) {
    const gps_request_translation* entry = gps_request_translations;

    while (entry->code != GPS_REQUEST_NONE && entry->code != code) {
      entry++;
    }
    code = entry->to_code;
  }
  return (gps_sentence_descriptor*)gps_sentence_descriptors +
         (code - GPS_NMEA_MASK_ANGLE);
}

/* A satellite set, a satellite for each hex digit above '6', by its
 * place in the field from 1 (0x534be); the Z3815A 4010's takes each
 * '1'. */
nmea_field_status nmea_decode_satellite_bits(char* field, uint32_t* out) {
  static const uint8_t HIGHEST_UNMARKED = '6';
  uint32_t set = 0;
  uint8_t id = 1;

  for (; *field != '\0'; field++, id++) {
    if ((ctype_table[(int8_t)*field + 1] & LIBC_CTYPE_HEX) != 0 &&
        (uint8_t)*field > HIGHEST_UNMARKED) {
      satellite_add_to_set(SATELLITE_GPS, id, &set);
    }
  }
  *out = set;
  return NMEA_FIELD_GOOD;
}

/* GSA's satellites onto the channels, its fix into the receiver status
 * and its DOP, PDOP for a 3D fix and HDOP for any other, in tenths
 * rounded (0x5374a). */
uint32_t nmea_gsa_commit(char* name, gps_state* record) {
  static const uint8_t FIX_2D = 2;
  static const uint8_t FIX_3D = 3;
  static const uint16_t POOR_DOP = 1200; /* hundredths */
  static const uint16_t HUNDREDTHS_PER_TENTH = 10;
  /* a channel's status bit for a satellite used for the fix */
  static const uint8_t USED_FOR_POSITION = 0x80;
  const nmea_gsa_fields* gsa = &gps.gsa;
  oncore_status_fields* status = &record->status;
  oncore_channel_status* channel = status->channels;
  oncore_channel_status* const end = &status->channels[gps.channel_count];
  uint32_t set = gsa->satellites;
  uint8_t id = 1;

  for (; channel < end && set != 0; id++, set >>= 1) {
    if ((set & 1) != 0) {
      channel->satellite = id;
      channel->mode = ONCORE_AVAILABLE_FOR_POSITION;
      channel->status |= USED_FOR_POSITION;
      channel++;
    }
  }
  for (; channel < end; channel++) {
    channel->satellite = 0;
    channel->mode = ONCORE_CODE_SEARCH;
    channel->status &= ~USED_FOR_POSITION;
  }
  status->receiver_status &= ~(ONCORE_STATUS_2D_FIX | ONCORE_STATUS_3D_FIX);
  if (gps.gga.angle_decoded != 0 && gsa->fix == FIX_2D) {
    status->receiver_status |= ONCORE_STATUS_2D_FIX;
  } else if (gps.gga.angle_decoded != 0 && gsa->fix == FIX_3D) {
    status->receiver_status |= ONCORE_STATUS_3D_FIX;
  }
  if (gsa->fix == FIX_3D) {
    status->dop =
        (int32_t)(gsa->pdop + HUNDREDTHS_PER_TENTH / 2) / HUNDREDTHS_PER_TENTH;
    status->dop_type = ONCORE_DOP_PDOP;
  } else {
    status->dop =
        (int32_t)(gsa->hdop + HUNDREDTHS_PER_TENTH / 2) / HUNDREDTHS_PER_TENTH;
    status->dop_type = ONCORE_DOP_HDOP;
  }
  if ((gsa->fix == FIX_3D && gsa->pdop > POOR_DOP) ||
      (gsa->fix == FIX_2D && gsa->hdop > POOR_DOP)) {
    status->receiver_status |= ONCORE_STATUS_POOR_GEOMETRY;
  } else {
    status->receiver_status &= ~ONCORE_STATUS_POOR_GEOMETRY;
  }
  return 0;
}

/* GSV's satellites: each channel's signal strength, its satellite's SNR
 * or 0, and the satellites into the visibility record (0x53852); the
 * Z3815A 4010's also sets visibility_received. */
uint32_t nmea_gsv_commit(char* name, gps_state* record) {
  const nmea_gsv_fields* gsv = &gps.gsv;

  if (gsv->count != 0) {
    oncore_channel_status* channel = record->status.channels;
    oncore_channel_status* const end =
        &record->status.channels[record->channel_count];

    for (; channel < end; channel++) {
      channel->signal_strength = 0;
      if (channel->satellite == 0) {
        continue;
      }
      for (const nmea_gsv_satellite* s = gsv->satellites;
           s < &gsv->satellites[gsv->count]; s++) {
        if (s->satellite == channel->satellite) {
          channel->signal_strength = s->snr;
          break;
        }
      }
    }
    oncore_visible_satellite* visible = record->visibility.satellites;

    for (const nmea_gsv_satellite* s = gsv->satellites;
         s < &gsv->satellites[gsv->count]; s++, visible++) {
      visible->satellite = s->satellite;
      visible->elevation = s->elevation;
      visible->azimuth = s->azimuth;
      visible->doppler = 0;
    }
  }
  record->visibility.count = gsv->count;
  return 0;
}

/* rrm's time RAIM result, satellites and sawtooth into the record, and
 * into gps the uptime and raim_received, which command_monitor takes
 * (0x53ba0). */
uint32_t nmea_rrm_commit(char* name, gps_state* record) {
  const nmea_rrm_fields* rrm = &gps.rrm;

  record->raim.solution_status = rrm->solution_status;
  record->raim.raim_status = rrm->raim_status;
  record->raim_satellites = rrm->satellites;
  record->raim.sawtooth = rrm->sawtooth;
  gps.field_6fa = get_uptime();
  gps.raim_received = 1;
  return 0;
}

/* A sentence field's decoder: anc's satellites, one for each '1' by its
 * place in the field from 1, and whether the field holds a '1' or a '2'
 * (0x53468). */
nmea_field_status nmea_decode_anc_satellites(char* field,
                                             nmea_anc_fields* place) {
  uint32_t set = 0;
  uint8_t marked = 0;
  uint8_t id = 1;

  for (; *field != '\0'; field++, id++) {
    if (*field == '1') {
      satellite_add_to_set(SATELLITE_GPS, id, &set);
      marked = 1;
    } else if (*field == '2') {
      marked = 1;
    }
  }
  place->satellites = set;
  place->marked = marked;
  return NMEA_FIELD_GOOD;
}

/* A channel's status bits that the anc and acc commits keep, each set
 * while the channel's satellite is in the sentence's set. */
static const uint8_t NMEA_CHANNEL_IN_ANC = 0x10;
static const uint8_t NMEA_CHANNEL_IN_ACC = 0x08;

/* Sets or clears a status bit of each of the record's channels: set
 * where the channel tracks a satellite of the set. */
static void nmea_channels_mark(gps_state* record, uint32_t satellites,
                               uint8_t bit) {
  oncore_channel_status* channel = record->status.channels;
  oncore_channel_status* const end =
      &record->status.channels[record->channel_count];

  for (; channel < end; channel++) {
    if (channel->satellite != 0 &&
        ((1u << (channel->satellite - 1)) & satellites) != 0) {
      channel->status |= bit;
    } else {
      channel->status &= ~bit;
    }
  }
}

/* anc onto the record's status: the receiver status's bit 0 clear while
 * anc marked a satellite and set while it did not, and each channel's
 * bit 4 for a satellite of anc's (0x53904). */
uint32_t nmea_anc_channels_commit(char* name, gps_state* record) {
  const nmea_anc_fields* anc = &gps.anc;

  if (anc->marked != 0) {
    record->status.receiver_status &= ~ONCORE_STATUS_BAD_ALMANAC;
  } else {
    record->status.receiver_status |= ONCORE_STATUS_BAD_ALMANAC;
  }
  nmea_channels_mark(record, anc->satellites, NMEA_CHANNEL_IN_ANC);
  return 0;
}

/* acc's commit: each channel's bit 3 for a satellite of acc's
 * (0x53970). */
uint32_t nmea_acc_commit(char* name, gps_state* record) {
  nmea_channels_mark(record, gps.acc.satellites, NMEA_CHANNEL_IN_ACC);
  return 0;
}

/* tps's commit, which ends each second's sentences: each sentence's
 * commit, GSV's once it has gathered a satellite, which also marks the
 * visibility as received when a GSV sentence came since the last
 * (0x53e3c). */
uint32_t nmea_tps_commit(char* name, gps_state* record) {
  nmea_gga_commit(name, record);
  nmea_gsa_commit(name, record);
  if (record->gsv.count != 0) {
    nmea_gsv_commit(name, record);
    if (record->gsv.sentence != 0) {
      record->visibility_received = 1;
      record->gsv.sentence = 0;
    }
  }
  nmea_anc_channels_commit(name, record);
  nmea_acc_commit(name, record);
  nmea_anc_commit(name, record);
  nmea_rrm_commit(name, record);
  nmea_rsd_commit(name, record);
  nmea_tps_fields_commit(name, record);
  return 0;
}

/* tps's: the time, the UTC offset and the time mode; the leap second
 * pending where its date is after the time's, and the leap second tps
 * announces, its date and time, whether due or not; the position hold
 * and, with it, GGA's position as the hold position, its height above
 * the ellipsoid unless field_592 has the height above mean sea level;
 * the receiver status's acquiring bit, clear only out of position hold
 * with GSA's satellites enough for a fix and the geometry good; then
 * the position message's work, and the status monitor's time
 * (0x539ea). */
uint32_t nmea_tps_fields_commit(char* name, gps_state* record) {
  static const uint8_t HOLD_OFF = 1;
  static const uint8_t HOLD_ON = 2;
  static const uint32_t DAY_SHIFT = 16;
  static const uint8_t FIX_SATELLITES = 3;
  nmea_tps_fields* tps = &gps.tps;
  const nmea_gga_fields* gga = &gps.gga;

  gps_periodic_send(NULL, NULL);
  nmea_tps_time_commit(tps, record);
  record->utc_offset = tps->utc_offset;
  record->time_mode = tps->time_mode;
  if (*(uint32_t*)&tps->date != 0 &&
      *(uint32_t*)&tps->leap_date >> DAY_SHIFT != 0 && tps->leap_second >= -1 &&
      tps->leap_second <= 1) {
    if ((tps->time_mode == NMEA_TIME_MODE_UTC ||
         tps->time_mode == NMEA_TIME_MODE_GPS) &&
        tps->leap_second != 0 &&
        nmea_leap_second_due(tps->date, tps->time, tps->leap_date,
                             tps->leap_time) != 0) {
      record->leap_second_pending = tps->leap_second == -1
                                        ? ONCORE_LEAP_SECOND_SUBTRACT
                                        : ONCORE_LEAP_SECOND_ADD;
    } else {
      record->leap_second_pending = ONCORE_NO_LEAP_SECOND;
    }
  }
  if (tps->leap_second == 1) {
    record->leap_second_announced = ONCORE_LEAP_SECOND_ADD;
  } else if (tps->leap_second == -1) {
    record->leap_second_announced = ONCORE_LEAP_SECOND_SUBTRACT;
  } else {
    record->leap_second_announced = ONCORE_NO_LEAP_SECOND;
  }
  record->leap_year = tps->leap_date.year;
  record->leap_month = tps->leap_date.month;
  record->leap_day = tps->leap_date.day;
  record->leap_time = tps->leap_time;
  if (tps->position_hold == HOLD_OFF) {
    record->position_hold = 0;
  } else if (tps->position_hold == HOLD_ON) {
    record->position_hold = 1;
    if (gga->angle_decoded != 0) {
      record->hold_position.latitude =
          nmea_hemisphere_sign(gga->latitude_hemisphere) * gga->latitude;
      record->hold_position.longitude =
          nmea_hemisphere_sign(gga->longitude_hemisphere) * gga->longitude;
      if ((record->field_592 & GPS_FIELD_592_HEIGHT_MSL) != 0) {
        record->hold_position.height = gga->height;
        record->hold_height_type = ONCORE_HEIGHT_MSL;
      } else {
        record->hold_position.height = gga->height + gga->height_offset;
        record->hold_height_type = ONCORE_HEIGHT_GPS;
      }
    }
  }
  gps.gga.angle_decoded = 0;
  if (tps->position_hold == HOLD_OFF &&
      count_bits(gps.gsa.satellites) >= FIX_SATELLITES &&
      (record->status.receiver_status & ONCORE_STATUS_POOR_GEOMETRY) == 0) {
    record->status.receiver_status &= ~ONCORE_STATUS_ACQUIRING;
  } else {
    record->status.receiver_status |= ONCORE_STATUS_ACQUIRING;
  }
  record->field_6e2 = *(uint32_t*)&tps->date_9 >> DAY_SHIFT != 0;
  oncore_position_message();
  gps.status_monitor_time = get_uptime();
  return 0;
}

/* The field after the one at field, past its comma or NUL. */
static uint8_t* gps_nmea_next_field(uint8_t* field) {
  return field + (uint8_t)nmea_field_length(field) + 1;
}

/* Whether a framed sentence is the one named, its address in *address. */
static bool gps_nmea_sentence_named(nmea_frame* frame, const char* name,
                                    uint8_t** address) {
  return (uint8_t)nmea_sentence_is(frame, (uint8_t*)name, address) != 0;
}

/* GPS_MATCH_ANSWERED when a response is the sentence named, else
 * GPS_MATCH_OTHER. */
static gps_match_result gps_nmea_answered_by(nmea_frame* response,
                                             const char* name) {
  uint8_t* address;

  return gps_nmea_sentence_named(response, name, &address) ? GPS_MATCH_ANSWERED
                                                           : GPS_MATCH_OTHER;
}

/* A GLL's answer, GGA: while the position is held, its latitude and
 * longitude each within 12 mas of the sent, any longitude at a pole
 * (0x546c8). */
static gps_match_result gps_nmea_match_gll(uint8_t* sent,
                                           nmea_frame* response) {
  static const int32_t TOLERANCE = 12;
  /* A latitude this near 90 degrees north is at the pole. */
  static const int32_t POLE = 90 * GPS_MAS_PER_DEGREE;
  static const int32_t POLE_TOLERANCE = 2;
  static const uint8_t LATITUDE = 1;
  static const uint8_t LONGITUDE = 2;
  uint8_t* sent_field;
  uint8_t* response_field;
  bool same = true;
  bool polar = false;

  if (!gps_nmea_sentence_named((nmea_frame*)sent, "GLL", &sent_field) ||
      !gps_nmea_sentence_named(response, "GGA", &response_field)) {
    return GPS_MATCH_OTHER;
  }
  if (gps.position_hold == 0) {
    return GPS_MATCH_ANSWERED;
  }
  /* GGA's time, before its latitude. */
  response_field = (uint8_t*)nmea_frame_field(1, response);
  for (uint8_t angle = LATITUDE; angle <= LONGITUDE && same; angle++) {
    int32_t sent_angle;
    int32_t response_angle;

    sent_field = gps_nmea_next_field(sent_field);
    response_field = gps_nmea_next_field(response_field);
    if (nmea_decode_angle((char*)sent_field, &sent_angle) != NMEA_FIELD_GOOD ||
        nmea_decode_angle((char*)response_field, &response_angle) !=
            NMEA_FIELD_GOOD) {
      same = false;
      continue;
    }
    sent_field = gps_nmea_next_field(sent_field);
    sent_angle *= nmea_hemisphere_sign(*sent_field);
    if (angle == LATITUDE && POLE - sent_angle - POLE_TOLERANCE <= 0) {
      polar = true;
    }
    response_field = gps_nmea_next_field(response_field);
    response_angle *= nmea_hemisphere_sign(*response_field);
    if (labs(sent_angle - response_angle) > TOLERANCE &&
        !(polar && angle == LONGITUDE)) {
      same = false;
    }
  }
  return same ? GPS_MATCH_ANSWERED : GPS_MATCH_DIFFERS;
}

/* An rrs's answer, rsd: its three fields the sent's (0x5493a). */
static gps_match_result gps_nmea_match_rrs(uint8_t* sent,
                                           gps_argument_mode mode,
                                           nmea_frame* response) {
  static const uint8_t FIELDS = 3;
  uint8_t* sent_field;
  uint8_t* response_field;
  bool same = true;

  if (!gps_nmea_sentence_named(response, "rsd", &response_field)) {
    return GPS_MATCH_OTHER;
  }
  if (mode == GPS_ARGUMENTS_QUERY) {
    return GPS_MATCH_ANSWERED;
  }
  if (!gps_nmea_sentence_named((nmea_frame*)sent, "rrs", &sent_field)) {
    return GPS_MATCH_OTHER;
  }
  sent_field = gps_nmea_next_field(sent_field);
  response_field = gps_nmea_next_field(response_field);
  for (uint8_t i = 1; i <= FIELDS && same; i++) {
    if ((uint8_t)nmea_field_equal(sent_field, NMEA_FIELD_MEASURED,
                                  response_field, NMEA_FIELD_MEASURED) == 0) {
      same = false;
    }
    sent_field = gps_nmea_next_field(sent_field);
    response_field = gps_nmea_next_field(response_field);
  }
  return same ? GPS_MATCH_ANSWERED : GPS_MATCH_DIFFERS;
}

/* A set's answer, ssd: its field of the set's letter, from its second,
 * the sent's, or for a height, while the position is held, within 10 of
 * it (0x54814). */
static gps_match_result gps_nmea_match_set(uint8_t* sent,
                                           gps_argument_mode mode,
                                           nmea_frame* response) {
  static const int8_t FIRST_SETTING = 2;
  static const char HEIGHT = 'H';
  static const int32_t HEIGHT_TOLERANCE = 10;
  uint8_t* sent_field;
  uint8_t* response_field;
  int32_t sent_height;
  int32_t response_height;

  if (!gps_nmea_sentence_named(response, "ssd", &response_field)) {
    return GPS_MATCH_OTHER;
  }
  if (mode == GPS_ARGUMENTS_QUERY) {
    return GPS_MATCH_ANSWERED;
  }
  if (!gps_nmea_sentence_named((nmea_frame*)sent, "set", &sent_field)) {
    return GPS_MATCH_OTHER;
  }
  sent_field = gps_nmea_next_field(sent_field);
  for (int8_t index = FIRST_SETTING;; index++) {
    response_field = (uint8_t*)nmea_frame_field(index, response);
    if (response_field == NULL) {
      return GPS_MATCH_DIFFERS;
    }
    if (*sent_field == *response_field) {
      break;
    }
  }
  if ((uint8_t)nmea_field_equal(sent_field, NMEA_FIELD_MEASURED, response_field,
                                NMEA_FIELD_MEASURED) != 0) {
    return GPS_MATCH_ANSWERED;
  }
  if (*sent_field != HEIGHT) {
    return GPS_MATCH_DIFFERS;
  }
  if (gps.position_hold == 0) {
    return GPS_MATCH_ANSWERED;
  }
  if (nmea_decode_ssd_height((char*)sent_field, &sent_height) !=
          NMEA_FIELD_GOOD ||
      nmea_decode_ssd_height((char*)response_field, &response_height) !=
          NMEA_FIELD_GOOD) {
    return GPS_MATCH_DIFFERS;
  }
  return labs(sent_height - response_height) > HEIGHT_TOLERANCE
             ? GPS_MATCH_DIFFERS
             : GPS_MATCH_ANSWERED;
}
/* Whether a response answers the sentence sent, by its descriptor's
 * match kind (0x5466e; its jump table at 0x546b8): sent is the sentence
 * as built, its fields still between commas, the response as framed;
 * the result a gps_match_result.  The length, the index and the
 * response's length are unread. */
uint8_t gps_response_match(gps_request_code_word param_1, nmea_frame* param_2,
                           uint8_t param_3, char param_4, uint16_t param_5,
                           nmea_frame* param_6, uint16_t param_7) {
  uint8_t* sent = (uint8_t*)param_2;
  const gps_argument_mode mode = (gps_argument_mode)param_4;

  switch (gps_request_descriptor(param_1)->match_kind) {
    case GPS_SENTENCE_MATCH_TIMEOUT:
      return GPS_MATCH_IGNORED;
    case GPS_SENTENCE_MATCH_GLL:
      return gps_nmea_match_gll(sent, param_6);
    case GPS_SENTENCE_MATCH_RRS:
      return gps_nmea_match_rrs(sent, mode, param_6);
    case GPS_SENTENCE_MATCH_RRQ:
      return gps_nmea_answered_by(param_6, "rsd");
    case GPS_SENTENCE_MATCH_SET:
      return gps_nmea_match_set(sent, mode, param_6);
    case GPS_SENTENCE_MATCH_SRQ:
      return gps_nmea_answered_by(param_6, "ssd");
    case GPS_SENTENCE_MATCH_TST:
      return gps_nmea_answered_by(param_6, "tst");
    case GPS_SENTENCE_MATCH_NONE:
    default:
      return GPS_MATCH_ANSWERED;
  }
}

/* tst's: the receiver ID's lines, the software's and the model's by the
 * software's part number, the rest "--"; the ID's commit; field_592's
 * bits and the channel count for the model, the cable delay's correction
 * for one software; and, for a result of 0, the self-test's failures
 * from its bits (0x53c12).  The lines, the commit and the failures are
 * the record's; field_592 and the correction are gps's own. */
uint32_t nmea_tst_commit(char* name, gps_state* record) {
  /* The engines' software part numbers, and the versions that differ. */
  static const uint32_t GT80_SOFTWARE = 4850266;
  static const uint32_t GT77_SOFTWARE = 4850207;
  static const uint32_t NO_TIME_RAIM_SOFTWARE = 4850113;
  static const uint16_t NO_TIME_RAIM_VERSION = 2;
  static const uint32_t CORRECTED_SOFTWARE = 4850102;
  static const uint16_t CORRECTED_VERSION = 8;
  static const int16_t CORRECTION = -400;
  /* self_test's bits: the response's bits 0 and 1 from bit 9, its bit
   * 2 as bit 13. */
  static const uint8_t LOW_BITS = 3;
  static const uint8_t LOW_BITS_SHIFT = 9;
  static const uint8_t BIT_2 = 4;
  static const uint16_t BIT_2_FAILURE = 0x2000;
  static const char NONE[] = "--";
  const nmea_tst_fields* tst = &gps.tst;
  oncore_receiver_id* id = &record->receiver_id;
  const gps_field_592_bit models =
      GPS_FIELD_592_MODEL_GT77 | GPS_FIELD_592_MODEL_GT80;

  sprintf(id->software_part_number, "SFTW P/N # %-7.7d",
          tst->software_part_number);
  sprintf(id->software_version, "SOFTWARE VER # %-3.3d",
          (uint32_t)tst->software_version);
  if (tst->software_part_number == GT80_SOFTWARE) {
    libc_copy(id->model_number, "MODEL # FURUNO GT-80");
    gps.field_592 = (gps.field_592 & ~models) | GPS_FIELD_592_MODEL_GT80;
  } else if (tst->software_part_number == GT77_SOFTWARE) {
    libc_copy(id->model_number, "MODEL # FURUNO GT-77");
    gps.field_592 = (gps.field_592 & ~models) | GPS_FIELD_592_MODEL_GT77;
  } else {
    libc_copy(id->model_number, "MODEL # FURUNO GT-74");
    gps.field_592 &= ~models;
  }
  libc_copy(id->copyright, NONE);
  libc_copy(id->software_revision, NONE);
  libc_copy(id->software_date, NONE);
  libc_copy(id->hardware_part_number, NONE);
  libc_copy(id->serial_number, NONE);
  libc_copy(id->manufacture_date, NONE);
  libc_copy(id->options, NONE);
  record->receiver_id_received = 1;
  gps.field_592 &= ~GPS_FIELD_592_BIT_1;
  gps.field_592 &= ~GPS_FIELD_592_BIT_0;
  gps_channel_count_from_model();
  gps.field_592 |= GPS_FIELD_592_HEIGHT_MSL;
  if (tst->software_part_number == NO_TIME_RAIM_SOFTWARE &&
      tst->software_version == NO_TIME_RAIM_VERSION) {
    gps.field_592 &= ~GPS_FIELD_592_TIME_RAIM;
  } else {
    gps.field_592 |= GPS_FIELD_592_TIME_RAIM;
  }
  gps.cable_delay_correction =
      tst->software_part_number == CORRECTED_SOFTWARE &&
              tst->software_version == CORRECTED_VERSION
          ? CORRECTION
          : 0;
  gps.field_592 |= GPS_FIELD_592_BIT_2;
  if (tst->result == 0) {
    const uint8_t bits = tst->self_test_bits;

    record->self_test =
        (uint16_t)(((bits & BIT_2) != 0 ? BIT_2_FAILURE : 0) |
                   ((uint16_t)(bits & LOW_BITS) << LOW_BITS_SHIFT));
  }
  return 0;
}

/* A field's length against its entry's, where positive: the
 * difference, a byte, as the image tests it. */
static bool gps_nmea_length_bad(int8_t expected, uint8_t length) {
  return (uint8_t)(length - abs(expected)) != 0 && expected >= 0;
}

/* A sentence's fields' decoders: its entry's, but for ssd from an engine
 * of twelve channels nmea_ssd_decoders_twelve (0x54586). */
static const nmea_field_entry* gps_nmea_fields_decoded(
    const nmea_sentence_entry* sentence) {
  static const uint8_t TWELVE_CHANNELS = 12;

  if (sentence->fields_decoded == nmea_ssd_decoders &&
      gps.channel_count == TWELVE_CHANNELS) {
    return nmea_ssd_decoders_twelve;
  }
  return sentence->fields_decoded;
}

/* Decodes a framed sentence by nmea_sentences: its address one of them,
 * its length and commas the entry's, each field's length its entry's;
 * then, with the kernel's preemption off, each field's decoder, the
 * commit last.  Counts an address, an unknown sentence or a field out of
 * place as gps_counts' error, false (0x544d0).  The fields' decoders it
 * looks up twice, before the lengths and before the decoding. */
uint32_t nmea_sentence_decode(nmea_frame* frame) {
  static const uint32_t NO_PREEMPTION = 0x80;
  char* address = nmea_sentence_address(frame);
  nmea_sentence_entry* sentence = nmea_sentences;

  if (address == NULL) {
    gps_counts.address_errors++;
    return 0;
  }
  while (!gps_nmea_same_name(sentence->name, address)) {
    if (sentence->name == NULL) {
      gps_counts.unknown_response++;
      return 0;
    }
    sentence++;
  }
  if (gps_nmea_length_bad(sentence->length, gps_nmea_framed(frame))) {
    gps_counts.data_errors++;
    return 0;
  }
  if ((uint8_t)(frame->fields - abs(sentence->fields)) != 0 &&
      (sentence->fields >= 0 ||
       !nmea_sentence_fields_allowed(sentence, frame->fields))) {
    gps_counts.data_errors++;
    return 0;
  }
  const nmea_field_entry* entry = gps_nmea_fields_decoded(sentence);

  if (entry == NULL) {
    return 1;
  }
  for (; entry->index != 0; entry++) {
    char* field = nmea_frame_field(entry->index, frame);

    if (field != NULL &&
        (uint8_t)(libc_length(field) - abs(entry->length)) != 0 &&
        entry->length > 0) {
      gps_counts.data_errors++;
      return 0;
    }
  }
  entry = gps_nmea_fields_decoded(sentence);
  psos_mode(NO_PREEMPTION, NO_PREEMPTION, NULL);
  do {
    if (entry->decode != NULL) {
      char* field = nmea_frame_field(entry->index, frame);

      if (field != NULL) {
        entry->decode(field, entry->place);
      }
    }
  } while ((entry++)->index != 0);
  psos_mode(0, NO_PREEMPTION, NULL);
  return 1;
}
