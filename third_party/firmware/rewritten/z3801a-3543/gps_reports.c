/* The console's GPS reports: the GPS Receiver Command Log, the GPS
 * Process Monitor and the pages of what the Oncore reports, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/gps/reports`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The records are docs/firmware/structures.md's
 * `gps`, `gps_counts` and `command_log`; the Oncore's units are
 * docs/firmware/gps.md's. */

/* The divisors that make an Oncore's integer units printable: tenths
 * (DOPs), hundredths (cm to m) and thousandths (mm/s to m/s, ms to s). */
static const float GPS_REPORTS_TENTHS = 10.0f;

static const float GPS_REPORTS_HUNDREDTHS = 100.0f;

/* The ignored PRNs' list of the Instrument State Settings: four
 * columns of 31 characters and a NUL. */
#define GPS_REPORTS_IGNORED_COLUMNS 4

#define GPS_REPORTS_IGNORED_COLUMN_SIZE 32

/* The channels of @@Ba, @@Bn and @@Ce. */
static const uint8_t GPS_REPORTS_CHANNELS =
    sizeof gps.status.channels / sizeof gps.status.channels[0];

/* A signed count of units, divided, for a "%f". */
static double gps_reports_scaled(int32_t value, float divisor) {
  return float_to_double(float_divide(integer_to_float(value), divisor));
}

/* An unsigned count of units, divided, for a "%f". */
static double gps_reports_scaled_unsigned(uint32_t value, float divisor) {
  return float_to_double(float_divide(unsigned_to_float(value), divisor));
}

/* The entry after one in a command log's ring. */
static gps_command_log_entry* gps_reports_log_next(
    gps_command_log* log, gps_command_log_entry* entry) {
  static const uint32_t ENTRIES = sizeof log->entries / sizeof log->entries[0];

  entry++;
  return entry >= log->entries + ENTRIES ? log->entries : entry;
}

/* Logs a command gpsm_task sends, with the first byte after its ID as
 * the message text gives it in hex, and @@Ba's time made local, or the
 * clock's while it is not set; the oldest entry goes once the log is
 * full; 1 (0x4c62c). */
int32_t command_log_add(gps_running_command* command, char* text) {
  /* the hex digits' place in gps_message_to_text's text, after "@@" and
   * the ID */
  static const uint8_t ARGUMENT_TEXT = 4;
  gps_command_log_entry entry;

  entry.code = command->request.code;
  entry.sender = command->request.sender;
  entry.message = command->message;
  entry.argument_text[0] = text[ARGUMENT_TEXT];
  entry.argument_text[1] = text[ARGUMENT_TEXT + 1];
  entry.argument_text[2] = 0;
  entry.query = command->query;
  if (get_date_time(&entry.date, &entry.time)) {
    entry.from_clock = 0;
    entry.date = oncore_date_pack(gps.status.date);
    entry.time = gps.status.time;
    utc_to_local(&entry.date, &entry.time);
  } else {
    entry.from_clock = 1;
  }

  gps_command_log_entry* next =
      gps_reports_log_next(&command_log, command_log.write);

  if (next == command_log.read) {
    command_log.read = gps_reports_log_next(&command_log, command_log.read);
  }
  /* The whole entry, its two bytes never set as the image's frame
   * leaves them. */
  *command_log.write = entry;
  command_log.write = next;
  return 1;
}

/* The name of a GPS request's sender, as the logs print it. */
static const char* gps_reports_sender_name(gps_request_sender sender) {
  switch (sender) {
    case GPS_SENDER_SEQUENCE:
      return "PLL";
    case GPS_SENDER_SETTING:
      return "SCPI1";
    case GPS_SENDER_COMMAND_MONITOR:
      return "GPSc";
    case GPS_SENDER_STATUS_MONITOR:
      return "GPSs";
    default:
      return "?";
  }
}

/* Takes a command log's oldest entry and prints it into a line; 0 when
 * the log is empty, else 1 (0x4c712). */
