/* The functions of screen.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/screen.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* scpi_real_to_text's text: a sign, then the digits; its second
 * character is the first digit, '0' only for zero. */
static const char SCREEN_ZERO_DIGIT = '0';

/* The satellite's bit of a set, GPS prn - 1 of a long, a pseudolite's
 * prn - 0x21: 1 if it is set, 0 if not or for 0. */
int32_t satellite_in_set(satellite_kind kind, uint8_t id, uint32_t mask) {
  if (id == 0) {
    return 0;
  }
  if (kind == SATELLITE_PSEUDOLITE) {
    id -= GPS_LAST_SATELLITE;
  }
  return (mask & cpu_long_bit((uint8_t)(id - 1))) != 0;
}

/* A satellite's cell: its elevation and azimuth while the record holds
 * them, below 91 and 360 degrees; else no position. */
void satellite_elevation_azimuth(oncore_visible_satellite* satellite,
                                 satellite_cell* cell) {
  static const uint8_t ELEVATION_LIMIT = 91;
  static const uint16_t AZIMUTH_LIMIT = 360;

  if (satellite != NULL && satellite->elevation < ELEVATION_LIMIT &&
      satellite->azimuth < AZIMUTH_LIMIT) {
    cell->status = SATELLITE_CELL_POSITION;
    cell->elevation = satellite->elevation;
    cell->azimuth = satellite->azimuth;
    return;
  }
  cell->status = SATELLITE_CELL_NO_POSITION;
}

/* qsort's order of two satellites: by elevation, then the higher
 * number first. */
int32_t satellite_compare(void* first, void* second) {
  static const int32_t BEFORE = -1;
  static const int32_t AFTER = 1;
  const screen_satellite_elevation* a = first;
  const screen_satellite_elevation* b = second;

  if (a->elevation != b->elevation) {
    return a->elevation < b->elevation ? BEFORE : AFTER;
  }
  if (a->satellite == b->satellite) {
    return 0;
  }
  return a->satellite > b->satellite ? BEFORE : AFTER;
}

/* The SI prefixes from 10^-12 to 10^9, none for 10^0 (si_prefixes,
 * si_prefixes). */
static const char SCREEN_SI_PREFIXES[] = {'p',  'n', 'u', 'm',
                                          '\0', 'k', 'M', 'G'};

/* A value as scpi_real_to_text gives it to the resolution and digits,
 * 15 for 0, its exponent made a multiple of 3 and an SI prefix, "*" past
 * them, then a space, the prefix and the unit; "--" if it gives none.
 * The text keeps scpi_real_to_text's sign first. */
void format_si(double value, char resolution, uint8_t digits, char* unit,
               char* text) {
  static const int16_t EXPONENT_DIGITS = 2;
  static const char POINT_AS_NEEDED = 0;
  static const int16_t SMALLEST_EXPONENT = -12; /* p */
  static const int16_t LARGEST_EXPONENT = 11;   /* G, 10^9 to 10^11 */
  static const int16_t PREFIX_STEP = 3;
  static const char NO_PREFIX = '*';
  /* scpi_real_to_text takes the double's bits as two longs, high
   * first. */
  union {
    double value;
    int32_t words[2];
  } bits = {value};
  char* cursor;
  int16_t exponent;
  char prefix;
  uint8_t counted;

  if (digits == 0) {
    digits = SCPI_REAL_DOUBLE_DIGITS;
  }
  if (scpi_real_to_text(bits.words[0], bits.words[1], (uint8_t*)text, digits,
                        EXPONENT_DIGITS, resolution, POINT_AS_NEEDED) != 0) {
    libc_copy(text, "--");
    return;
  }
  cursor = strchr(text, 'E');
  *cursor = '\0';
  exponent = atoi(cursor + 1);
  if (exponent < SMALLEST_EXPONENT || exponent > LARGEST_EXPONENT) {
    prefix = NO_PREFIX;
  } else {
    /* How far the point moves right to make the exponent a multiple of
     * 3. */
    uint8_t shift = exponent >= 0
                        ? exponent % PREFIX_STEP
                        : PREFIX_STEP - 1 - (-1 - exponent) % PREFIX_STEP;

    prefix = SCREEN_SI_PREFIXES[(exponent - shift - SMALLEST_EXPONENT) /
                                PREFIX_STEP];
    if (shift != 0 && text[1] != SCREEN_ZERO_DIGIT) {
      cursor = strchr(text, '.');
      if (cursor == NULL) {
        cursor = text + libc_length(text);
      }
      while (shift-- != 0) {
        if (*cursor == '.') {
          if (cursor[1] == '\0') {
            cursor[1] = '0';
            cursor[2] = '\0';
          }
          cursor[0] = cursor[1];
          *++cursor = '.';
        } else {
          *cursor++ = '0';
          *cursor = '\0';
        }
      }
      if (cursor[0] == '.' && cursor[1] == '\0') {
        *cursor = '\0';
      }
    }
  }

  /* Cut to the digits, a leading zero not counted. */
  counted = text[1] != SCREEN_ZERO_DIGIT;
  for (cursor = text + 2; counted < digits && *cursor != '\0'; cursor++) {
    if (*cursor != '.') {
      counted++;
    }
  }
  *cursor = '\0';

  cursor = text + libc_length(text);
  *cursor++ = ' ';
  *cursor = prefix;
  if (prefix != '\0') {
    cursor[1] = '\0';
  }
  strcat(text, unit);
}

