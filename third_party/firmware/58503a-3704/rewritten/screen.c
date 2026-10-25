/* The receiver status screen, :SYSTem:STATus? (097-z3801-01 and
 * 097-59551-02 show its layout; docs/screen-format-strings.md has its
 * strings): the snapshot status_snapshot takes of the loop and the GPS
 * engine, and the lines status_screen prints from it.  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/screen`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  The floating arithmetic is calls to the
 * image's soft-float routines (docs/firmware/softfloat.md):
 * double_divide_reversed(a, b) is b / a, float_compare(a, b) is
 * positive when b < a and double_compare(a, b) negative when b < a. */

/* An element satellite_status_list sorts with satellite_compare: a
 * visible satellite's number and elevation (0x49cba). */
typedef struct screen_satellite_elevation {
  uint8_t satellite;
  uint8_t elevation;
} screen_satellite_elevation;

/* scpi_real_to_text's text: a sign, then the digits; its second
 * character is the first digit, '0' only for zero. */
static const char SCREEN_ZERO_DIGIT = '0';

/* The unit format_si appends for seconds (0x4efd4). */
static char* const SCREEN_SECONDS = "s";

/* The satellite's bit of a set, GPS prn - 1 of a long, a pseudolite's
 * prn - 0x21: 1 if it is set, 0 if not or for 0 (0x49a1e). */
int32_t satellite_in_set(satellite_kind kind, uint8_t id, uint32_t mask) {
  if (id == 0) {
    return 0;
  }
  if (kind == SATELLITE_PSEUDOLITE) {
    id -= GPS_LAST_SATELLITE;
  }
  return (mask & cpu_long_bit((uint8_t)(id - 1))) != 0;
}

/* The tracking bands a channel's mode puts a satellite in: acquiring
 * below 5, synchronized below 7, then ephemeris, compared signed
 * (0x4991e to 0x4994a). */
static void screen_band_add(satellite_kind kind, uint8_t id, int8_t mode,
                            void* acquiring, void* synchronized,
                            void* ephemeris) {
  static const int8_t SYNCHRONIZED_FIRST = ONCORE_MESSAGE_SYNC_DETECT;
  static const int8_t EPHEMERIS_FIRST = ONCORE_EPHEMERIS_ACQUIRE;

  if (mode <= 0) {
    return;
  }
  if (mode < SYNCHRONIZED_FIRST) {
    satellite_add_to_set(kind, id, acquiring);
  } else if (mode < EPHEMERIS_FIRST) {
    satellite_add_to_set(kind, id, synchronized);
  } else {
    satellite_add_to_set(kind, id, ephemeris);
  }
}

/* A @@Bb record's class by its health, none for no record (0x49a4e). */
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
 * them, below 91 and 360 degrees; else no position (0x49a90). */
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
 * number first (0x49bd0). */
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
 * the receiver sends sorted by number; NULL if none (0x503a2). */
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
 * channel's band, if any; else its elevation and azimuth (0x49ac4). */
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

/* satellite_status_list's first argument: 1 for the tracked, those
 * used for the fix; 0 for those visible or tracked and not used
 * (0x49c3e). */
static const char SCREEN_LIST_NOT_TRACKED = 0;

static const char SCREEN_LIST_TRACKED = 1;

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
 * zero or what rounds to it (0x482c2). */
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

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then "%4d:%02d:%06.7f" degrees, minutes and seconds
 * (0x48364). */
void format_dms_precise(char* text, double mas, char latitude) {
  gps_format_dms(text, mas, latitude, "%c%4d:%02d:%06.7f");
}

/* The SI prefixes from 10^-12 to 10^9, none for 10^0 (si_prefixes,
 * 0x4dd6d). */
static const char SCREEN_SI_PREFIXES[] = {'p',  'n', 'u', 'm',
                                          '\0', 'k', 'M', 'G'};