int32_t gps_print_command(gps_command_log* log, char* line) {
  if (log->read == log->write) {
    return 0;
  }
  gps_command_log_entry entry = *log->read;
  char date[12];
  char time[10];

  log->read = gps_reports_log_next(log, log->read);
  format_date(clock_date_of(entry.date), date);
  format_time(entry.time, time);
  sprintf(line, "%-5s %02d  %-2s %-2s [%s]   %11s  %8s %s",
          gps_reports_sender_name(entry.sender), (int32_t)entry.code,
          oncore_messages[entry.message].id, entry.argument_text,
          entry.query != GPS_ARGUMENTS_SET ? "q" : "c", date, time,
          entry.from_clock != 0 ? " " : "e");
  return 1;
}

/* Prints a command log, taking its entries (0x4c8fe). */
void gps_print_command_log(gps_command_log* log) {
  /* the most a log holds, one entry short of its ring */
  static const uint8_t MOST_COMMANDS = 20;
  char line[46];
  uint8_t printed;

  psos_pause(1);
  console_write("\r\n");
  console_write("GPS Receiver Command Log __________");
  printf(" [last %2d]\r\n", MOST_COMMANDS);
  console_write("Task  ID  Command     Date and Time Issued\r\n");
  console_write("---------------------------------------------\r\n");
  for (printed = 0; printed < MOST_COMMANDS; printed++) {
    if (gps_print_command(log, line) != 1) {
      break;
    }
    printf("%s\r\n", line);
  }
  if (printed == 0) {
    console_write("(no commands were issued since last read)\r\n\r\n");
  }
}

/* Prints the status snapshots frozen at a GPS receiver error, the
 * latest and the flagged one, and the command log copied with them,
 * leaving the copy to print again (0x4c9a0). */
void gps_print_1pps(void) {
  psos_pause(1);
  console_write("\r\n");
  if (status_snapshots_frozen == 0) {
    console_write("(no GPS receiver errors were logged)\r\n");
    return;
  }
  status_screen(&status_snapshot_latest, 0, console_write,
                (printf_function*)printf);
  console_write("\r\n\r\n");
  status_screen(&status_snapshot_flagged, 0, console_write,
                (printf_function*)printf);
  console_write("\r\n\r\n");
  gps_command_log_entry* read = command_log_snapshot.read;

  gps_print_command_log(&command_log_snapshot);
  command_log_snapshot.read = read;
}

/* Prints @@Cj, the receiver's identification (0x4937e). */
void gps_print_id(void) {
  psos_pause(1);
  console_write("\r\n\r\nGPS Receiver Identification\r\n");
  console_write("-----------------------------------------------------\r\n");
  printf("Model Number:       %s\r\n", gps.receiver_id.model_number);
  printf("Serial Number:      %s\r\n", gps.receiver_id.serial_number);
  printf("Manufacture Date:   %s\r\n", gps.receiver_id.manufacture_date);
  printf("Installed Options:  %s\r\n\r\n", gps.receiver_id.options);
  printf("Hardware P/N:       %s\r\n", gps.receiver_id.hardware_part_number);
  printf("Software P/N:       %s\r\n", gps.receiver_id.software_part_number);
  printf("Software Version:   %s\r\n", gps.receiver_id.software_version);
  printf("Software Revision:  %s\r\n", gps.receiver_id.software_revision);
  printf("Software Date:      %s\r\n\r\n", gps.receiver_id.software_date);
  printf("Copyright Notice:   %s\r\n", gps.receiver_id.copyright);
}

/* Prints an @@Ba record: time, position, heights, DOP and the channels
 * (0x49012).  The DOP's type is the byte at +0x22, which the record
 * calls pad_22. */
