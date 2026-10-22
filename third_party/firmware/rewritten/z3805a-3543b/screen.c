/* The receiver status screen, :SYSTem:STATus? (097-z3801-01 and
 * 097-59551-02 show its layout; docs/screen-format-strings.md has its
 * strings): the snapshot status_snapshot takes of the loop and the GPS
 * engine, and the lines status_screen prints from it.  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/screen`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  satellite_status and satellite_status_list stay the
 * decompiler's: their prototypes take the satellite sets and the
 * visibility record as int32_t.  The floating arithmetic is calls to the
 * image's soft-float routines (docs/firmware/softfloat.md):
 * double_divide_reversed(a, b) is b / a, float_compare(a, b) is
 * positive when b < a and double_compare(a, b) negative when b < a. */

/* What satellite_find is given, gps.visibility_received (0x100d7a), and
 * the @@Bb message after it, which its search reads (0x4f32c). */
typedef struct screen_visibility {
  uint8_t received; /* gps.visibility_received */
  uint8_t field_01;
  oncore_visibility_fields visibility; /* +0x02: gps.visibility */
} screen_visibility;

/* An element satellite_status_list sorts with satellite_compare: a
 * visible satellite's number and elevation (0x4987a). */
typedef struct screen_satellite_elevation {
  uint8_t satellite;
  uint8_t elevation;
} screen_satellite_elevation;

/* scpi_real_to_text's text: a sign, then the digits; its second
 * character is the first digit, '0' only for zero. */
static const char SCREEN_ZERO_DIGIT = '0';

/* The unit format_si appends for seconds (0x4e1e4). */
static char* const SCREEN_SECONDS = "s";

/* Copies a string, as the image's formatters do in line. */
static void screen_copy(char* to, const char* from) {
  while ((*to++ = *from++) != '\0') {
  }
}

/* A string's length, as the image counts it in line. */
static uint32_t screen_length(const char* text) {
  uint32_t length = 0;

  while (text[length] != '\0') {
    length++;
  }
  return length;
}

/* Bit count of a long, as lsl.l shifts by a register: the count modulo
 * 64, so that one past 31 is no bit (MC68000PRM, LSL). */
static uint32_t screen_bit(int32_t count) {
  static const int32_t COUNT_MASK = 0x3f;
  static const int32_t LONG_BITS = 32;
  int32_t shift = count & COUNT_MASK;

  return shift < LONG_BITS ? 1u << shift : 0;
}

/* The satellite's bit of a set, GPS prn - 1 of a long, a pseudolite's
 * prn - 0x21: 1 if it is set, 0 if not or for 0 (0x495e2). */
int32_t satellite_in_set(satellite_kind kind, uint8_t id, uint32_t mask) {
  static const uint8_t PSEUDOLITE_FIRST = 0x20;

  if (id == 0) {
    return 0;
  }
  if (kind == SATELLITE_PSEUDOLITE) {
    id -= PSEUDOLITE_FIRST;
  }
  return (mask & screen_bit((uint8_t)(id - 1))) != 0;
}

/* The tracking bands a channel's mode puts a satellite in: acquiring
 * below 5, synchronized below 7, then ephemeris, compared signed
 * (0x494f0 to 0x4951a). */
