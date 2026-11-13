/* Of the status screen's routines, those the function check finds the
 * decompiler's reading wrong for: the snapshot, the Z3815A 4010's on
 * this image's record (status_snapshot_record), which copies
 * interval_mean_flag and the byte after it as a word where the
 * decompiler read a long; and the screen, the 58503A 3704's with the
 * TIMEREF line selected and the GPS engine's echoes, whose mode texts
 * the decompiler kept in a byte, and its printer
 * (docs/firmware/z3817a-3752.md, "The function check"); the health
 * monitor's tests, the satellites' sets, lists and status, the angles'
 * formats and the mode lines' column and suffix, from the 58503A's and
 * the Z3815A's, carried or written as this image's where its code
 * differs; and, last, a satellite's cell and the height, time and GPS
 * 1PPS lines, written as this image's ("The export and the checks").  Its
 * comments' addresses are the Z3817A's.  Each keeps its generated prototype. */

/* satellite_status_list's lists: the satellites not tracked, then the
 * tracked. */
static const char SCREEN_LIST_NOT_TRACKED = 0;
static const char SCREEN_LIST_TRACKED = 1;

/* Takes the snapshot status_screen prints: the loop's state, the
 * reference's selection and pulse and the hardware condition under a
 * raised interrupt mask, then the GPS engine's, both references'
 * records, its satellites, twice as many not tracked listed as it has
 * channels, and its position as the engine reports it, the height the
 * MSL one where field_592's bit 6 says so (0x50c3c). */
void status_snapshot(status_snapshot_record* snapshot) {
  uint32_t date;
  time_of_day time;
  uint8_t holdover;
  const uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  gps_reference_record* const gps_reference = &gps.references[0];
  gps_reference_record* const external_reference = &gps.references[1];

  snapshot->stage = pll.stage;
  snapshot->mode = pll_mode();
  snapshot->powerup_suffix = pll_powerup_suffix_index();
  snapshot->recovery_suffix = recovery_suffix_index();
  snapshot->interval = interval_reading.value;
  snapshot->interval_valid = interval_reading.valid;
  snapshot->interval_errors = interval_reading.errors;
  snapshot->interval_tag = interval_reading.tag;
  snapshot->interval_mean = interval_mean;
  /* The flag and the byte after it, as a word (0x50c9e). */
  memcpy(&snapshot->interval_mean_flag, &interval_mean_flag,
         sizeof snapshot->interval_mean_flag);
  snapshot->tfom = pll.tfom;
  snapshot->ffom_valid = get_ffom(&snapshot->ffom);
  snapshot->kenneth_present_valid = kenneth_present_get(
      &snapshot->kenneth_present, &snapshot->kenneth_present_resolution);
  snapshot->kenneth_predicted_valid =
      kenneth_predicted_get(&snapshot->kenneth_predicted,
                            &snapshot->kenneth_predicted_resolution, &holdover);
  snapshot->holdover_waiting_reason = holdover_waiting_reason();
  snapshot->holdover_duration = pll.holdover_duration;
  snapshot->holdover_limit_state = nv_settings.holdover_limit_state;
  snapshot->holdover_limit_threshold = nv_settings.holdover_limit_threshold;
  snapshot->utc_time = gps.utc_time;
  snapshot->time_zone[0] = nv_settings.time_zone[0];
  snapshot->time_zone[1] = nv_settings.time_zone[1];
  if (get_date_time(&date, &time)) {
    snapshot->from_clock = 0;
    snapshot->time = gps.status.time;
    snapshot->date = oncore_date_pack(gps.status.date);
    utc_to_local(&snapshot->date, &snapshot->time);
  } else {
    snapshot->from_clock = 1;
    snapshot->time = time;
    snapshot->date = date;
  }
  snapshot->timeref_select = pll.timeref_select;
  snapshot->pulse_mode = pll.pulse_mode;
  gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
  gps_receiver_status_update(GPS_RECEIVER_ALTERNATE,
                             (char)gps_alternate_status());
  snapshot->douglas_duration_infinite = nv_settings.douglas_duration_infinite;
  snapshot->hardware_condition = status_hardware_condition();
  set_interrupt_mask(sr);

  snapshot->field_592 = gps.field_592;
  snapshot->gps_field_663 = gps.field_663;
  snapshot->fix_mode = gps.fix_mode;
  snapshot->reference_valid = gps_reference->valid;
  snapshot->pps_dead = gps_reference->pps_dead;
  snapshot->status_code = gps_reference->state;
  snapshot->reference_elapsed = gps_reference->elapsed;
  snapshot->external_valid = external_reference->valid;
  snapshot->external_pps_dead = external_reference->pps_dead;
  snapshot->external_status_code = external_reference->state;
  snapshot->external_elapsed = external_reference->elapsed;
  if (nv_settings.leap_minute_length == CLOCK_LEAP_MINUTE_SHORT) {
    snapshot->leap_second_pending = ONCORE_LEAP_SECOND_SUBTRACT;
  } else if (nv_settings.leap_minute_length == CLOCK_LEAP_MINUTE_LONG) {
    snapshot->leap_second_pending = ONCORE_LEAP_SECOND_ADD;
  } else {
    snapshot->leap_second_pending = ONCORE_NO_LEAP_SECOND;
  }
  snapshot->receiver_status = gps.status.receiver_status;
  snapshot->pps_cable_delay = gps.pps_cable_delay;
  snapshot->mask_angle = gps.mask_angle;
  snapshot->position_hold = gps.position_hold;
  snapshot->average_count = gps.average_count;
  snapshot->douglas_duration = nv_settings.douglas_duration;
  snapshot->gps_field_576 = gps.surveying;
  snapshot->gps_field_577 = gps.field_577;
  snapshot->channel_count = gps.channel_count;
  snapshot->status_received = gps.status_received;
  satellite_sets_build(gps.status.channels, &snapshot->satellites);
  satellite_status_list(SCREEN_LIST_NOT_TRACKED, snapshot->channel_count * 2,
                        &snapshot->satellites, gps.status.channels,
                        (gps_visibility_record*)&gps.visibility_received,
                        snapshot->not_tracked, &snapshot->not_tracked_count);
  satellite_status_list(SCREEN_LIST_TRACKED, snapshot->channel_count,
                        &snapshot->satellites, gps.status.channels,
                        (gps_visibility_record*)&gps.visibility_received,
                        snapshot->tracked, &snapshot->tracked_count);

  snapshot->position_source = "";
  snapshot->position[0] = gps.status.latitude;
  snapshot->position[1] = gps.status.longitude;
  if ((snapshot->field_592 & GPS_FIELD_592_BIT_6) != 0) {
    snapshot->position[2] = gps.status.height_msl;
    snapshot->height_type = ONCORE_HEIGHT_MSL;
  } else {
    snapshot->position[2] = gps.status.height_gps;
    snapshot->height_type = ONCORE_HEIGHT_GPS;
  }
}