void gps_print_status(oncore_status_fields* status) {
  char text[80];

  psos_pause(1);
  printf("TIME %2d:%02d:%02d.%09d\tDATE  %2d/%02d/%04d\r\n",
         (uint32_t)status->time.hour, (uint32_t)status->time.minute,
         (uint32_t)status->time.second, status->fraction,
         (uint32_t)status->date.month, (uint32_t)status->date.day,
         (uint32_t)status->date.year);
  format_angle(text, status->latitude, 1);
  printf("LAT  %s\r\n", text);
  format_angle(text, status->longitude, 0);
  printf("LON  %s\t\tVIS SATS   %d\r\n", text, (uint32_t)status->visible_count);
  printf("HGT (msl)%18.2f\tTRACK SATS   %d\r\n",
         gps_reports_scaled(status->height_msl, GPS_REPORTS_HUNDREDTHS),
         (uint32_t)status->tracked_count);
  printf("HGT (gps)%18.2f\tRX STATUS   %02X\r\n",
         gps_reports_scaled(status->height_gps, GPS_REPORTS_HUNDREDTHS),
         (uint32_t)status->receiver_status);
  char* format =
      status->pad_22 == ONCORE_DOP_PDOP ? "PDOP%23.1f\r\n" : "HDOP%23.1f\r\n";

  printf(format, gps_reports_scaled_unsigned(status->dop, GPS_REPORTS_TENTHS));
  console_write("\r\n");
  console_write("CHN      SVID      MODE       SS      STATUS\r\n");
  for (int32_t channel = 0; channel < GPS_REPORTS_CHANNELS; channel++) {
    oncore_channel_status* tracked = &status->channels[channel];

    /* The mode sign-extended, as the image passes it. */
    printf("%2d%10d%10d%10d        %02X\r\n", channel + 1,
           (uint32_t)tracked->satellite, (int32_t)(int8_t)tracked->mode,
           (uint32_t)tracked->signal_strength, (uint32_t)tracked->status);
  }
}

/* Prints gps.status as three "gs: " lines, for the console word
 * abr_stat (0x491ce). */
void gps_debug_status(void) {
  char part[80];
  char line[80];

  debug_sprintf(line, "gs: %02d:%02d:%02d.%09d,%2d/%02d/%04d,",
                (uint32_t)gps.status.time.hour,
                (uint32_t)gps.status.time.minute,
                (uint32_t)gps.status.time.second, gps.status.fraction,
                (uint32_t)gps.status.date.month, (uint32_t)gps.status.date.day,
                (uint32_t)gps.status.date.year);
  format_angle(part, gps.status.latitude, 1);
  strcat(line, part);
  format_angle(part, gps.status.longitude, 0);
  strcat(line, part);
  printf("%s,%4.2f\r\n", line,
         gps_reports_scaled(gps.status.height_msl, GPS_REPORTS_HUNDREDTHS));
  debug_sprintf(line, "gs: %d,%d,%02X,", (uint32_t)gps.status.visible_count,
                (uint32_t)gps.status.tracked_count,
                (uint32_t)gps.status.receiver_status);
  printf("%s%2.1f\r\n", line,
         gps_reports_scaled_unsigned(gps.status.dop, GPS_REPORTS_TENTHS));
  libc_copy(line, "gs: ");
  for (uint32_t channel = 0; channel < GPS_REPORTS_CHANNELS; channel++) {
    oncore_channel_status* tracked = &gps.status.channels[channel];

    debug_sprintf(part, "%02d,%1d,%03d,%02X,", (uint32_t)tracked->satellite,
                  (int32_t)(int8_t)tracked->mode,
                  (uint32_t)tracked->signal_strength,
                  (uint32_t)tracked->status);
    if (channel == GPS_REPORTS_CHANNELS - 1) {
      part[libc_length(part) - 1] = 0; /* the last comma */
    }
    strcat(line, part);
  }
  strcat(line, "\r\n");
  console_write(line);
}

