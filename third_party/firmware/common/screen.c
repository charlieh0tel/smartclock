/* The functions of screen.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/screen.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* An element satellite_status_list sorts with satellite_compare: a
 * visible satellite's number and elevation. */
typedef struct screen_satellite_elevation {
  uint8_t satellite;
  uint8_t elevation;
} screen_satellite_elevation;

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

/* A @@Bb record's class by its health, none for no record. */
satellite_health_class satellite_track_class(
    oncore_visible_satellite* satellite) {
  if (satellite == NULL) {
    return SATELLITE_CLASS_UNKNOWN;
  }
  switch (satellite->health) {
    case ONCORE_HEALTHY:
      return SATELLITE_CLASS_HEALTHY;
    case ONCORE_UNHEALTHY:
      return SATELLITE_CLASS_UNHEALTHY;
    case ONCORE_HEALTHY_REMOVED:
    case ONCORE_UNHEALTHY_REMOVED:
      return SATELLITE_CLASS_REMOVED;
    default:
      return SATELLITE_CLASS_UNKNOWN;
  }
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

/* The @@Bb record of a satellite, by bisection of the records, which
 * the receiver sends sorted by number; NULL if none. */
oncore_visible_satellite* satellite_find(uint8_t id,
                                         gps_visibility_record* record) {
  oncore_visible_satellite* low = record->visibility.satellites;
  oncore_visible_satellite* high = low + record->visibility.count;

  while (low < high) {
    oncore_visible_satellite* middle = low + (high - low) / 2;

    if (id < middle->satellite) {
      high = middle;
    } else if (id > middle->satellite) {
      low = middle + 1;
    } else {
      return middle;
    }
  }
  return NULL;
}

/* A satellite's cell: none for an ID not a satellite's; for one not
 * tracked, ignored, unhealthy or acquiring, by its health and its
 * channel's band, if any; else its elevation and azimuth. */
void satellite_status(uint8_t tracked, uint8_t id, satellite_sets* sets,
                      gps_visibility_record* visibility, satellite_cell* cell) {
  oncore_visible_satellite* satellite;

  if (satellite_id_valid(SATELLITE_ANY, id) == 0) {
    cell->status = SATELLITE_CELL_NONE;
    return;
  }
  satellite = id <= GPS_LAST_SATELLITE ? satellite_find(id, visibility) : NULL;
  if (tracked != 1) {
    switch (satellite_track_class(satellite)) {
      case SATELLITE_CLASS_REMOVED:
        cell->status = SATELLITE_CELL_IGNORED;
        return;
      case SATELLITE_CLASS_UNHEALTHY:
        cell->status = SATELLITE_CELL_UNHEALTHY;
        return;
      default:
        break;
    }
    if (satellite_in_set(SATELLITE_GPS, id, sets->acquiring) != 0 ||
        satellite_in_set(SATELLITE_PSEUDOLITE, id,
                         sets->pseudolites_acquiring) != 0) {
      cell->status = SATELLITE_CELL_ACQUIRING;
      return;
    }
    if (satellite_in_set(SATELLITE_GPS, id, sets->synchronized) != 0 ||
        satellite_in_set(SATELLITE_PSEUDOLITE, id,
                         sets->pseudolites_synchronized) != 0) {
      cell->status = SATELLITE_CELL_SYNCHRONIZED;
      return;
    }
    if (satellite_in_set(SATELLITE_GPS, id, sets->ephemeris) != 0 ||
        satellite_in_set(SATELLITE_PSEUDOLITE, id,
                         sets->pseudolites_ephemeris) != 0) {
      cell->status = SATELLITE_CELL_EPHEMERIS;
      return;
    }
  }
  satellite_elevation_azimuth(satellite, cell);
}

/* Whether two strings are the same, as format_dms compares in line. */
static bool screen_equal(const char* a, const char* b) {
  while (*a == *b) {
    if (*a == '\0') {
      return true;
    }
    a++;
    b++;
  }
  return false;
}

/* An angle in mas as its hemisphere, N or S, E or W by longitude, then
 * "%4d:%02d:%06.3f" degrees, minutes and seconds; a blank hemisphere for
 * zero or what rounds to it. */
void format_dms(int32_t mas, char longitude, char* text) {
  uint16_t degrees;
  uint8_t minutes;
  double seconds;
  char hemisphere;

  if (mas > 0) {
    hemisphere = longitude == 0 ? 'N' : 'E';
  } else if (mas < 0) {
    hemisphere = longitude == 0 ? 'S' : 'W';
  } else {
    hemisphere = ' ';
  }
  mas_to_dms(mas, &degrees, &minutes, &seconds);
  sprintf(text, "%c%4d:%02d:%06.3f", hemisphere, degrees, minutes, seconds);
  if (screen_equal(text + 1, "   0:00:00.000")) {
    text[0] = ' ';
  }
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

/* A signal level as "%3d", or blanks when negative. */
void format_level(int16_t level, char* text) {
  if (level < 0) {
    libc_copy(text, "   ");
    return;
  }
  sprintf(text, "%3d", level);
}

/* :SYSTem:PRINt:LENGth?'s answer, the screen's lines. */
uint8_t status_screen_length(void) {
  static const uint8_t LINES = 23;

  return LINES;
}