/* format_si's digits for every digit a double has. */
static const uint8_t SCREEN_EVERY_DIGIT = 0;

/* A value as scpi_real_to_text gives it to the resolution and digits,
 * 15 for 0, its exponent made a multiple of 3 and an SI prefix, "*" past
 * them, then a space, the prefix and the unit; "--" if it gives none.
 * The text keeps scpi_real_to_text's sign first (0x49fd2). */
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
 * minutes below 1000, else "1000+ hr" (0x4a15e). */
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
 * "--:--:--" and false (0x4a3f0). */
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

/* The months' names, by month, "---" for 0 (month_names, 0x4dd76). */
static char* const SCREEN_MONTH_NAMES[] = {"---", "Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug", "Sep",
                                           "Oct", "Nov", "Dec"};

/* A date as "%02d %s %04d" day, month and year, 1900 to 2079; else
 * "-- --- ----"; 1 if it is one (0x4a44a). */
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

/* A satellite's number as " %2d", or blanks for none (0x49e3c). */
void format_satellite_number(char satellite, char* text) {
  if (satellite == 0) {
    libc_copy(text, "   ");
    return;
  }
  sprintf(text, " %2d", (uint8_t)satellite);
}

/* Marks a satellite's number "*" when the flag is set, attempting to
 * track; the flag (0x49e70). */
bool mark_flag(char flag, char* text) {
  if (flag != 0) {
    *text = '*';
  }
  return flag != 0;
}

/* A signal level as "%3d", or blanks when negative (0x49e86). */
void format_level(int16_t level, char* text) {
  if (level < 0) {
    libc_copy(text, "   ");
    return;
  }
  sprintf(text, "%3d", level);
}

/* The rows of the position column status_screen_position fills, beside
 * the satellites, by its jump table (0x4a5a2). */
#define SCREEN_ROW_GPS_PPS 1
#define SCREEN_ROW_ANTENNA_DELAY 2
#define SCREEN_ROW_POSITION 3
#define SCREEN_ROW_MODE 4
#define SCREEN_ROW_SUSPENDED 5
#define SCREEN_ROW_LATITUDE 6
#define SCREEN_ROW_LONGITUDE 7
#define SCREEN_ROW_HEIGHT 8

/* The survey's progress: the positions averaged, at most the survey's
 * length (0x4a67c, 0x4a74a). */
static uint32_t screen_survey_done(const status_snapshot_record* snapshot) {
  return snapshot->average_count < (uint32_t)snapshot->douglas_duration
             ? snapshot->average_count
             : (uint32_t)snapshot->douglas_duration;
}

/* The mode row's survey: "Survey:  ", the percent done, "% complete"
 * (0x4a670). */
static void screen_survey_line(status_snapshot_record* snapshot, char* line) {
  uint32_t done = screen_survey_done(snapshot);
  char percent_text[16];
  char* percent;

  strcat(line, "Survey:  ");
  if (done == 0) {
    percent = "    0";
  } else {
    float fraction = float_multiply(
        float_divide(unsigned_to_float(done),
                     integer_to_float(snapshot->douglas_duration)),
        GPS_SURVEY_PERCENT);

    if (float_compare(GPS_SURVEY_LEAST, fraction) > 0) {
      percent = " <0.1";
    } else if (float_compare(fraction, GPS_SURVEY_MOST_UNFINISHED) <= 0 ||
               done >= (uint32_t)snapshot->douglas_duration) {
      sprintf(percent_text, "%5.1f", float_to_double(fraction));
      percent = percent_text;
    } else {
      percent = ">99.9";
    }
  }
  strcat(line, percent);
  strcat(line, "% complete");
}

/* Why the survey is suspended, or NULL: no @@Ba yet, fewer than four
 * satellites used, or no 3D fix, bit 5 of the receiver status
 * (0x4a78e). */