/* Prints @@Bb, the satellites predicted visible (0x48f6a). */
void gps_print_visibility(void) {
  psos_pause(1);
  printf("Visible Sats: %2d\r\n", (uint32_t)gps.visibility.count);
  console_write("\r\n");
  console_write("PN Code ID    Doppler    Elevation     Azimuth    Health\r\n");
  for (int32_t index = 0; index < gps.visibility.count; index++) {
    oncore_visible_satellite* visible = &gps.visibility.satellites[index];

    /* The health sign-extended, as the image passes it. */
    printf("   %2d          %5d         %2d          %3d        %02X\r\n",
           (uint32_t)visible->satellite, (int32_t)visible->doppler,
           (uint32_t)visible->elevation, (uint32_t)visible->azimuth,
           (int32_t)(int8_t)visible->health);
  }
}

/* Prints @@Bc, the best satellite combinations by xDOP (0x481e2).  Its
 * count is taken against a signed byte, as the image's loop does. */
void gps_print_best_combinations(void) {
  psos_pause(1);
  console_write("\r\n");
  printf("xDOP TYPE: %1d  VALID COMBOS: %1d  BEST-4 or N-IN-VIEW: %1d\r\n",
         (int32_t)(int8_t)gps.xdop_table.type, (uint32_t)gps.xdop_table.count,
         (int32_t)(int8_t)gps.xdop_table.mode);
  console_write("\r\nxDOP VALUE      SAT COMBO IDs\r\n");
  for (int8_t index = 0; index < gps.xdop_table.count; index++) {
    oncore_xdop_combination* combination = &gps.xdop_table.combinations[index];

    /* The xDOP sign-extended, as the image converts it. */
    printf(" %4.1f    ",
           gps_reports_scaled((int16_t)combination->xdop, GPS_REPORTS_TENTHS));
    for (int8_t satellite = 0; satellite < GPS_REPORTS_CHANNELS; satellite++) {
      printf("  %2d", (uint32_t)combination->satellites[satellite]);
    }
    console_write("\r\n");
  }
  console_write("\r\n");
}

/* Prints @@Bk, the extended navigation data: DOPs, velocity, the ECEF
 * position, each channel's URA and the least squares matrix's triangle
 * (0x482be). */
void gps_print_extension(void) {
  oncore_extension_fields* extension = &gps.extension;
  int16_t* matrix = extension->least_squares_inverse;

  psos_pause(1);
  console_write("\r\n");
  printf("GDOP: %6.1f  PDOP: %6.1f  HDOP: %6.1f VDOP: %6.1f TDOP: %6.1f\r\n",
         gps_reports_scaled_unsigned(extension->gdop, GPS_REPORTS_TENTHS),
         gps_reports_scaled_unsigned(extension->pdop, GPS_REPORTS_TENTHS),
         gps_reports_scaled_unsigned(extension->hdop, GPS_REPORTS_TENTHS),
         gps_reports_scaled_unsigned(extension->vdop, GPS_REPORTS_TENTHS),
         gps_reports_scaled_unsigned(extension->tdop, GPS_REPORTS_TENTHS));
  printf("MAG VAR: %4d VEL: n %5d e %5d u %5d AGE: %5d\r\n",
         (int32_t)extension->magnetic_variation,
         (int32_t)extension->velocity_north, (int32_t)extension->velocity_east,
         (int32_t)extension->velocity_up, (uint32_t)extension->correction_age);
  printf("ECEF POSITION: x %11d y %11d z %11d\r\n", extension->ecef_x,
         extension->ecef_y, extension->ecef_z);
  printf("URA PER CHANNEL: 1= %2d  2= %2d  3= %2d  4= %2d  5= %2d  6= %2d\r\n",
         (uint32_t)extension->ura[0], (uint32_t)extension->ura[1],
         (uint32_t)extension->ura[2], (uint32_t)extension->ura[3],
         (uint32_t)extension->ura[4], (uint32_t)extension->ura[5]);
  console_write("Least Squares Sym Matrix elements\r\n");
  printf("  %2d  %2d  %2d  %2d\r\n", (int32_t)matrix[0], (int32_t)matrix[1],
         (int32_t)matrix[2], (int32_t)matrix[3]);
  printf("   X  %2d  %2d  %2d\r\n", (int32_t)matrix[4], (int32_t)matrix[5],
         (int32_t)matrix[6]);
  printf("   X   X  %2d  %2d\r\n", (int32_t)matrix[7], (int32_t)matrix[8]);
  printf("   X   X   X  %2d\r\n", (int32_t)matrix[9]);
  printf("RECEIVER CLOCK BIAS: %6d  RECEIVER OSC OFFSET: %5d\r\n",
         (int32_t)extension->clock_bias,
         (uint32_t)extension->oscillator_offset);
  console_write("\r\n");
}