static void screen_band_add(satellite_kind kind, uint8_t id, int8_t mode,
                            uint32_t* acquiring, uint32_t* synchronized,
                            uint32_t* ephemeris) {
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

/* For status_snapshot: the sets of satellites visible, used and
 * tracked, the tracked by their channels' modes, and those not used;
 * all empty unless both the visible and the used sets are known
 * (0x49474).  satellite_add_to_set takes a uint32_t * for a pseudolite
 * set too, and writes its byte. */
void satellite_sets_build(oncore_channel_status* channels,
                          satellite_sets* sets) {
  static const uint8_t CHANNELS = 6;
  bool visible_known;
  bool used_known;
  const oncore_channel_status* channel;

  memset(sets, 0, sizeof *sets);
  visible_known = visible_satellites(&sets->visible_count, &sets->visible);
  used_known =
      satellites_in_use(&sets->used_count, &sets->pseudolites_used_count,
                        &sets->used, &sets->pseudolites_used);
  if (!visible_known || !used_known) {
    return;
  }
  for (channel = channels; channel < channels + CHANNELS; channel++) {
    uint8_t id = channel->satellite;
    int8_t mode = (int8_t)channel->mode;

    if (satellite_id_valid(SATELLITE_GPS, id) != 0) {
      satellite_add_to_set(SATELLITE_GPS, id, &sets->tracked);
      screen_band_add(SATELLITE_GPS, id, mode, &sets->acquiring,
                      &sets->synchronized, &sets->ephemeris);
    } else if (satellite_id_valid(SATELLITE_PSEUDOLITE, id) != 0) {
      satellite_add_to_set(SATELLITE_PSEUDOLITE, id,
                           (uint32_t*)&sets->pseudolites_tracked);
      screen_band_add(SATELLITE_PSEUDOLITE, id, mode,
                      (uint32_t*)&sets->pseudolites_acquiring,
                      (uint32_t*)&sets->pseudolites_synchronized,
                      (uint32_t*)&sets->pseudolites_ephemeris);
    }
  }
  sets->tracked_unused = sets->tracked & ~sets->used;
  sets->acquiring &= sets->tracked_unused;
  sets->synchronized &= sets->tracked_unused;
  sets->ephemeris &= sets->tracked_unused;
  sets->unused = (sets->visible | sets->tracked) & ~sets->used;
  sets->pseudolites_tracked_unused =
      sets->pseudolites_tracked & ~sets->pseudolites_used;
  sets->pseudolites_acquiring &= sets->pseudolites_tracked_unused;
  sets->pseudolites_synchronized &= sets->pseudolites_tracked_unused;
  sets->pseudolites_ephemeris &= sets->pseudolites_tracked_unused;
}

/* A @@Bb record's class by its health, none for no record (0x49612). */
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
 * them, below 91 and 360 degrees; else no position (0x49654). */
void satellite_elevation_azimuth(oncore_visible_satellite* satellite,
                                 uint8_t* entry) {
  static const uint8_t ELEVATION_LIMIT = 91;
  static const uint16_t AZIMUTH_LIMIT = 360;
  satellite_cell* cell = (satellite_cell*)entry;

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
 * number first (0x49794). */
uint32_t satellite_compare(uint8_t* first, uint8_t* second) {
  static const int32_t BEFORE = -1;
  static const int32_t AFTER = 1;
  const screen_satellite_elevation* a =
      (const screen_satellite_elevation*)first;
  const screen_satellite_elevation* b =
      (const screen_satellite_elevation*)second;

  if (a->elevation != b->elevation) {
    return a->elevation < b->elevation ? BEFORE : AFTER;
  }
  if (a->satellite == b->satellite) {
    return 0;
  }
  return a->satellite > b->satellite ? BEFORE : AFTER;
}

/* The @@Bb record of a satellite, by bisection of the records, which
 * the receiver sends sorted by number; NULL if none (0x4f32c). */
oncore_visible_satellite* satellite_find(uint8_t id, void* table) {
  screen_visibility* record = table;
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

/* gps_receiver_status_update's first argument: 0 updates gps' status;
 * 1, with gps_status_alternate's, returns at once (0x4b168). */
static const char SCREEN_STATUS_UPDATE = 0;

static const char SCREEN_STATUS_UPDATE_NONE = 1;

/* satellite_status_list's first argument: 1 for the tracked, those
 * used for the fix; 0 for those visible or tracked and not used
 * (0x49802). */
static const char SCREEN_LIST_NOT_TRACKED = 0;

static const char SCREEN_LIST_TRACKED = 1;

/* Takes the snapshot status_screen prints: the loop's state under a
 * raised interrupt mask, then the GPS engine's, its satellites and its
 * position, held, averaged by the survey or as the engine reports it
 * (0x4a636).  The satellite lists' prototype takes the sets and the
 * visibility record as int32_t. */
void status_snapshot(status_snapshot_record* snapshot) {
  static const uint8_t NOT_TRACKED_ENTRIES = 12;
  static const uint8_t TRACKED_ENTRIES = 6;
  uint32_t date;
  time_of_day time;
  uint8_t holdover; /* kenneth_predicted_get's third output, unused */
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  snapshot->stage = pll.stage;
  snapshot->mode = pll_mode();
  snapshot->powerup_suffix = pll_powerup_suffix_index();
  snapshot->recovery_suffix = recovery_suffix_index();
  snapshot->interval = interval_reading.value;
  snapshot->interval_valid = interval_reading.valid;
  snapshot->interval_errors = interval_reading.errors;
  snapshot->interval_tag = interval_reading.tag;
  snapshot->interval_mean = interval_mean;
  /* The flag and the byte after it, as a word (0x4a698). */
  memcpy(&snapshot->interval_mean_flag, &interval_mean_flag,
         sizeof snapshot->interval_mean_flag);
  snapshot->tfom = pll.tfom;
  snapshot->ffom_valid = get_ffom((frequency_figure_of_merit*)&snapshot->ffom);
  snapshot->kenneth_present_valid = kenneth_present_get(
      &snapshot->kenneth_present, (int8_t*)&snapshot->field_02b);
  snapshot->kenneth_predicted_valid = kenneth_predicted_get(
      &snapshot->kenneth_predicted, (int8_t*)&snapshot->field_031, &holdover);
  snapshot->holdover_waiting_reason = holdover_waiting_reason();
  snapshot->holdover_duration = pll.holdover_duration;
  snapshot->holdover_limit_state = nv_settings.holdover_limit_state;
  snapshot->holdover_limit_threshold = nv_settings.holdover_limit_threshold;
  snapshot->time_zone[0] = nv_settings.time_zone[0];
  snapshot->time_zone[1] = nv_settings.time_zone[1];
  if (get_date_time(&date, &time)) {
    snapshot->clock_invalid = 0;
    snapshot->time = gps.status.time;
    snapshot->date = oncore_date_pack(gps.status.date);
    utc_to_local(&snapshot->date, &snapshot->time);
  } else {
    snapshot->clock_invalid = 1;
    snapshot->time = time;
    snapshot->date = date;
  }
  snapshot->pll_field_3d5 = pll.field_3d5;
  gps_receiver_status_update(SCREEN_STATUS_UPDATE, 0);
  gps_receiver_status_update(SCREEN_STATUS_UPDATE_NONE, gps_status_alternate());
  snapshot->douglas_duration_infinite = nv_settings.douglas_duration_infinite;
  snapshot->hardware_condition = status_hardware_condition();
  set_interrupt_mask(sr);

  snapshot->fix_mode = gps.fix_mode;
  snapshot->reference_valid = gps.reference_valid;
  snapshot->pps_dead = gps.pps_dead;
  snapshot->status_code = gps.status_code;
  snapshot->gps_field_532[0] = gps.field_532[0];
  snapshot->gps_field_532[1] = gps.field_532[1];
  snapshot->status_code_reset = gps.status_code_reset;
  snapshot->leap_second_pending = gps.leap_second_pending;
  snapshot->receiver_status = gps.status.receiver_status;
  snapshot->utc_time = gps.utc_time;
  snapshot->pps_cable_delay = gps.pps_cable_delay;
  snapshot->mask_angle = gps.mask_angle;
  snapshot->position_hold = gps.position_hold;
  snapshot->average_count = gps.average_count;
  snapshot->douglas_duration = nv_settings.douglas_duration;
  satellite_sets_build(gps.status.channels, &snapshot->satellites);
  satellite_status_list(
      SCREEN_LIST_NOT_TRACKED, NOT_TRACKED_ENTRIES,
      (int32_t)&snapshot->satellites, &gps.status.channels[0].satellite,
      (int32_t)&gps.visibility_received, (uint8_t*)snapshot->not_tracked,
      (char*)&snapshot->not_tracked_count);
  satellite_status_list(
      SCREEN_LIST_TRACKED, TRACKED_ENTRIES, (int32_t)&snapshot->satellites,
      &gps.status.channels[0].satellite, (int32_t)&gps.visibility_received,
      (uint8_t*)snapshot->tracked, (char*)&snapshot->tracked_count);
  snapshot->status_received = gps.status_received;

  if (snapshot->position_hold != 0) {
    snapshot->position_source = "";
    snapshot->position[0] = gps.hold_position.latitude;
    snapshot->position[1] = gps.hold_position.longitude;
    snapshot->position[2] = gps.hold_position.height;
  } else if (snapshot->average_count == 0 ||
             snapshot->douglas_duration_infinite == 1) {
    snapshot->position_source =
        snapshot->douglas_duration_infinite == 0 ? "INIT " : "";
    snapshot->position[0] = gps.status.latitude;
    snapshot->position[1] = gps.status.longitude;
    snapshot->position[2] = gps.status.height_msl;
  } else {
    snapshot->position_source = "AVG ";
    snapshot->position[0] = double_to_integer(gps.average_latitude);
    snapshot->position[1] = double_to_integer(gps.average_longitude);
    snapshot->position[2] = double_to_integer(gps.average_height_msl);
  }
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
 * zero or what rounds to it (0x47f14). */
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
 * (0x47fb6). */
void format_dms_precise(char* text, double mas, char latitude) {
  static const int32_t MAS_PER_DEGREE = 3600000;
  static const int32_t MAS_PER_MINUTE = 60000;
  static const double MAS_PER_SECOND = 1000.0;
  uint8_t degrees;
  uint8_t minutes;
  double seconds;
  char hemisphere;

  if (double_compare(0.0, mas) < 0) {
    hemisphere = latitude == 0 ? 'W' : 'S';
    mas = -mas;
  } else {
    hemisphere = latitude == 0 ? 'E' : 'N';
  }
  degrees = double_to_unsigned(
      double_divide_reversed_pointer((double)MAS_PER_DEGREE, &mas));
  double_subtract_in_place(&mas, integer_to_double(degrees * MAS_PER_DEGREE));
  minutes = double_to_unsigned(
      double_divide_reversed_pointer((double)MAS_PER_MINUTE, &mas));
  double_subtract_in_place(&mas, integer_to_double(minutes * MAS_PER_MINUTE));
  seconds = double_divide_reversed_pointer(MAS_PER_SECOND, &mas);
  sprintf(text, "%c%4d:%02d:%06.7f", hemisphere, degrees, minutes, seconds);
}

/* The SI prefixes from 10^-12 to 10^9, none for 10^0 (si_prefixes,
 * 0x4cfc5). */
static const char SCREEN_SI_PREFIXES[] = {'p',  'n', 'u', 'm',
                                          '\0', 'k', 'M', 'G'};

/* A value as scpi_real_to_text gives it to the resolution and digits,
 * 15 for 0, its exponent made a multiple of 3 and an SI prefix, "*" past
 * them, then a space, the prefix and the unit; "--" if it gives none.
 * The text keeps scpi_real_to_text's sign first (0x49b88). */
void format_si(double value, char resolution, uint8_t digits, char* unit,
               char* text) {
  static const uint8_t DOUBLE_DIGITS = 15;
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
    digits = DOUBLE_DIGITS;
  }
  if (scpi_real_to_text(bits.words[0], bits.words[1], (uint8_t*)text, digits,
                        EXPONENT_DIGITS, resolution, POINT_AS_NEEDED) != 0) {
    screen_copy(text, "--");
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
        cursor = text + screen_length(text);
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

  cursor = text + screen_length(text);
  *cursor++ = ' ';
  *cursor = prefix;
  if (prefix != '\0') {
    cursor[1] = '\0';
  }
  strcat(text, unit);
}

/* A duration in seconds as minutes and seconds below an hour, hours and
 * minutes below 1000, else "1000+ hr" (0x49d14). */
void format_duration_short(uint32_t seconds, char* text) {
  static const uint32_t SECONDS_PER_MINUTE = 60;
  static const uint32_t SECONDS_PER_HOUR = 3600;
  static const uint32_t LONGEST = 1000 * 3600;
  uint32_t large_unit;
  uint32_t small_unit;
  char large_letter;
  char small_letter;

  if (seconds >= LONGEST) {
    screen_copy(text, "1000+ hr");
    return;
  }
  if (seconds < SECONDS_PER_HOUR) {
    large_letter = 'm';
    small_letter = 's';
    large_unit = SECONDS_PER_MINUTE;
    small_unit = 1;
  } else {
    large_letter = 'h';
    small_letter = 'm';
    large_unit = SECONDS_PER_HOUR;
    small_unit = SECONDS_PER_MINUTE;
  }
  sprintf(text, "%3d%c %02d%c", seconds / large_unit, large_letter,
          seconds % large_unit / small_unit, small_letter);
}

/* A time of day as "%02d:%02d:%02d", a leap second's 60 allowed; else
 * "--:--:--" and false (0x49e5a). */
bool format_time(time_of_day time, char* text) {
  static const uint8_t HOURS = 24;
  static const uint8_t MINUTES = 60;
  static const uint8_t SECONDS = 61;

  if (time.hour < HOURS && time.minute < MINUTES && time.second < SECONDS) {
    sprintf(text, "%02d:%02d:%02d", time.hour, time.minute, time.second);
    return true;
  }
  screen_copy(text, "--:--:--");
  return false;
}

/* The months' names, by month, "---" for 0 (month_names, 0x4cfce). */
static char* const SCREEN_MONTH_NAMES[] = {"---", "Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug", "Sep",
                                           "Oct", "Nov", "Dec"};

/* A date as "%02d %s %04d" day, month and year, 1900 to 2079; else
 * "-- --- ----"; 1 if it is one (0x49eb4). */
uint32_t format_date(uint16_t year, uint8_t month, uint8_t day, char* text) {
  static const uint8_t LAST_MONTH = 12;
  static const uint8_t LAST_DAY = 31;
  static const uint16_t FIRST_YEAR = 1900;
  static const uint16_t LAST_YEAR = 2079;

  if (month == 0 || month > LAST_MONTH || day == 0 || day > LAST_DAY ||
      year < FIRST_YEAR || year > LAST_YEAR) {
    screen_copy(text, "-- --- ----");
    return 0;
  }
  sprintf(text, "%02d %s %04d", day, SCREEN_MONTH_NAMES[month], year);
  return 1;
}

/* A satellite's number as " %2d", or blanks for none (0x499f2). */
void format_satellite_number(char satellite, char* text) {
  if (satellite == 0) {
    screen_copy(text, "   ");
    return;
  }
  sprintf(text, " %2d", (uint8_t)satellite);
}

/* Marks a satellite's number "*" when the flag is set, attempting to
 * track; the flag (0x49a26). */
bool mark_flag(char flag, uint8_t* text) {
  if (flag != 0) {
    *text = '*';
  }
  return flag != 0;
}

/* A signal level as "%3d", or blanks when negative (0x49a3c). */
void format_level(int16_t level, char* text) {
  if (level < 0) {
    screen_copy(text, "   ");
    return;
  }
  sprintf(text, "%3d", level);
}

/* A satellite's cell: "%2d %3d" elevation and azimuth, or what its
 * status says (0x49a6e). */
void format_satellite_cell(satellite_cell cell, char* text) {
  char* words;

  switch (cell.status) {
    case SATELLITE_CELL_POSITION:
      sprintf(text, "%2d %3d", cell.elevation, cell.azimuth);
      return;
    case SATELLITE_CELL_NO_POSITION:
      words = "-- ---";
      break;
    case SATELLITE_CELL_IGNORED:
      words = "Ignore";
      break;
    case SATELLITE_CELL_UNHEALTHY:
      words = "Not OK";
      break;
    case SATELLITE_CELL_ACQUIRING:
      words = "Acq   ";
      break;
    case SATELLITE_CELL_SYNCHRONIZED:
      words = "Acq . ";
      break;
    case SATELLITE_CELL_EPHEMERIS:
      words = "Acq ..";
      break;
    default:
      words = "      ";
      break;
  }
  screen_copy(text, words);
}

/* The screen's synchronization status, by the receiver's status code
 * (0x49db0). */
void status_screen_sync(status_snapshot_record* snapshot, char* line) {
  char* words;

  switch (snapshot->status_code) {
    case GPS_SYNC_RECEIVER_ERROR:
      words = "Invalid: GPS rcvr err";
      break;
    case GPS_SYNC_ABSENT:
      words = "Absent or freq error";
      break;
    case GPS_SYNC_NOT_TRACKING:
      words = "Invalid: not tracking";
      break;
    case GPS_SYNC_INACCURATE_POSITION:
      words = "Invalid: inacc position";
      break;
    case GPS_SYNC_RAIM_ERROR:
      words = "Invalid: Time RAIM error";
      break;
    case GPS_SYNC_ASSESSING:
      words = "Assessing stability";
      break;
    case GPS_SYNC_ASSESSING_27:
      words = "Assessing stability .";
      break;
    case GPS_SYNC_ASSESSING_53:
      words = "Assessing stability ..";
      break;
    case GPS_SYNC_ASSESSING_77:
      words = "Assessing stability ...";
      break;
    case GPS_SYNC_SYNCHRONIZED:
      words = snapshot->utc_time == ONCORE_TIME_UTC
                  ? "Synchronized to UTC"
                  : "Synchronized to GPS Time";
      break;
    default:
      words = "";
      break;
  }
  screen_copy(line, words);
}

/* The screen's time line: the time's scale, GPS or UTC, local with a
 * time zone, the time, "[?]" while the clock is not set, and the date
 * (0x49f30). */
void status_screen_time(status_snapshot_record* snapshot, char* line) {
  char* scale;
  char time_text[9];
  char date_text[13];

  if (snapshot->time_zone[0] == 0 && snapshot->time_zone[1] == 0) {
    scale = snapshot->utc_time == ONCORE_TIME_GPS ? "GPS" : "UTC";
  } else {
    scale = snapshot->utc_time == ONCORE_TIME_GPS ? "LOCL GPS" : "LOCAL";
  }
  format_time(snapshot->time, time_text);
  format_date(snapshot->date >> 16, snapshot->date >> 8, snapshot->date,
              date_text);
  sprintf(line, "%-9.9s%8s %3s %11s", scale, time_text,
          snapshot->clock_invalid == 0 ? "[?]" : "", date_text);
}

/* The rows of the position column status_screen_position fills, beside
 * the satellites. */
#define SCREEN_ROW_ANTENNA_DELAY 1
#define SCREEN_ROW_POSITION 2
#define SCREEN_ROW_MODE 3
#define SCREEN_ROW_SUSPENDED 4
#define SCREEN_ROW_LATITUDE 5
#define SCREEN_ROW_LONGITUDE 6

/* The survey's progress: the positions averaged, at most the survey's
 * length (0x4a072, 0x4a19e). */
static uint32_t screen_survey_done(const status_snapshot_record* snapshot) {
  return snapshot->average_count < (uint32_t)snapshot->douglas_duration
             ? snapshot->average_count
             : (uint32_t)snapshot->douglas_duration;
}

/* The mode row's survey: "Survey:  ", the percent done, "% complete"
 * (0x4a066). */
static void screen_survey_line(status_snapshot_record* snapshot, char* line) {
  static const float PERCENT = 100.0f;
  static const float LEAST = 0.1f;
  static const float MOST = 99.9f;
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
        PERCENT);

    if (float_compare(LEAST, fraction) > 0) {
      percent = " <0.1";
    } else if (float_compare(fraction, MOST) <= 0 ||
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
 * (0x4a1c4). */
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
 * the angle (0x4a1f4, 0x4a238). */
static void screen_angle_line(status_snapshot_record* snapshot, int32_t mas,
                              char longitude, char* axis, char* line) {
  char angle[16];
  char label[10];

  format_dms(mas, longitude, angle);
  screen_copy(label, snapshot->position_source);
  strcat(label, axis);
  sprintf(line, "%-8s %s", label, angle);
}

/* The screen's position column by row: the antenna delay, the
 * position's heading, the survey's mode and why it is suspended, the
 * latitude and the longitude; no other row writes the line
 * (0x49fcc). */
void status_screen_position(uint8_t row, status_snapshot_record* snapshot,
                            char* line) {
  static const double NANOSECONDS = 1e9;
  static const char NANOSECOND_RESOLUTION = -9;
  static const uint8_t EVERY_DIGIT = 0;
  char delay[16];
  char* reason;

  switch (row) {
    case SCREEN_ROW_ANTENNA_DELAY:
      format_si(double_divide_reversed(
                    NANOSECONDS, unsigned_to_double(snapshot->pps_cable_delay)),
                NANOSECOND_RESOLUTION, EVERY_DIGIT, SCREEN_SECONDS, delay);
      sprintf(line, "ANT DLY  %s", delay + 1);
      break;
    case SCREEN_ROW_POSITION:
      screen_copy(line, "Position ________________________");
      break;
    case SCREEN_ROW_MODE:
      screen_copy(line, "MODE     ");
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
          screen_survey_done(snapshot) < (uint32_t)snapshot->douglas_duration) {
        reason = screen_survey_suspended(snapshot);
      }
      if (reason != NULL) {
        sprintf(line, "%9sSuspended: %13.13s", "", reason);
      } else {
        screen_copy(line, "");
      }
      break;
    case SCREEN_ROW_LATITUDE:
      screen_angle_line(snapshot, snapshot->position[0], 0, "LAT", line);
      break;
    case SCREEN_ROW_LONGITUDE:
      screen_angle_line(snapshot, snapshot->position[1], 1, "LON", line);
      break;
    default:
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
 * (0x4a30c). */
static int16_t screen_tfom_code(const status_snapshot_record* snapshot) {
  union libc_float tfom = {.value = snapshot->tfom};

  return tfom_code(tfom.bits);
}

/* The screen's column beside the mode lines by row: TFOM and FFOM, the
 * 1 PPS TI, the holdover threshold and the uncertainty's heading; no
 * other row writes the line (0x4a29c). */
void status_screen_tfom(uint8_t row, status_snapshot_record* snapshot,
                        char* line) {
  static const char TI_RESOLUTION = -10;
  static const char THRESHOLD_RESOLUTION = -9;
  static const uint8_t EVERY_DIGIT = 0;
  static const uint8_t LIMIT_OFF = 1; /* holdover_limit_state: no limit */
  static const uint8_t ROOMY = 10;    /* TI texts that fit the long name */
  static const uint8_t CLOSE = 15;    /* the short */
  char text[34];
  uint8_t length;

  switch (row) {
    case SCREEN_ROW_TFOM:
      if (snapshot->ffom_valid == 0) {
        screen_copy(text, "--");
      } else {
        sprintf(text, "%d", snapshot->ffom);
      }
      sprintf(line, "TFOM     %d%13sFFOM    %2s", screen_tfom_code(snapshot),
              "", text);
      break;
    case SCREEN_ROW_INTERVAL:
      /* The flag is interval_mean_flag's first byte (0x4a330). */
      if (snapshot->interval_mean_flag >> 8 == 0) {
        screen_copy(text, " --");
      } else {
        format_si(float_to_double(snapshot->interval_mean), TI_RESOLUTION,
                  EVERY_DIGIT, "s ", text);
        length = screen_length(text);
        if (length <= ROOMY) {
          strcat(text, "relative to GPS");
        } else if (length <= CLOSE) {
          strcat(text, "rel to GPS");
        } else {
          strcat(text, "rel GPS");
        }
      }
      sprintf(line, "1PPS TI %s", text);
      break;
    case SCREEN_ROW_HOLD_THRESHOLD:
      if (snapshot->holdover_limit_state == LIMIT_OFF) {
        screen_copy(text, " Off");
      } else {
        format_si(float_to_double(snapshot->holdover_limit_threshold),
                  THRESHOLD_RESOLUTION, EVERY_DIGIT, SCREEN_SECONDS, text);
      }
      sprintf(line, "HOLD THR %s", text + 1);
      break;
    case SCREEN_ROW_UNCERTAINTY:
      screen_copy(line, "Holdover Uncertainty ____________");
      break;
    default:
      break;
  }
}

/* The screen's height line: the position's source, HGT, and the height
 * in m above mean sea level (0x49b06). */
void status_screen_height(status_snapshot_record* snapshot, char* line) {
  static const float CENTIMETERS_PER_METER = 100.0f;
  float height = float_divide(integer_to_float(snapshot->position[2]),
                              CENTIMETERS_PER_METER);
  char value[21];
  char label[9];

  screen_copy(label, snapshot->position_source);
  strcat(label, "HGT");
  sprintf(value, "%+9.2f m  (MSL)", float_to_double(height));
  sprintf(line, "%-15s%s", label, value);
}

/* The suffix of the power-up line, or of the recovery line by its own
 * index, power-up's fine frequency or phase alignment; with the TI
 * reading for those that settle it (0x4a518). */
void status_screen_mode_suffix(char line, status_snapshot_record* snapshot,
                               char* text) {
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
    screen_copy(text, words);
  }
}

/* Whether bit bit of a value is set, the value sign-extended
 * (0x4a42c). */
bool bit_is_set(int16_t value, int16_t bit) {
  return (value & screen_bit(bit)) != 0;
}

/* 1 if any of bits 1 to 3 is set: the internal power supplies'
 * (0x4a448). */
uint8_t bits_1_to_3_any(int16_t value) {
  return bit_is_set(value, 1) | bit_is_set(value, 2) | bit_is_set(value, 3);
}

/* 1 if bit 4 or 5 is set: the oven's power (0x4a494). */
uint8_t bits_4_5_any(int16_t value) {
  return bit_is_set(value, 4) | bit_is_set(value, 5);
}

/* 1 if bit 6 or 7 is set: the EFC's (0x4a4c0). */
uint8_t bits_6_7_any(int16_t value) {
  return bit_is_set(value, 6) | bit_is_set(value, 7);
}

/* 1 if bit 8 or 9 is set: the GPS receiver's (0x4a4ec). */
uint8_t bits_8_9_any(int16_t value) {
  return bit_is_set(value, 8) | bit_is_set(value, 9);
}

/* A health monitor test: nonzero when the hardware condition's bits say
 * the part failed.  status_screen passes it the condition and the
 * entry's bit (0x4af14), which only bit_is_set reads. */
typedef uint8_t screen_health_test(int16_t condition, int16_t bit);

/* One of the health monitor's parts, which status_screen prints as
 * "%s: %-3s%s", name, OK or Err, suffix. */
typedef struct screen_health_part {
  char* name;
  screen_health_test* test;
  int16_t bit;
  char* suffix; /* the space before the next */
} screen_health_part;

/* The health monitor's parts, by status_hardware_condition's bits
 * (0x4d076, 14 bytes each, a NULL name its end). */
static const screen_health_part SCREEN_HEALTH_PARTS[] = {
    {"Self Test", (screen_health_test*)bit_is_set, 0, "   "},
    {"Int Pwr", (screen_health_test*)bits_1_to_3_any, 1, "  "},
    {"Oven Pwr", (screen_health_test*)bits_4_5_any, 4, "  "},
    {"OCXO", (screen_health_test*)bit_is_set, 12, "  "},
    {"EFC", (screen_health_test*)bits_6_7_any, 6, "  "},
    {"GPS Rcv", (screen_health_test*)bits_8_9_any, 9, ""},
};

/* The mode lines, Locked, Recovery, Holdover and Power-up, the snapshot's
 * marked (status_screen_mode_lines, 0x4d032). */
static const status_screen_mode_line SCREEN_MODE_LINES[] = {
    {STATUS_SCREEN_LINE_LOCKED, 0, "Locked to "},
    {STATUS_SCREEN_LINE_RECOVERY, 0, "Recovery"},
    {STATUS_SCREEN_LINE_HOLDOVER, 0, "Holdover"},
    {STATUS_SCREEN_LINE_POWERUP, 0, "Power-up"},
};

/* The mode line the snapshot's mode marks (0x4a9be). */
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

/* The holdover line's suffix: manual, or why it waits (0x4aa88). */
static char* screen_holdover_suffix(const status_snapshot_record* snapshot) {
  if (snapshot->mode == PLL_MODE_HOLDOVER_MANUAL) {
    return ": manually initiated";
  }
  switch (snapshot->holdover_waiting_reason) {
    case HOLD_CAUSE_MANUAL:
      return ": manually initiated";
    case HOLD_CAUSE_GPS:
      return snapshot->pll_field_3d5 == 0 ? ": GPS 1PPS CLK invalid"
                                          : ": EXT 1PPS CLK invalid";
    case HOLD_CAUSE_TI_ERROR:
    case HOLD_CAUSE_HARDWARE_FAILURE:
      return ": internal hardware problem";
    case HOLD_CAUSE_TI_LIMIT_EXCEEDED:
      return ": 1PPS TI exceeds hold threshold";
    default:
      return "";
  }
}

/* The marked mode line's suffix, into text where it builds one
 * (0x4aa0e). */
static char* screen_mode_suffix(status_screen_line line, bool locked,
                                status_snapshot_record* snapshot, char* text) {
  switch (line) {
    case STATUS_SCREEN_LINE_LOCKED:
      screen_copy(text, snapshot->pll_field_3d5 == 0 ? "GPS" : "Ext");
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

/* The satellite columns' rows, beside the position column. */
static const uint8_t SCREEN_SATELLITE_ROWS = 6;

/* Prints the receiver status screen from a snapshot through write and
 * print, after clearing the terminal for clear 1: synchronization, the
 * mode lines with TFOM, FFOM, the TI and holdover beside them,
 * acquisition with the satellites and the position, and the health
 * monitor (0x4a8ea). */
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
  bool attempting;
  bool failed;
  uint8_t raim_option = 0;
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
  write(" \r\n");
  write("SYNCHRONIZATION ............................");
  locked = false;
  switch (snapshot->stage) {
    case PLL_STAGE_POWERUP:
    case PLL_STAGE_DIAG:
    case PLL_STAGE_FATAL_ERROR:
      words = "............... [ Outputs Invalid ]";
      break;
    case PLL_STAGE_NORMAL_PLL:
      locked = true;
      words = "................. [ Outputs Valid ]";
      break;
    default:
      words = " [ Outputs Valid/Reduced Accuracy ]";
      break;
  }
  print("%s\r\n", words);

  write(
      "SmartClock Mode ___________________________   Reference Outputs "
      "_______________\r\n");
  current = screen_mode_line(snapshot->mode);
  row = 1;
  for (mode_line = SCREEN_MODE_LINES;
       mode_line < SCREEN_MODE_LINES +
                       sizeof SCREEN_MODE_LINES / sizeof SCREEN_MODE_LINES[0];
       mode_line++) {
    if (current == mode_line->mode) {
      suffix = screen_mode_suffix(current, locked, snapshot, text);
      screen_copy(line, ">> ");
    } else {
      suffix = "";
      screen_copy(line, "   ");
    }
    strcat(line, mode_line->text);
    strcat(line, suffix);
    print("%-46.46s", line);
    status_screen_tfom(row, snapshot, line);
    print("%s\r\n", line);
    row++;
  }

  if (snapshot->kenneth_predicted_valid == 0) {
    screen_copy(text, " --");
  } else {
    format_si(float_to_double(snapshot->kenneth_predicted), snapshot->field_031,
              PREDICTION_DIGITS, SCREEN_SECONDS, text);
    strcat(text, "/initial 24 hrs");
  }
  print("%43s   Predict  %s\r\n", "", text + 1);
  if (current == STATUS_SCREEN_LINE_HOLDOVER ||
      current == STATUS_SCREEN_LINE_RECOVERY) {
    screen_copy(line, "Holdover Duration:");
    format_duration_short(snapshot->holdover_duration, text);
    strcat(line, text);
  } else {
    screen_copy(line, " ");
  }
  if (snapshot->kenneth_present_valid == 0) {
    label = "";
    screen_copy(text, " ");
  } else {
    label = "Present";
    format_si(float_to_double(snapshot->kenneth_present), snapshot->field_02b,
              PREDICTION_DIGITS, SCREEN_SECONDS, text);
  }
  print("%43s   %s  %s\r\n", line, label, text + 1);

  write("ACQUISITION ..........................................");
  if (snapshot->pll_field_3d5 == 0) {
    words = snapshot->reference_valid != 0 && snapshot->pps_dead == 0
                ? ".. [ GPS 1PPS CLK Valid ]"
                : " [ GPS 1PPS CLK Invalid ]";
  } else {
    words = snapshot->gps_field_532[0] != 0 && snapshot->gps_field_532[1] == 0
                ? ".. [ EXT 1PPS CLK Valid ]"
                : " [ EXT 1PPS CLK Invalid ]";
  }
  print("%s\r\n", words);
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
  write("Satellite Status __________________________   ");
  print("Time _____%s\r\n", words);
  print("Tracking: %d        Not Tracking: %-2d%11s", snapshot->tracked_count,
        snapshot->not_tracked_count, "");
  status_screen_time(snapshot, line);
  print("%s\r\n", line);
  print(
      "%-16.16s   %-11.11s  %-11.11s%3s",
      snapshot->tracked_count == 0 ? "" : "PRN  El  Az   SS",
      snapshot->not_tracked_count == 0 ? "" : "PRN  El  Az",
      snapshot->not_tracked_count <= SCREEN_SATELLITE_ROWS ? "" : "PRN  El  Az",
      "");
  status_screen_sync(snapshot, text);
  print("%s %s\r\n", snapshot->pll_field_3d5 == 0 ? "1PPS CLK" : "GPS 1PPS",
        text);

  attempting = false;
  for (row = 1; row <= SCREEN_SATELLITE_ROWS; row++) {
    const satellite_list_entry* first = not_tracked;
    const satellite_list_entry* second = not_tracked + SCREEN_SATELLITE_ROWS;

    format_satellite_number(tracked->satellite, tracked_number);
    format_level(tracked->level, tracked_level);
    format_satellite_cell(tracked->cell, tracked_cell);
    format_satellite_number(first->satellite, first_number);
    attempting |= mark_flag(first->attempting, (uint8_t*)first_number);
    format_satellite_cell(first->cell, first_cell);
    format_satellite_number(second->satellite, second_number);
    attempting |= mark_flag(second->attempting, (uint8_t*)second_number);
    format_satellite_cell(second->cell, second_cell);
    status_screen_position(row, snapshot, line);
    print("%3.3s  %6.6s  %3.3s   %3.3s  %6.6s  %3.3s  %6.6s%3s%s\r\n",
          tracked_number, tracked_cell, tracked_level, first_number, first_cell,
          second_number, second_cell, "", line);
    tracked++;
    not_tracked++;
  }
  print("ELEV MASK %2d deg%3s%-25.25s%2s", snapshot->mask_angle, "",
        attempting ? "*attempting to track" : "", "");
  status_screen_height(snapshot, line);
  print("%s\r\n", line);

  line[0] = '\0';
  failed = false;
  for (part = SCREEN_HEALTH_PARTS;
       part < SCREEN_HEALTH_PARTS +
                  sizeof SCREEN_HEALTH_PARTS / sizeof SCREEN_HEALTH_PARTS[0];
       part++) {
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

/* :SYSTem:PRINt:LENGth?'s answer, the screen's lines (0x4f596). */
uint8_t status_screen_length(void) {
  static const uint8_t LINES = 23;

  return LINES;
}