static char* screen_survey_suspended(const status_snapshot_record* snapshot) {
  static const uint8_t FIX_SATELLITES = 4;
  static const uint8_t RECEIVER_3D_FIX = 0x20;

  if (snapshot->status_received == 0) {
    return "no track data";
  }
  if (count_bits(snapshot->satellites.used) < FIX_SATELLITES) {
    return "track <4 sats";
  }
  if ((snapshot->receiver_status & RECEIVER_3D_FIX) == 0) {
    return "poor geometry";
  }
  return NULL;
}

/* A latitude or longitude row: the position's source and its axis, then
 * the angle (0x4a7fe, 0x4a842). */
static void screen_angle_line(status_snapshot_record* snapshot, int32_t mas,
                              char longitude, char* axis, char* line) {
  char angle[16];
  char label[10];

  format_dms(mas, longitude, angle);
  libc_copy(label, snapshot->position_source);
  strcat(label, axis);
  sprintf(line, "%-8s %s", label, angle);
}

/* The screen's position column by row: the GPS 1PPS's state, the
 * antenna delay, the position's heading, the survey's mode and why it
 * is suspended, the latitude, the longitude and the height, which sets
 * height_shown, cleared first; any other row is empty (0x4a562). */
void status_screen_position(uint8_t row, status_snapshot_record* snapshot,
                            char* line, uint8_t* height_shown) {
  static const double NANOSECONDS = 1e9;
  static const char NANOSECOND_RESOLUTION = -9;
  char text[24];
  char* reason;

  *height_shown = 0;
  switch (row) {
    case SCREEN_ROW_GPS_PPS:
      status_screen_sync(snapshot, text);
      sprintf(line, "GPS 1PPS %s", text);
      break;
    case SCREEN_ROW_ANTENNA_DELAY:
      format_si(double_divide_reversed(
                    NANOSECONDS, integer_to_double(snapshot->pps_cable_delay)),
                NANOSECOND_RESOLUTION, SCREEN_EVERY_DIGIT, SCREEN_SECONDS,
                text);
      sprintf(line, "ANT DLY  %s", text[0] == '-' ? text : text + 1);
      break;
    case SCREEN_ROW_POSITION:
      libc_copy(line, "Position ________________________");
      break;
    case SCREEN_ROW_MODE:
      libc_copy(line, "MODE     ");
      if (snapshot->position_hold != 0) {
        strcat(line, "Hold");
      } else if (snapshot->douglas_duration_infinite == 1) {
        strcat(line, "Navigation");
      } else {
        screen_survey_line(snapshot, line);
      }
      break;
    case SCREEN_ROW_SUSPENDED:
      reason = NULL;
      if (snapshot->position_hold == 0 &&
          (screen_survey_done(snapshot) <
               (uint32_t)snapshot->douglas_duration ||
           snapshot->douglas_duration_infinite == 1)) {
        reason = screen_survey_suspended(snapshot);
      }
      if (reason != NULL) {
        sprintf(line, "%9sSuspended: %13.13s", "", reason);
      } else {
        libc_copy(line, "");
      }
      break;
    case SCREEN_ROW_LATITUDE:
      screen_angle_line(snapshot, snapshot->position[0], 0, "LAT", line);
      break;
    case SCREEN_ROW_LONGITUDE:
      screen_angle_line(snapshot, snapshot->position[1], 1, "LON", line);
      break;
    case SCREEN_ROW_HEIGHT:
      status_screen_height(snapshot, line);
      *height_shown = 1;
      break;
    default:
      line[0] = '\0';
      break;
  }
}

/* The rows of the column beside the mode lines status_screen_tfom
 * fills. */
#define SCREEN_ROW_TFOM 1
#define SCREEN_ROW_INTERVAL 2
#define SCREEN_ROW_HOLD_THRESHOLD 3
#define SCREEN_ROW_UNCERTAINTY 4

/* The TFOM's code, which tfom_code takes from the float's bits
 * (0x4a916). */