/* The unit format_si appends for seconds. */
static char* const SCREEN_SECONDS = "s";

/* format_si's digits for every digit a double has. */
static const uint8_t SCREEN_EVERY_DIGIT = 0;

/* The rows of the position column status_screen_position fills, beside
 * the satellites, by its jump table (0x505d6). */
#define SCREEN_ROW_GPS_PPS 1

#define SCREEN_ROW_ANTENNA_DELAY 2

#define SCREEN_ROW_POSITION 3

#define SCREEN_ROW_MODE 4

#define SCREEN_ROW_SUSPENDED 5

#define SCREEN_ROW_LATITUDE 6

#define SCREEN_ROW_LONGITUDE 7

#define SCREEN_ROW_HEIGHT 8

/* What a row the engine has not echoed shows. */
static char* const SCREEN_NOT_HEARD = "--";

/* Why the survey is suspended, or NULL: no @@Ea or @@Ba yet, fewer than
 * four satellites used, or no 3D fix, bit 5 of the receiver status
 * (0x50726 to 0x50760). */
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
 * the angle, or "--" before the engine has echoed the position hold or
 * sent its status (0x5079a, 0x50806). */
static void screen_angle_line(status_snapshot_record* snapshot, int32_t mas,
                              char longitude, char* axis, char* line) {
  char angle[25];
  char label[11];

  if (!gps_echo_seen(GPS_ECHO_POSITION_HOLD) ||
      gps.status_received == GPS_STATUS_NONE) {
    libc_copy(angle, SCREEN_NOT_HEARD);
  } else {
    format_dms(mas, longitude, angle);
  }
  libc_copy(label, snapshot->position_source);
  strcat(label, axis);
  sprintf(line, "%-8s %s", label, angle);
}