/* Prints @@Bn, the Time RAIM status, and each channel's time estimate,
 * starred when the channel is used for position; or that Time RAIM is
 * not installed (0x4849c). */
void gps_print_time_raim(void) {
  /* @@Bn's pulse reference past oncore_pulse_sync's two */
  static const uint8_t PULSE_EPOCH = 2;
  /* alarm_limit's unit */
  static const uint32_t ALARM_LIMIT_NS = 100;
  oncore_raim_fields* raim = &gps.raim;
  const char* text;
  const char* output;
  char limit[10];
  bool listed = false;

  psos_pause(1);
  console_write("\r\n\r\n");
  if (gps.raim_option == 0) {
    console_write("Time RAIM capability is not installed.\r\n");
    return;
  }
  printf("--------------------- Time RAIM Status ----------------------\r\n");
  printf("MODE       %-3s", raim->raim_on == 1 ? "On" : "Off");
  switch (gps.raim_assessment) {
    case GPS_RAIM_INACTIVE:
      text = "Inactive";
      break;
    case GPS_RAIM_OK:
      text = "OK";
      break;
    case GPS_RAIM_ALARM_DETECTED:
      text = "Alarm Detected";
      break;
    case GPS_RAIM_ALARM:
      text = "Alarm";
      break;
    default:
      text = "?";
      break;
  }
  printf("%17sAssessment: %-14s\r\n", "", text);
  printf("%31s# Alarms:   %d   (%d detected)\r\n", "",
         (uint32_t)gps_counts.raim_alarms, (uint32_t)gps_counts.raim_detected);
  console_write(
      "\r\nTime Solution _____________________   1PPS Pulse ____________\r\n");
  switch (raim->solution_status) {
    case ONCORE_RAIM_OK:
      text = "OK";
      break;
    case ONCORE_RAIM_ALARM:
      text = "** ALARM **";
      break;
    case ONCORE_RAIM_UNKNOWN:
      text = "Unknown";
      break;
    default:
      text = "?";
      break;
  }
  printf("STATUS     %-24s   STATUS    %s\r\n", text,
         raim->pulse_status != 0 ? "On" : "Off");
  switch (raim->raim_status) {
    case ONCORE_RAIM_DETECT_AND_ISOLATE:
      text = "Detection/Isolation";
      break;
    case ONCORE_RAIM_DETECT_ONLY:
      text = "Detection Only";
      break;
    case ONCORE_RAIM_NEITHER:
      text = "Unavailable";
      break;
    default:
      text = "?";
      break;
  }
  switch (raim->pps_mode) {
    case ONCORE_PPS_OFF:
      output = "Always Off";
      break;
    case ONCORE_PPS_ON:
      output = "Always On";
      break;
    case ONCORE_PPS_WHEN_TRACKING:
      output = "When Tracking";
      break;
    case ONCORE_PPS_WHEN_RAIM_OK:
      output = "When Time RAIM OK";
      break;
    default:
      output = "?";
      break;
  }
  printf("\r\nRAIM MODE  %-24s   OUTPUT    %s\r\n", text, output);
  /* An unknown reference leaves the RAIM mode's text: the image's
   * default sets another register. */
  if (raim->pulse_sync == ONCORE_PULSE_UTC) {
    text = "UTC";
  } else if (raim->pulse_sync == ONCORE_PULSE_GPS) {
    text = "GPS";
  } else if (raim->pulse_sync == PULSE_EPOCH) {
    text = "Epoch";
  }
  sprintf(limit, "%d ns", (uint32_t)raim->alarm_limit * ALARM_LIMIT_NS);
  printf("ALARM LIM  %-24s   TIME REF  %s\r\n", limit, text);
  sprintf(limit, "%d ns", (uint32_t)raim->sigma);
  printf("SIGMA EST  %-24s   SAWT ERR  %+d ns\r\n", limit,
         (int32_t)raim->sawtooth);
  console_write(
      "\r\nFractional Time Estimates ___________________________________\r\n");
  console_write("\r\nCh    PRN    Time (ns)\r\n");
  console_write("----------------------\r\n");
  for (uint8_t channel = 0; channel < GPS_REPORTS_CHANNELS; channel++) {
    uint8_t satellite = raim->channels[channel].satellite;

    if (satellite != 0) {
      printf(" %d     %2d%s   %9d\r\n", channel + 1, (uint32_t)satellite,
             satellite_used_for_position(satellite) != 0 ? "*" : " ",
             raim->channels[channel].time_estimate);
      listed = true;
    }
  }
  if (listed) {
    console_write("\r\n* tracking/used in time solution\r\n");
  }
}