static int16_t screen_tfom_code(const status_snapshot_record* snapshot) {
  union libc_float tfom = {.value = snapshot->tfom};

  return tfom_code(tfom.bits);
}

/* The screen's column beside the mode lines by row: TFOM and FFOM, the
 * 1 PPS TI relative to the reference in use, GPS or Ext, the holdover
 * threshold and the uncertainty's heading; any other row is empty
 * (0x4a8a6). */
void status_screen_tfom(uint8_t row, status_snapshot_record* snapshot,
                        char* line) {
  static const char TI_RESOLUTION = -10;
  static const char THRESHOLD_RESOLUTION = -9;
  static const uint8_t ROOMY = 10; /* TI texts that fit the long words */
  static const uint8_t CLOSE = 15; /* the short */
  char text[34];
  uint8_t length;
  char* relative;

  switch (row) {
    case SCREEN_ROW_TFOM:
      if (snapshot->ffom_valid == 0) {
        libc_copy(text, "--");
      } else {
        sprintf(text, "%d", snapshot->ffom);
      }
      sprintf(line, "TFOM     %d%13sFFOM    %2s", screen_tfom_code(snapshot),
              "", text);
      break;
    case SCREEN_ROW_INTERVAL:
      /* The flag is interval_mean_flag's first byte (0x4a93a). */
      if (snapshot->interval_mean_flag >> 8 == 0) {
        libc_copy(text, " --");
      } else {
        format_si(float_to_double(snapshot->interval_mean), TI_RESOLUTION,
                  SCREEN_EVERY_DIGIT, "s ", text);
        length = libc_length(text);
        if (length <= ROOMY) {
          relative = "relative to ";
        } else if (length <= CLOSE) {
          relative = "rel to ";
        } else {
          relative = "rel ";
        }
        strcat(text, relative);
        strcat(text, snapshot->pll_field_3d5 == 0 ? "GPS" : "Ext");
      }
      sprintf(line, "1PPS TI %s", text);
      break;
    case SCREEN_ROW_HOLD_THRESHOLD:
      if (snapshot->holdover_limit_state == HOLDOVER_LIMIT_OFF) {
        libc_copy(text, " Off");
      } else {
        format_si(float_to_double(snapshot->holdover_limit_threshold),
                  THRESHOLD_RESOLUTION, SCREEN_EVERY_DIGIT, SCREEN_SECONDS,
                  text);
      }
      sprintf(line, "HOLD THR %s", text + 1);
      break;
    case SCREEN_ROW_UNCERTAINTY:
      libc_copy(line, "Holdover Uncertainty ____________");
      break;
    default:
      line[0] = '\0';
      break;
  }
}

/* The suffix of the power-up line, or of the recovery line by its own
 * index, power-up's fine frequency or phase alignment; with the TI
 * reading for those that settle it (0x4ab38). */