/* The screen's position column by row: the GPS 1PPS's state with the
 * pulse, the antenna delay, the position's heading, its mode and why
 * the survey is suspended, each once the engine has echoed it, the
 * latitude, the longitude and the height, which sets height_shown,
 * cleared first; any other row is empty (0x50590). */
void status_screen_position(uint8_t row, status_snapshot_record* snapshot,
                            char* line, uint8_t* height_shown) {
  static const double NANOSECONDS = 1e9;
  static const char NANOSECOND_RESOLUTION = -9;
  char text[25];
  char* reason;

  *height_shown = 0;
  switch (row) {
    case SCREEN_ROW_GPS_PPS: {
      char* const pulse =
          snapshot->pulse_mode == REFERENCE_PULSE_1PPS ? "1PPS" : "PP2S";

      status_screen_sync(snapshot, text);
      sprintf(line, "GPS %-4s %s", pulse, text);
      break;
    }
    case SCREEN_ROW_ANTENNA_DELAY:
      if (!gps_echo_seen(GPS_ECHO_CABLE_DELAY)) {
        libc_copy(text, " --");
      } else {
        format_si(
            double_divide_reversed(
                NANOSECONDS, integer_to_double(snapshot->pps_cable_delay)),
            NANOSECOND_RESOLUTION, SCREEN_EVERY_DIGIT, SCREEN_SECONDS, text);
      }
      sprintf(line, "ANT DLY  %s", text[0] == '-' ? text : text + 1);
      break;
    case SCREEN_ROW_POSITION:
      libc_copy(line, "Position ________________________");
      break;
    case SCREEN_ROW_MODE:
      libc_copy(line, "MODE     ");
      if (!gps_echo_seen(GPS_ECHO_POSITION_HOLD)) {
        strcat(line, SCREEN_NOT_HEARD);
      } else {
        strcat(line, snapshot->position_hold == 0 ? "Survey" : "Hold");
      }
      break;
    case SCREEN_ROW_SUSPENDED:
      reason = NULL;
      if (gps_echo_seen(GPS_ECHO_POSITION_HOLD) &&
          snapshot->position_hold == 0) {
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

/* The mode lines, Locked, Recovery, Holdover and Power-up, the
 * snapshot's marked, and the null text that ends them
 * (status_screen_mode_lines, 0x53f0c).  Aligned to 2, as its type,
 * which smartclock.h packs, is not: gcc reads the text pointers with
 * long accesses, which the CPU32 faults on at an odd address. */
static const status_screen_mode_line
    __attribute__((aligned(2))) SCREEN_MODE_LINES[] = {
        {STATUS_SCREEN_LINE_LOCKED, 0, "Locked "},
        {STATUS_SCREEN_LINE_RECOVERY, 0, "Recovery"},
        {STATUS_SCREEN_LINE_HOLDOVER, 0, "Holdover"},
        {STATUS_SCREEN_LINE_POWERUP, 0, "Power-up"},
        {STATUS_SCREEN_LINE_NONE, 0, NULL},
};

/* The mode line the snapshot's mode marks (0x50fe4, its table at
 * 0x50ffa). */
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

/* Whether the snapshot's TIMEREF selection is the first line's, 0, or
 * the second's. */
static bool screen_timeref1(const status_snapshot_record* snapshot) {
  return snapshot->timeref_select == 0;
}

/* The holdover line's suffix: manual, or why it waits, the TIMEREF
 * line's validity by the selection (0x510aa to 0x5112c). */
static char* screen_holdover_suffix(const status_snapshot_record* snapshot) {
  if (snapshot->mode == PLL_MODE_HOLDOVER_MANUAL) {
    return ": manually initiated";
  }
  switch (snapshot->holdover_waiting_reason) {
    case HOLD_CAUSE_MANUAL:
      return ": manually initiated";
    case HOLD_CAUSE_GPS:
      return screen_timeref1(snapshot) ? ": TIMEREF1 invalid"
                                       : ": TIMEREF2 invalid";
    case HOLD_CAUSE_TI_ERROR:
    case HOLD_CAUSE_HARDWARE_FAILURE:
      return ": internal hardware problem";
    case HOLD_CAUSE_TI_LIMIT_EXCEEDED:
      return ": 1PPS TI exceeds hold threshold";
    case HOLD_CAUSE_GPS_COMMAND_MONITOR:
      return ": GPS receiver failure";
    default:
      return "";
  }
}

/* The marked mode line's suffix, into text where it builds one
 * (0x51030 to 0x5113c): locked, to the TIMEREF line selected. */
static char* screen_mode_suffix(status_screen_line line, bool locked,
                                status_snapshot_record* snapshot, char* text) {
  switch (line) {
    case STATUS_SCREEN_LINE_LOCKED:
      libc_copy(text,
                screen_timeref1(snapshot) ? "to TIMEREF1" : "to TIMEREF2");
      strcat(text, locked ? "" : ": stabilizing freq");
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
 * entry's bit, a long (0x51678), which only bit_is_set reads. */
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
 * (0x53f4e): the self-test, the supplies, the oven's supply, the OCXO,
 * the EFC and the GPS receiver. */
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

/* The acquisition line's dots after its heading, which its source and
 * validity take the place of (0x51352). */
static const int32_t SCREEN_ACQUISITION_DOTS = 13;

/* Prints the receiver status screen from a snapshot through write and
 * print, after clearing the terminal for clear 1: synchronization, the
 * mode lines with TFOM, FFOM, the TI and holdover beside them,
 * acquisition with the TIMEREF line selected and its pulse, the
 * satellites, a row for each of the receiver's channels, and the
 * position, and the health monitor; the leap second and the mask angle
 * only once the engine has echoed them (gps_echo_seen; 0x50ede). */
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
  char* source;
  char* validity;
  char tracked_number[5];
  char tracked_level[4];
  char tracked_cell[7];
  char first_number[5];
  char first_cell[7];
  char second_number[5];
  char second_cell[8];
  char line[81];
  char text[44];

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

  if (screen_reference_valid(snapshot->external_valid,
                             snapshot->external_pps_dead)) {
    source = "";
    validity = "Valid";
  } else {
    source = "(GPS) ";
    validity =
        screen_reference_valid(snapshot->reference_valid, snapshot->pps_dead)
            ? "Valid"
            : "Invalid";
  }
  words = snapshot->pulse_mode == REFERENCE_PULSE_1PPS ? "1PPS" : "PP2S";
  print(
      "ACQUISITION ...................................%.*s [ TIMEREF%d %s%s "
      "%s ]\r\n",
      SCREEN_ACQUISITION_DOTS -
          (uint8_t)(libc_length(source) + libc_length(validity)),
      "........", screen_timeref1(snapshot) ? 1 : 2, source, words, validity);
  print("Tracking: %d ____   ", snapshot->tracked_count);
  sprintf(text, "Not Tracking: %d ________", snapshot->not_tracked_count);
  print("%.24s   ", text);
  words = "_______________________";
  if (gps_echo_seen(GPS_ECHO_LEAP_SECOND_PENDING)) {
    switch (snapshot->leap_second_pending) {
      case ONCORE_LEAP_SECOND_ADD:
        words = " +1 leap second pending";
        break;
      case ONCORE_LEAP_SECOND_SUBTRACT:
        words = " -1 leap second pending";
        break;
      default:
        break;
    }
  }
  print("Time _____%s\r\n", words);
  print("%-16.16s   %-11.11s  %-11.11s%3s",
        snapshot->tracked_count == 0 ? "" : "PRN  El  Az   SQ",
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
  if ((snapshot->field_592 & GPS_FIELD_592_MODEL_R) != 0) {
    label = "";
    libc_copy(line, "");
  } else {
    label = "ELEV MASK";
    if (!gps_echo_seen(GPS_ECHO_MASK_ANGLE)) {
      libc_copy(line, "--");
    } else {
      sprintf(line, "%2d deg", snapshot->mask_angle);
    }
  }
  print("%-9.9s %-6.6s%3s%-25.25s%2s", label, line, "", words, "");
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
  print("%s%s\r\n",
        "HEALTH MONITOR ......................................................",
        failed ? " [ ERROR ]" : "... [ OK ]");
  print("%s", line);
}

/* Prints status_screen_snapshots[1] for port 1, else [0], without
 * clearing the terminal, for :SYSTem:PRINt? (0x5173a). */
void status_screen_print(uint8_t port, write_function* write,
                         printf_function* print) {
  static const uint8_t SECOND = 1;

  status_screen(port == SECOND ? &status_screen_snapshots[1]
                               : &status_screen_snapshots[0],
                0, write, print);
}

/* Whether bit bit of a value is set, the value sign-extended, the bit a
 * long (0x50a30). */
uint8_t bit_is_set(int16_t value, int32_t bit) {
  return (value & cpu_long_bit(bit)) != 0;
}

/* Bit 12, the internal reference's (0x50a4a). */
uint8_t bit_12_set(int16_t value) { return bit_is_set(value, 12); }

/* 1 if any of bits 1 to 3 is set: the internal power supplies'
 * (0x50a5a). */
uint32_t bits_1_to_3_any(int16_t value) {
  return bit_is_set(value, 2) | bit_is_set(value, 1) | bit_is_set(value, 3);
}

/* Bit 4, the oven's supply (0x50aa6). */
uint8_t bit_4_set(int16_t value) { return bit_is_set(value, 4); }

/* 1 if bit 6 or 7 is set: the EFC's (0x50ab6). */
uint8_t bits_6_7_any(int16_t value) {
  return bit_is_set(value, 6) | bit_is_set(value, 7);
}

/* 1 if bit 8 or 9 is set: the GPS receiver's (0x50ae2). */
uint8_t bits_8_9_any(int16_t value) {
  return bit_is_set(value, GPS_HARDWARE_PPS_BIT) | bit_is_set(value, 9);
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

/* The tracking bands a channel's mode puts a satellite in: acquiring
 * below 5, synchronized below 7, then ephemeris, compared signed
 * (0x4f8a0 to 0x4f8cc). */
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

/* For status_snapshot: the sets of satellites visible, used and
 * tracked, the tracked by their channels' modes when the used set is
 * known, and those not used; all empty unless the visible set is known
 * (0x4f824).  The Z3805A's leaves the tracked sets empty too when the
 * used set is not known, and walks six channels where this walks the
 * engine's. */
void satellite_sets_build(oncore_channel_status* channels,
                          satellite_sets* sets) {
  memset(sets, 0, sizeof *sets);
  const bool visible_known =
      visible_satellites(&sets->visible_count, &sets->visible);
  const bool used_known =
      satellites_in_use(&sets->used_count, &sets->pseudolites_used_count,
                        &sets->used, &sets->pseudolites_used);

  if (!visible_known) {
    return;
  }
  for (const oncore_channel_status* channel = channels;
       channel < channels + gps.channel_count; channel++) {
    const uint8_t id = channel->satellite;
    const int8_t mode = (int8_t)channel->mode;

    if (satellite_id_valid(SATELLITE_GPS, id) != 0) {
      satellite_add_to_set(SATELLITE_GPS, id, &sets->tracked);
      if (used_known) {
        screen_band_add(SATELLITE_GPS, id, mode, &sets->acquiring,
                        &sets->synchronized, &sets->ephemeris);
      }
    } else if (satellite_id_valid(SATELLITE_PSEUDOLITE, id) != 0) {
      satellite_add_to_set(SATELLITE_PSEUDOLITE, id,
                           &sets->pseudolites_tracked);
      if (used_known) {
        screen_band_add(
            SATELLITE_PSEUDOLITE, id, mode, &sets->pseudolites_acquiring,
            &sets->pseudolites_synchronized, &sets->pseudolites_ephemeris);
      }
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

/* The visibility record's satellite of an ID, by bisection of the
 * records, which the receiver sends sorted by number; NULL if none, or
 * while no visibility has been received (0x565c4) (0x565a0). */
oncore_visible_satellite* satellite_find(uint8_t id,
                                         gps_visibility_record* record) {
  oncore_visible_satellite* low = record->visibility.satellites;
  oncore_visible_satellite* high = low + record->visibility.count;

  if (gps.visibility_received == 0) {
    return NULL;
  }
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

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then "%4d:%02d:%06.7f" degrees, minutes and seconds
 * (0x4e2c6). */
void format_dms_precise(char* text, double mas, char latitude) {
  gps_format_dms(text, mas, latitude, "%c%4d:%02d:%06.7f");
}

/* The screen's list of satellites, tracked (those used) or not (the
 * rest, visible or tracked), by number, the GPS satellites first, each
 * with its cell, then blank entries, satellite_status_blank_entry, to
 * the list's end; count the satellites listed.  When more than the
 * list's entries are not tracked, those on a channel are kept, then the
 * highest of the rest (0x4fb88).  The 58503A 3704's: the rest are
 * gathered in a list of 12 (0x4fc10), which more than 12 overflow, a
 * firmware fault; status_snapshot asks for twice the engine's
 * channels. */
void satellite_status_list(uint8_t tracked, uint8_t entries,
                           satellite_sets* sets,
                           oncore_channel_status* channels,
                           gps_visibility_record* visibility,
                           satellite_list_entry* list, uint8_t* count) {
  const satellite_list_entry* end = list + entries;
  const satellite_list_entry blank = satellite_status_blank_entry;
  uint32_t set;
  uint8_t pseudolites;

  if (tracked == 1) {
    set = sets->used;
    pseudolites = sets->pseudolites_used;
  } else {
    set = sets->unused;
    pseudolites = sets->pseudolites_tracked_unused;
    if ((int32_t)(count_bits(pseudolites) + count_bits(set)) > entries) {
      const oncore_visibility_fields* visible = &visibility->visibility;
      screen_satellite_elevation others[12];
      screen_satellite_elevation* next = others;
      int8_t room;

      set &= sets->tracked;
      for (const oncore_visible_satellite* satellite = visible->satellites;
           satellite < visible->satellites + visible->count; satellite++) {
        if (satellite_in_set(SATELLITE_GPS, satellite->satellite, set) == 0) {
          next->satellite = satellite->satellite;
          next->elevation = satellite->elevation;
          next++;
        }
      }
      qsort(others, next - others, sizeof others[0], satellite_compare);
      /* Byte arithmetic, as the image's. */
      room = (int8_t)(entries - (uint8_t)count_bits(set) -
                      (uint8_t)count_bits(pseudolites));
      while (room-- != 0) {
        next--;
        satellite_add_to_set(SATELLITE_GPS, next->satellite, &set);
      }
    }
  }
  *count = 0;
  for (uint8_t id = GPS_FIRST_SATELLITE;
       list < end && (set != 0 || pseudolites != 0); id++, set >>= 1) {
    if (id == GPS_FIRST_PSEUDOLITE) {
      set = pseudolites;
      pseudolites = 0;
    }
    if ((set & 1) == 0) {
      continue;
    }
    list->satellite = id;
    if (tracked == 1) {
      for (const oncore_channel_status* channel = channels;
           channel < channels + gps.channel_count; channel++) {
        if (id == channel->satellite) {
          list->level = channel->signal_strength;
          break;
        }
      }
    } else {
      list->attempting =
          satellite_in_set(SATELLITE_GPS, id, sets->tracked_unused) != 0 ||
          satellite_in_set(SATELLITE_PSEUDOLITE, id,
                           sets->pseudolites_tracked_unused) != 0;
    }
    satellite_status(tracked, id, sets, visibility, &list->cell);
    (*count)++;
    list++;
  }
  for (; list < end; list++) {
    *list = blank;
  }
}

/* An angle in mas as its hemisphere, nmea_hemisphere's but blank for
 * zero, then "%4d:%02d:%06.3f" degrees, minutes and seconds; the
 * hemisphere blank too for what rounds to zero, or for a longitude to
 * 180 degrees (0x4e238). */
void format_dms(int32_t mas, char longitude, char* text) {
  uint16_t degrees;
  uint8_t minutes;
  double seconds;
  const char hemisphere =
      mas == 0 ? ' ' : (char)nmea_hemisphere(mas, longitude);

  mas_to_dms(mas, &degrees, &minutes, &seconds);
  sprintf(text, "%c%4d:%02d:%06.3f", (int32_t)hemisphere, (uint32_t)degrees,
          (uint32_t)minutes, seconds);
  if (screen_equal(text + 1, "   0:00:00.000") ||
      (longitude == 1 && screen_equal(text + 1, " 180:00:00.000"))) {
    text[0] = ' ';
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
 * 1 PPS TI relative to the reference selected, TIMEREF1 or TIMEREF2
 * ("rel to TIMEREF" or "rel TIMEREF" and its number), the holdover
 * threshold and the uncertainty's heading; any other row is empty
 * (0x50890). */
void status_screen_tfom(uint8_t row, status_snapshot_record* snapshot,
                        char* line) {
  static const char TI_RESOLUTION = -10;
  static const char THRESHOLD_RESOLUTION = -9;
  static const uint8_t ROOMY = 10; /* TI texts that fit "rel to " */
  char text[34];
  uint8_t length;

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
        strcat(text, length <= ROOMY ? "rel to TIMEREF" : "rel TIMEREF");
        strcat(text, snapshot->timeref_select == 0 ? "1" : "2");
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
 * reading for those that settle it (0x50b0e). */
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

/* A satellite's cell: "%2d %3d" elevation and azimuth, or what its
 * status says (0x4fe2c). */
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
  libc_copy(text, words);
}

/* A visible satellite's class by its health, none for no record
 * (0x4f9d0).  satellite_status passes it the long after the visibility
 * record, which it ignores, where the Z3815A 4010's takes it as Time
 * RAIM's set. */
satellite_health_class satellite_track_class(
    oncore_visible_satellite* satellite, uint32_t unused) {
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

/* A satellite's cell: none for an ID not a satellite's; for one not
 * tracked, ignored, unhealthy or acquiring, by its health and its
 * channel's band, if any; else its elevation and azimuth (0x4fa40).  The
 * Z3815A 4010's but that no class is isolated. */
void satellite_status(uint8_t tracked, uint8_t id, satellite_sets* sets,
                      gps_visibility_record* visibility, satellite_cell* cell) {
  oncore_visible_satellite* satellite;

  if ((uint8_t)satellite_id_valid(SATELLITE_ANY, id) == 0) {
    cell->status = SATELLITE_CELL_NONE;
    return;
  }
  satellite = id <= GPS_LAST_SATELLITE ? satellite_find(id, visibility) : NULL;
  if (tracked != 1) {
    /* The long after the record, which satellite_track_class ignores
     * (0x4fa8e). */
    const uint32_t* after = (const uint32_t*)(visibility + 1);

    switch (satellite_track_class(satellite, *after)) {
      case SATELLITE_CLASS_REMOVED:
        cell->status = SATELLITE_CELL_IGNORED;
        return;
      case SATELLITE_CLASS_UNHEALTHY:
        cell->status = SATELLITE_CELL_UNHEALTHY;
        return;
      default:
        break;
    }
    if ((uint8_t)satellite_in_set(SATELLITE_GPS, id, sets->acquiring) != 0 ||
        (uint8_t)satellite_in_set(SATELLITE_PSEUDOLITE, id,
                                  sets->pseudolites_acquiring) != 0) {
      cell->status = SATELLITE_CELL_ACQUIRING;
      return;
    }
    if ((uint8_t)satellite_in_set(SATELLITE_GPS, id, sets->synchronized) != 0 ||
        (uint8_t)satellite_in_set(SATELLITE_PSEUDOLITE, id,
                                  sets->pseudolites_synchronized) != 0) {
      cell->status = SATELLITE_CELL_SYNCHRONIZED;
      return;
    }
    if ((uint8_t)satellite_in_set(SATELLITE_GPS, id, sets->ephemeris) != 0 ||
        (uint8_t)satellite_in_set(SATELLITE_PSEUDOLITE, id,
                                  sets->pseudolites_ephemeris) != 0) {
      cell->status = SATELLITE_CELL_EPHEMERIS;
      return;
    }
  }
  satellite_elevation_azimuth(satellite, cell);
}

/* The screen's height line: the position's source, HGT, and the height
 * in m above mean sea level or the GPS ellipsoid, by the snapshot's
 * height type; "HGT --" until the position hold's echo is heard while
 * the status has come (0x4fec4).  The Z3815A 4010's prints the height
 * always. */
void status_screen_height(status_snapshot_record* snapshot, char* line) {
  static const float CENTIMETERS_PER_METER = 100.0f;

  if (!gps_echo_seen(GPS_ECHO_POSITION_HOLD) ||
      gps.status_received == GPS_STATUS_NONE) {
    sprintf(line, "%-8s %s", "HGT", "--");
    return;
  }
  float height = float_divide(integer_to_float(snapshot->position[2]),
                              CENTIMETERS_PER_METER);
  char value[21];
  char label[9];

  libc_copy(label, snapshot->position_source);
  strcat(label, "HGT");
  sprintf(value,
          snapshot->height_type == ONCORE_HEIGHT_MSL ? "%+9.2f m  (MSL)"
                                                     : "%+9.2f m  (GPS)",
          float_to_double(height));
  sprintf(line, "%-15s%s", label, value);
}

/* A state's text by how much of its period has passed: the last, with
 * three dots, from 77 percent, then two from 53, one from 27, else the
 * first (0x5024c to 0x502ee, 0x502f2 to 0x50390). */
static char* screen_progress_text(uint32_t elapsed, uint16_t period,
                                  char* const texts[4]) {
  static const double FRACTIONS[] = {0.77, 0.53, 0.27};
  static const uint32_t STEPS = sizeof FRACTIONS / sizeof FRACTIONS[0];

  for (uint32_t step = 0; step < STEPS; step++) {
    if (elapsed >= double_to_unsigned(double_multiply(
                       unsigned_to_double(period), FRACTIONS[step]))) {
      return texts[STEPS - step];
    }
  }
  return texts[0];
}

/* The screen's GPS 1PPS state, by the GPS reference's state as the
 * snapshot keeps it: "Absent or freq error" while it is valid with its
 * 1PPS dead; the invalid states' reasons; assessing and questionable
 * with dots by the time passed of their periods; and synchronized to UTC
 * or GPS time once the engine has echoed @@Aw, plain "Synchronized"
 * before (0x501bc, its jump table at 0x501fc). */
void status_screen_sync(status_snapshot_record* snapshot, char* line) {
  static char* const ASSESSING[] = {
      "Assessing stability", "Assessing stability .", "Assessing stability ..",
      "Assessing stability ..."};
  static char* const QUESTIONABLE[] = {
      "Questionable accuracy", "Questionable accuracy .",
      "Questionable accuracy ..", "Questionable accuracy..."};
  const gps_reference_record* reference = &gps.references[0];
  char* words;

  if (snapshot->reference_valid == 1 && snapshot->pps_dead != 0) {
    libc_copy(line, "Absent or freq error");
    return;
  }
  switch (snapshot->status_code) {
    case GPS_SYNC_RECEIVER_FAILED:
      words = "Invalid: GPS rcvr failed";
      break;
    case GPS_SYNC_NOT_TRACKING:
      words = "Invalid: not tracking";
      break;
    case GPS_SYNC_INACCURATE_POSITION:
      words = "Invalid: inacc position";
      break;
    case GPS_SYNC_READING_NAVIGATION:
      words = "Invalid: reading NAV msg";
      break;
    case GPS_SYNC_RAIM_ERROR:
      words = "Invalid: Time RAIM error";
      break;
    case GPS_SYNC_INVALID:
      words = "Invalid";
      break;
    case GPS_SYNC_ASSESSING:
      words = screen_progress_text(snapshot->reference_elapsed,
                                   reference->validation_time, ASSESSING);
      break;
    case GPS_SYNC_QUESTIONABLE:
    case GPS_SYNC_QUESTIONABLE_CLEARING:
      words = screen_progress_text(snapshot->reference_elapsed,
                                   reference->hysteresis, QUESTIONABLE);
      break;
    case GPS_SYNC_VALID:
      if (!gps_echo_seen(GPS_ECHO_UTC_SELECT)) {
        words = "Synchronized";
      } else {
        words = snapshot->utc_time == ONCORE_TIME_UTC
                    ? "Synchronized to UTC"
                    : "Synchronized to GPS Time";
      }
      break;
    default:
      words = "";
      break;
  }
  libc_copy(line, words);
}

/* The screen's time line: the time's scale, TIME until the engine has
 * echoed @@Aw, then GPS or UTC, local with a time zone; the time and the
 * date, dashes while the clock is not set and no status has come; and
 * "[?]" while the clock is not set (0x504b0). */
void status_screen_time(status_snapshot_record* snapshot, char* line) {
  char* scale;
  char time_text[9];
  char date_text[13];

  if (snapshot->from_clock == 0 && gps.status_received == GPS_STATUS_NONE) {
    libc_copy(time_text, "--:--:--");
    libc_copy(date_text, "-- --- ----");
  } else {
    format_time(snapshot->time, time_text);
    format_date(clock_date_of(snapshot->date), date_text);
  }
  if (!gps_echo_seen(GPS_ECHO_UTC_SELECT)) {
    scale = "TIME";
  } else if (snapshot->time_zone[0] == 0 && snapshot->time_zone[1] == 0) {
    scale = snapshot->utc_time == ONCORE_TIME_UTC ? "UTC" : "GPS";
  } else {
    scale = snapshot->utc_time == ONCORE_TIME_UTC ? "LOCAL" : "LOCL GPS";
  }
  sprintf(line, "%-9.9s%8s %3s %11s", scale, time_text,
          snapshot->from_clock == 0 ? "[?]" : "", date_text);
}