/* Prints @@Ce, the pseudorange corrections, and their average and
 * deviation (0x487b4). */
void gps_print_pseudorange_corrections(void) {
  static const double TENTHS = 10.0;
  char week_time[18];

  psos_pause(1);
  console_write("\r\n");
  console_write("Pseudorange Correction Status ________________\r\n");
  /* the time, in tenths of a second of the week, in seconds */
  double time =
      double_divide_reversed(TENTHS, unsigned_to_double(gps.corrections.time));

  format_duration(time, week_time);
  printf("\r\nGPS TIME    %8.1f / %s\r\n", time, week_time);
  console_write("----------------------------------------------\r\n");
  console_write("Ch     PRN    Range (m)     Rate (m/s)   Eph #\r\n");
  console_write("----------------------------------------------\r\n");
  for (uint8_t channel = 0; channel < GPS_REPORTS_CHANNELS; channel++) {
    oncore_correction_channel* correction = &gps.corrections.channels[channel];

    if (correction->satellite != 0) {
      printf(" %d      %2d    %9.2f       %6.3f      %3d\r\n", channel + 1,
             (uint32_t)correction->satellite,
             gps_reports_scaled(correction->range_correction,
                                GPS_REPORTS_HUNDREDTHS),
             gps_reports_scaled(correction->rate_correction,
                                GPS_REPORTS_THOUSANDTHS),
             (uint32_t)correction->issue_of_data);
    }
  }
  console_write("\r\nDerived Statistics ___________________________\r\n");
  printf("AVG  %.2f m    STD DEV  %.2f m\r\n", gps.correction_average,
         gps.correction_deviation);
}

/* Prints the Instrument State Settings: the receiver's time, date,
 * settings, hold and initial positions, ignored satellites, DOP and
 * correction settings (0x4895e). */