void status_screen_mode_suffix(status_screen_line line,
                               status_snapshot_record* snapshot, char* text) {
  static const uint8_t TI_DIGITS = 4;
  pll_powerup_suffix suffix;
  char* words;
  bool with_interval = false;
  char reading[10];
  char bracketed[16];

  if (line == STATUS_SCREEN_LINE_POWERUP) {
    suffix = snapshot->powerup_suffix;
  } else if (snapshot->recovery_suffix == PLL_RECOVERY_SUFFIX_FINE_FREQUENCY) {
    suffix = PLL_POWERUP_SUFFIX_FINE_FREQUENCY;
  } else if (snapshot->recovery_suffix == PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT) {
    suffix = PLL_POWERUP_SUFFIX_PHASE_ALIGNMENT;
  } else {
    suffix = PLL_POWERUP_SUFFIX_NONE;
  }
  switch (suffix) {
    case PLL_POWERUP_SUFFIX_WARM_UP:
      words = ": OCXO warm-up";
      break;
    case PLL_POWERUP_SUFFIX_GPS_ACQUISITION:
      words = ": GPS acquisition";
      break;
    case PLL_POWERUP_SUFFIX_COARSE:
      if (snapshot->interval_valid == 0) {
        words = ": coarse frequency adjustment";
      } else {
        words = ": coarse freq adj";
        with_interval = true;
      }
      break;
    case PLL_POWERUP_SUFFIX_FINE_FREQUENCY:
      if (snapshot->interval_valid == 0) {
        words = ": fine frequency adjustment";
      } else {
        words = ": fine freq adj";
        with_interval = true;
      }
      break;
    case PLL_POWERUP_SUFFIX_PHASE_ALIGNMENT:
      words = ": phase alignment";
      with_interval = true;
      break;
    case PLL_POWERUP_SUFFIX_LEAP_SECOND:
      words = ": leap second determination";
      break;
    default:
      words = "";
      break;
  }
  if (with_interval && snapshot->interval_valid != 0) {
    format_si(snapshot->interval, (char)snapshot->interval_tag, TI_DIGITS,
              SCREEN_SECONDS, reading);
    sprintf(bracketed, "[TI %9.9s]", reading);
    sprintf(text, "%-18.18s%14s", words, bracketed);
  } else {
    libc_copy(text, words);
  }
}

/* The mode lines, Locked, Recovery, Holdover and Power-up, the
 * snapshot's marked, and the null text that ends them
 * (status_screen_mode_lines, 0x4ddda). */
static const status_screen_mode_line SCREEN_MODE_LINES[] = {
    {STATUS_SCREEN_LINE_LOCKED, 0, "Locked "},
    {STATUS_SCREEN_LINE_RECOVERY, 0, "Recovery"},
    {STATUS_SCREEN_LINE_HOLDOVER, 0, "Holdover"},
    {STATUS_SCREEN_LINE_POWERUP, 0, "Power-up"},
    {STATUS_SCREEN_LINE_NONE, 0, NULL},
};

/* The mode line the snapshot's mode marks (0x4b042). */
static status_screen_line screen_mode_line(pll_screen_mode mode) {
  switch (mode) {
    case PLL_MODE_HOLDOVER_MANUAL:
    case PLL_MODE_HOLDOVER:
      return STATUS_SCREEN_LINE_HOLDOVER;
    case PLL_MODE_RECOVERY:
      return STATUS_SCREEN_LINE_RECOVERY;
    case PLL_MODE_POWERUP:
      return STATUS_SCREEN_LINE_POWERUP;
    case PLL_MODE_LOCKED:
      return STATUS_SCREEN_LINE_LOCKED;
    default:
      return STATUS_SCREEN_LINE_NONE;
  }
}

/* The reference the loop uses, by pll.field_3e5 as the snapshot keeps
 * it: 0 the GPS's, else the external 1PPS's. */
static bool screen_external(const status_snapshot_record* snapshot) {
  return snapshot->pll_field_3d5 != 0;
}

/* The holdover line's suffix: manual, or why it waits (0x4b108). */
static char* screen_holdover_suffix(const status_snapshot_record* snapshot) {
  if (snapshot->mode == PLL_MODE_HOLDOVER_MANUAL) {
    return ": manually initiated";
  }
  switch (snapshot->holdover_waiting_reason) {
    case HOLD_CAUSE_MANUAL:
      return ": manually initiated";
    case HOLD_CAUSE_GPS:
      return screen_external(snapshot) ? ": Ext 1PPS invalid"
                                       : ": GPS 1PPS invalid";
    case HOLD_CAUSE_TI_ERROR:
    case HOLD_CAUSE_HARDWARE_FAILURE:
      return ": internal hardware problem";
    case HOLD_CAUSE_TI_LIMIT_EXCEEDED:
      return ": 1PPS TI exceeds hold threshold";
    case HOLD_CAUSE_1PPS_SELECTION:
      return screen_external(snapshot) ? ": switched to Ext 1PPS reference"
                                       : ": switched to GPS 1PPS reference";
    case HOLD_CAUSE_GPS_COMMAND_MONITOR:
      return ": GPS receiver failure";
    default:
      return "";
  }
}