/* A duration in seconds as minutes and seconds below an hour, hours and
 * minutes below 1000, else "1000+ hr". */
void format_duration_short(uint32_t seconds, char* text) {
  static const uint32_t LONGEST = 1000 * 3600;
  uint32_t large_unit;
  uint32_t small_unit;
  char large_letter;
  char small_letter;

  if (seconds >= LONGEST) {
    libc_copy(text, "1000+ hr");
    return;
  }
  if (seconds < CLOCK_SECONDS_PER_HOUR) {
    large_letter = 'm';
    small_letter = 's';
    large_unit = CLOCK_SECONDS_PER_MINUTE;
    small_unit = 1;
  } else {
    large_letter = 'h';
    small_letter = 'm';
    large_unit = CLOCK_SECONDS_PER_HOUR;
    small_unit = CLOCK_SECONDS_PER_MINUTE;
  }
  sprintf(text, "%3d%c %02d%c", seconds / large_unit, large_letter,
          seconds % large_unit / small_unit, small_letter);
}

/* A time of day as "%02d:%02d:%02d", a leap second's 60 allowed; else
 * "--:--:--" and false. */
bool format_time(time_of_day time, char* text) {
  static const uint8_t HOURS = 24;
  static const uint8_t MINUTES = 60;
  static const uint8_t SECONDS = 61;

  if (time.hour < HOURS && time.minute < MINUTES && time.second < SECONDS) {
    sprintf(text, "%02d:%02d:%02d", time.hour, time.minute, time.second);
    return true;
  }
  libc_copy(text, "--:--:--");
  return false;
}

/* The months' names, by month, "---" for 0 (month_names, month_names). */
static char* const SCREEN_MONTH_NAMES[] = {"---", "Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug", "Sep",
                                           "Oct", "Nov", "Dec"};

/* A date as "%02d %s %04d" day, month and year, 1900 to 2079; else
 * "-- --- ----"; 1 if it is one. */
bool format_date(calendar_date date, char* text) {
  static const uint8_t LAST_MONTH = 12;
  static const uint8_t LAST_DAY = 31;
  static const uint16_t FIRST_YEAR = 1900;
  static const uint16_t LAST_YEAR = 2079;

  if (date.month == 0 || date.month > LAST_MONTH || date.day == 0 ||
      date.day > LAST_DAY || date.year < FIRST_YEAR || date.year > LAST_YEAR) {
    libc_copy(text, "-- --- ----");
    return 0;
  }
  sprintf(text, "%02d %s %04d", date.day, SCREEN_MONTH_NAMES[date.month],
          date.year);
  return 1;
}

/* A satellite's number as " %2d", or blanks for none. */
void format_satellite_number(char satellite, char* text) {
  if (satellite == 0) {
    libc_copy(text, "   ");
    return;
  }
  sprintf(text, " %2d", (uint8_t)satellite);
}

/* Marks a satellite's number "*" when the flag is set, attempting to
 * track; the flag. */
bool mark_flag(char flag, char* text) {
  if (flag != 0) {
    *text = '*';
  }
  return flag != 0;
}