void gps_print_instrument_state(void) {
  /* The ignored PRNs' list, built as one text across four columns of
   * 31 characters and split by a NUL at each column's end, as the
   * image's adjacent buffers are; the columns go on the four lines
   * after. */
  static const uint8_t LAST_GPS = 32;
  char ignored[GPS_REPORTS_IGNORED_COLUMNS * GPS_REPORTS_IGNORED_COLUMN_SIZE +
               1];
  char text[32];
  char other[16];
  const char* name;
  const char* operation;
  char sign;
  int8_t minutes;

  psos_pause(1);
  console_write(
      "\r\n------------------------- Instrument State Settings "
      "--------------------------\r\n");
  if (gps.gmt_offset.minutes < 0) {
    sign = '-';
    minutes = -gps.gmt_offset.minutes;
  } else {
    minutes = gps.gmt_offset.minutes;
    sign = gps.gmt_offset.sign == 0 ? '+' : '-';
  }
  printf("TIME   %02d:%02d:%02d    GMT CORR  %c%02dh:%02dm%24s",
         (uint32_t)gps.time.hour, (uint32_t)gps.time.minute,
         (uint32_t)gps.time.second, sign, (uint32_t)gps.gmt_offset.hours,
         (int32_t)minutes, "");
  printf("DATE   %02d/%02d/%04d\r\n", (uint32_t)gps.date.month,
         (uint32_t)gps.date.day, (uint32_t)gps.date.year);

  enabled_string(gps.position_hold, text);
  switch (gps.application_type) {
    case ONCORE_APPLICATION_AIR:
      name = "Air";
      break;
    case ONCORE_APPLICATION_HANDHELD:
      name = "Handheld";
      break;
    case ONCORE_APPLICATION_LAND:
      name = "Land";
      break;
    case ONCORE_APPLICATION_MARINE:
      name = "Marine";
      break;
    case ONCORE_APPLICATION_STATIC:
      name = "Static";
      break;
    default:
      name = "?";
      break;
  }
  printf("\r\nPOS HOLD   %-8s%42sAPPL   %s\r\n", text, "", name);
  if (gps.fix_algorithm == ONCORE_FIX_ALL_IN_VIEW) {
    name = "N-in-view";
  } else if (gps.fix_algorithm == ONCORE_FIX_BEST_FOUR) {
    name = "Best 4 satellites";
  } else {
    name = "?";
  }
  if (gps.fix_mode == ONCORE_POSITION_FIX) {
    operation = "Pos Fix";
  } else if (gps.fix_mode == ONCORE_IDLE) {
    operation = "Idle";
  } else {
    operation = "?";
  }
  printf("FIX ALG    %-17s%33sOPER   %s\r\n", name, "", operation);

  console_write("\r\nHold Position _____________________________   ");
  console_write("Initial Position _______________\r\n");
  format_angle(text, gps.hold_position.latitude, 1);
  format_angle(other, gps.position.latitude, 1);
  printf("LAT        %s  = %+10d ms   %s  = %+10d ms\r\n", text,
         gps.hold_position.latitude, other, gps.position.latitude);
  format_angle(text, gps.hold_position.longitude, 0);
  format_angle(other, gps.position.longitude, 0);
  printf("LON        %s  = %+10d ms   %s  = %+10d ms\r\n", text,
         gps.hold_position.longitude, other, gps.position.longitude);
  /* the held height in the column of its type, the ellipsoid's or mean
   * sea level's */
  if (gps.hold_height_type == ONCORE_HEIGHT_GPS) {
    sprintf(text, "%+8d cm", gps.hold_position.height);
    other[0] = 0;
  } else {
    text[0] = 0;
    sprintf(other, "%+8d cm", gps.hold_position.height);
  }
  printf("GPS HGT           %11s%24s%+8d cm\r\n", text, "",
         gps.position.height_gps);
  printf("MSL HGT           %11s%24s%+8d cm\r\n", other, "",
         gps.position.height_msl);

  console_write("\r\nSatellite Options _________________________   ");
  console_write("PRNs Ignored ___________________\r\n");
  for (uint8_t column = 0; column < GPS_REPORTS_IGNORED_COLUMNS; column++) {
    ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE] = 0;
  }
  uint32_t mask = gps.ignored_satellites;

  for (uint8_t prn = 1; mask != 0 && prn <= LAST_GPS; prn++) {
    if ((mask & 1) != 0) {
      sprintf(other, "%2d, ", (uint32_t)prn);
      strcat(ignored, other);
    }
    mask >>= 1;
  }
  uint8_t length = libc_length(ignored);

  if (length == 0) {
    libc_copy(ignored, "(none ignored)");
  } else {
    ignored[length - 2] = 0; /* the last ", " */
  }
  for (uint8_t column = 1; column < GPS_REPORTS_IGNORED_COLUMNS; column++) {
    ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE - 1] = 0;
  }
  printf("ELEV MASK  %-2d%33s%s\r\n", (uint32_t)gps.mask_angle, "", ignored);
  switch (gps.satellite_select) {
    case ONCORE_SELECT_AUTOMATIC:
      name = "Automatic";
      break;
    case ONCORE_SELECT_MANUAL:
      name = "Manual";
      break;
    case ONCORE_SELECT_HIGHEST_IN_SKY:
      name = "Highest-in-sky";
      break;
    default:
      name = "?";
      break;
  }
  printf("SELECTION  %-14s%21s%s\r\n", name, "",
         &ignored[1 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  switch (gps.xdop_type) {
    case ONCORE_XDOP_GDOP:
      name = "GDOP";
      break;
    case ONCORE_XDOP_PDOP:
      name = "PDOP";
      break;
    case ONCORE_XDOP_HDOP:
      name = "HDOP";
      break;
    case ONCORE_XDOP_VDOP:
      name = "VDOP";
      break;
    case ONCORE_XDOP_TDOP:
      name = "TDOP";
      break;
    default:
      name = "?";
      break;
  }
  printf("DOP TYPE   %-4s           DOP HYST     %4.1f   %s\r\n", name,
         gps_reports_scaled_unsigned(gps.xdop_hysteresis, GPS_REPORTS_TENTHS),
         &ignored[2 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  enabled_string(gps.ionospheric_correction, text);
  printf("ION CORR   %-8s       DOP 3D/2D    %4.1f   %s\r\n", text,
         gps_reports_scaled_unsigned(gps.xdop_threshold, GPS_REPORTS_TENTHS),
         &ignored[3 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);

  console_write("\r\nCorrection Factors and Thresholds _________   ");
  console_write("General Status _________________\r\n");
  enabled_string(gps.utc_time, text);
  if (gps.utc_time == ONCORE_TIME_GPS) {
    strcat(text, " (GPS time)");
  }
  switch (gps.leap_second_pending) {
    case ONCORE_NO_LEAP_SECOND:
      name = "None pending";
      break;
    case ONCORE_LEAP_SECOND_ADD:
      name = "+1 pending";
      break;
    case ONCORE_LEAP_SECOND_SUBTRACT:
      name = "-1 pending";
      break;
    default:
      name = "?";
      break;
  }
  printf("UTC CORR   %-19s                LEAP SEC  %-12s\r\n", text, name);
  printf("1PPS OFFS  %9d ns   POS FIX THR %3d\r\n", gps.pps_offset,
         (uint32_t)gps.position_fix_threshold);
  printf("CABLE DLY     %6d ns   DIF OUT THR %3d s\r\n", gps.pps_cable_delay,
         (uint32_t)gps.differential_output_threshold);
}

/* Prints the survey's averaged position and height, its count while
 * averaging, and how many channels' status has bit 7, used for the fix
 * (0x48e5e). */
void gps_print_average(void) {
  static const double HUNDREDTHS = 100.0;
  /* @@Ba's channel status bit, used for the fix */
  static const uint8_t CHANNEL_USED = 0x80;
  char text[80];
  uint8_t used = 0;

  psos_pause(1);
  console_write("\r\n");
  format_dms_precise(text, gps.average_latitude, 1);
  printf("AVG LAT  %+3.6f = %s\r\n", gps.average_latitude, text);
  format_dms_precise(text, gps.average_longitude, 0);
  printf("AVG LON  %+3.6f = %s\r\n", gps.average_longitude, text);
  printf("AVG HGT  %+3.6f\r\n",
         double_divide_reversed_pointer(HUNDREDTHS, &gps.average_height_msl));
  if (gps.position_hold == 0) {
    printf("N %11d\r\n", gps.average_count);
    printf("Averaging: ON ");
  } else {
    printf("Averaging: OFF");
  }
  for (uint8_t channel = 0; channel < GPS_REPORTS_CHANNELS; channel++) {
    if ((gps.status.channels[channel].status & CHANNEL_USED) != 0) {
      used++;
    }
  }
  printf("  Sats with bit 7: %2d\r\n", (uint32_t)used);
}