/* The marked mode line's suffix, into text where it builds one
 * (0x4b08e). */
static char* screen_mode_suffix(status_screen_line line, bool locked,
                                status_snapshot_record* snapshot, char* text) {
  switch (line) {
    case STATUS_SCREEN_LINE_LOCKED:
      libc_copy(text, screen_external(snapshot) ? "to Ext" : "to GPS");
      strcat(text, locked ? "" : ": stabilizing frequency");
      return text;
    case STATUS_SCREEN_LINE_RECOVERY:
    case STATUS_SCREEN_LINE_POWERUP:
      status_screen_mode_suffix(line, snapshot, text);
      return text;
    case STATUS_SCREEN_LINE_HOLDOVER:
      return screen_holdover_suffix(snapshot);
    default:
      return "";
  }
}

/* A health monitor test: nonzero when the hardware condition's bits say
 * the part failed.  status_screen passes it the condition and the
 * entry's bit, a long (0x4b62a), which only bit_is_set reads. */
typedef uint8_t screen_health_test(int16_t condition, int32_t bit);

/* One of the health monitor's parts, which status_screen prints as
 * "%s: %-3s%s", name, OK or Err, suffix: 16 bytes. */
typedef struct screen_health_part {
  char* name; /* NULL ends the table */
  screen_health_test* test;
  int32_t bit;
  char* suffix; /* the space before the next */
} screen_health_part;

/* The health monitor's parts, by status_hardware_condition's bits
 * (0x4de1c): the self-test, the supplies, the oven's supply, the
 * internal reference, the EFC and the GPS receiver. */
static const screen_health_part SCREEN_HEALTH_PARTS[] = {
    {"Self Test", (screen_health_test*)bit_is_set, 0, "   "},
    {"Int Pwr", (screen_health_test*)bits_1_to_3_any, 1, "  "},
    {"Oven Pwr", (screen_health_test*)bit_4_set, 4, "  "},
    {"OCXO", (screen_health_test*)bit_12_set, 12, "  "},
    {"EFC", (screen_health_test*)bits_6_7_any, 6, "  "},
    {"GPS Rcv", (screen_health_test*)bits_8_9_any, 9, ""},
    {NULL, NULL, 0, NULL},
};

/* The satellite columns' fewest rows, beside the position column; an
 * eight-channel receiver's take eight. */
static const uint8_t SCREEN_SATELLITE_ROWS = 7;

/* A reference's record as the snapshot keeps it valid: valid 1 with its
 * 1PPS alive, or 2 (gps_reference_record.valid). */
static bool screen_reference_valid(uint8_t valid, uint8_t pps_dead) {
  return (valid == 1 && pps_dead == 0) || valid == 2;
}

/* Prints the receiver status screen from a snapshot through write and
 * print, after clearing the terminal for clear 1: synchronization, the
 * mode lines with TFOM, FFOM, the TI and holdover beside them,
 * acquisition with the satellites, a row for each of the receiver's
 * channels, and the position, and the health monitor (0x4af48). */
void status_screen(status_snapshot_record* snapshot, char clear,
                   write_function* write, printf_function* print) {
  static const char CLEAR = 1;
  static const char ESCAPE = 0x1b;
  static const uint8_t PREDICTION_DIGITS = 4;
  static const uint8_t RAIM_MARK = 15; /* where the health line shows it */
  const satellite_list_entry* tracked = snapshot->tracked;
  const satellite_list_entry* not_tracked = snapshot->not_tracked;
  const screen_health_part* part;
  const status_screen_mode_line* mode_line;
  status_screen_line current;
  bool locked;
  bool failed;
  uint8_t attempting;
  uint8_t height_shown = 0;
  uint8_t row_height;
  uint8_t raim_option = 0;
  uint8_t rows;
  uint8_t row;
  char* words;
  char* suffix;
  char* label;
  char tracked_number[5];
  char tracked_level[4];
  char tracked_cell[7];
  char first_number[5];
  char first_cell[7];
  char second_number[5];
  char second_cell[8];
  char line[80];
  char text[41];

  if (clear == CLEAR) {
    print("%c%c%c%c%c%c%c", ESCAPE, '[', '2', 'J', ESCAPE, '[', 'H');
  }
  write(
      "------------------------------- Receiver Status "
      "-------------------------------\r\n");
  rows = snapshot->channel_count > SCREEN_SATELLITE_ROWS
             ? snapshot->channel_count
             : SCREEN_SATELLITE_ROWS;
  if (rows <= SCREEN_SATELLITE_ROWS) {
    write(" \r\n");
  }
  write("SYNCHRONIZATION ............................");
  switch (snapshot->stage) {
    case PLL_STAGE_POWERUP:
    case PLL_STAGE_DIAG:
    case PLL_STAGE_FATAL_ERROR:
      locked = false;
      words = "............... [ Outputs Invalid ]";
      break;
    case PLL_STAGE_NORMAL_PLL:
      locked = true;
      words = "................. [ Outputs Valid ]";
      break;
    default:
      locked = false;
      words = " [ Outputs Valid/Reduced Accuracy ]";
      break;
  }
  print("%s\r\n", words);

  write(
      "SmartClock Mode ___________________________   Reference Outputs "
      "_______________\r\n");
  current = screen_mode_line(snapshot->mode);
  row = 1;
  for (mode_line = SCREEN_MODE_LINES; mode_line->text != NULL; mode_line++) {
    char* prefix;

    if (current == mode_line->mode) {
      prefix = ">> ";
      suffix = screen_mode_suffix(current, locked, snapshot, text);
    } else {
      prefix = "   ";
      suffix = "";
    }
    libc_copy(line, prefix);
    strcat(line, mode_line->text);
    strcat(line, suffix);
    print("%-46.46s", line);
    status_screen_tfom(row, snapshot, line);
    print("%s\r\n", line);
    row++;
  }

  if (snapshot->kenneth_predicted_valid == 0) {
    libc_copy(text, " --");
  } else {
    format_si(float_to_double(snapshot->kenneth_predicted),
              snapshot->kenneth_predicted_resolution, PREDICTION_DIGITS,
              SCREEN_SECONDS, text);
    strcat(text, "/initial 24 hrs");
  }
  print("%43s   Predict  %s\r\n", "", text + 1);
  if (current == STATUS_SCREEN_LINE_HOLDOVER ||
      current == STATUS_SCREEN_LINE_RECOVERY) {
    libc_copy(line, "Holdover Duration:");
    format_duration_short(snapshot->holdover_duration, text);
    strcat(line, text);
  } else {
    libc_copy(line, " ");
  }
  if (snapshot->kenneth_present_valid == 0) {
    label = "";
    libc_copy(text, " ");
  } else {
    label = "Present";
    format_si(float_to_double(snapshot->kenneth_present),
              snapshot->kenneth_present_resolution, PREDICTION_DIGITS,
              SCREEN_SECONDS, text);
  }
  print("%43s   %s  %s\r\n", line, label, text + 1);

  write("ACQUISITION ..............................................");
  if (!screen_external(snapshot)) {
    words =
        screen_reference_valid(snapshot->reference_valid, snapshot->pps_dead)
            ? ".. [ GPS 1PPS Valid ]"
            : " [ GPS 1PPS Invalid ]";
  } else {
    words = screen_reference_valid(snapshot->external_valid,
                                   snapshot->external_pps_dead)
                ? ".. [ Ext 1PPS Valid ]"
                : " [ Ext 1PPS Invalid ]";
  }
  print("%s\r\n", words);
  print("Tracking: %d ____   ", snapshot->tracked_count);
  sprintf(text, "Not Tracking: %d ________", snapshot->not_tracked_count);
  print("%.24s   ", text);
  switch (snapshot->leap_second_pending) {
    case ONCORE_LEAP_SECOND_ADD:
      words = " +1 leap second pending";
      break;
    case ONCORE_LEAP_SECOND_SUBTRACT:
      words = " -1 leap second pending";
      break;
    default:
      words = "_______________________";
      break;
  }
  print("Time _____%s\r\n", words);
  print("%-16.16s   %-11.11s  %-11.11s%3s",
        snapshot->tracked_count == 0 ? "" : "PRN  El  Az   SS",
        snapshot->not_tracked_count == 0 ? "" : "PRN  El  Az",
        snapshot->not_tracked_count <= snapshot->channel_count ? ""
                                                               : "PRN  El  Az",
        "");
  status_screen_time(snapshot, line);
  print("%s\r\n", line);

  attempting = 0;
  for (row = 1; row <= rows; row++) {
    if (row <= snapshot->channel_count) {
      const satellite_list_entry* second =
          not_tracked + snapshot->channel_count;

      format_satellite_number(tracked->satellite, tracked_number);
      format_satellite_cell(tracked->cell, tracked_cell);
      format_level(tracked->level, tracked_level);
      format_satellite_number(not_tracked->satellite, first_number);
      attempting |= mark_flag(not_tracked->attempting, first_number);
      format_satellite_cell(not_tracked->cell, first_cell);
      format_satellite_number(second->satellite, second_number);
      attempting |= mark_flag(second->attempting, second_number);
      format_satellite_cell(second->cell, second_cell);
      tracked++;
      not_tracked++;
    } else {
      libc_copy(tracked_number, " ");
      tracked_level[0] = '\0';
      tracked_cell[0] = '\0';
      first_cell[0] = '\0';
      first_number[0] = '\0';
      second_cell[0] = '\0';
      second_number[0] = '\0';
    }
    status_screen_position(row, snapshot, line, &row_height);
    if (row_height != 0) {
      height_shown = 1;
    }
    print("%3.3s  %6.6s  %3.3s   %3.3s  %6.6s  %3.3s  %6.6s%3s%s\r\n",
          tracked_number, tracked_cell, tracked_level, first_number, first_cell,
          second_number, second_cell, "", line);
  }
  words = attempting != 0 ? "*attempting to track" : "";
  sprintf(line, "%2d deg", snapshot->mask_angle);
  print("ELEV MASK %-6.6s%3s%-25.25s%2s", line, "", words, "");
  if (height_shown != 0) {
    line[0] = '\0';
  } else {
    status_screen_height(snapshot, line);
  }
  print("%s\r\n", line);

  line[0] = '\0';
  failed = false;
  for (part = SCREEN_HEALTH_PARTS; part->name != NULL; part++) {
    char* result = "OK";

    if (part->test(snapshot->hardware_condition, part->bit) != 0) {
      result = "Err";
      failed = true;
    }
    sprintf(text, "%s: %-3s%s", part->name, result, part->suffix);
    strcat(line, text);
  }
  if (gps_message_supported(GPS_REQUEST_TRAIM, &raim_option) == 0 ||
      raim_option == 0) {
    line[RAIM_MARK] = '|';
  }
  write(
      "HEALTH MONITOR ......................................................");
  print("%s\r\n", failed ? " [ ERROR ]" : "... [ OK ]");
  print("%s", line);
}

/* :SYSTem:PRINt:LENGth?'s answer, the screen's lines (0x5060c). */
uint8_t status_screen_length(void) {
  static const uint8_t LINES = 23;

  return LINES;
}
