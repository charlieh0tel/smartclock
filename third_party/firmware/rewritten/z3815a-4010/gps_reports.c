/* The console's GPS reports: the GT-74 Command Log, the GPS Process
 * Monitor and the pages of what the receiver reports, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/gps/reports`).  The 58503A
 * 3704's over this image's gps record and command log
 * (docs/firmware/z3815a-4010.md), with this image's own texts and
 * columns.  The exported C includes this file after its own, so the types
 * and prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The divisors that make the receiver's integer units printable: tenths
 * (DOPs) and hundredths (cm to m). */
static const float GPS_REPORTS_TENTHS = 10.0f;

static const float GPS_REPORTS_HUNDREDTHS = 100.0f;

/* The ignored PRNs' list of the Instrument State Settings: four
 * columns of 31 characters and a NUL. */
#define GPS_REPORTS_IGNORED_COLUMNS 4

#define GPS_REPORTS_IGNORED_COLUMN_SIZE 32

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

/* The status' height, the MSL one while field_592's bit 6 is set, else
 * the GPS one. */
static int32_t gps_reports_height(const oncore_status_fields* status) {
  return (gps.field_592 & GPS_FIELD_592_BIT_6) != 0 ? status->height_msl
                                                    : status->height_gps;
}

/* Logs a sentence gpsm_task sends, with up to two characters of its
 * text after its descriptor's address, and the status' time made local,
 * or the clock's while it is not set; the oldest entry goes once the log
 * is full; 1 (0x58808). */
uint32_t command_log_add(gps_running_command* command, char* text) {
  static const uint8_t ARGUMENT_CHARACTERS = 2;
  gps_command_log_entry entry;
  uint8_t length = 0;

  entry.sender = command->request.sender;
  entry.untranslated_code = command->request.untranslated_code;
  entry.code = command->request.code;
  entry.message = command->message;
  char* address = gps_request_descriptor(command->message)->address;
  char* found = strstr(text, address);

  if (found != NULL) {
    for (char* c = found + libc_length(address);
         length < ARGUMENT_CHARACTERS && *c != '\0' && *c != '<' && *c != '*';
         c++) {
      entry.argument_text[length++] = *c;
    }
  }
  entry.argument_text[length] = '\0';
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

/* The name of a GPS request's sender, as the logs print it, by its
 * gps_request_sender; "?" for none or another.  A table, so that gcc's
 * code holds no jump table, whose words objdump would decode as
 * instructions the CPU32 does not have. */
static const char* gps_reports_sender_name(gps_request_sender sender) {
  static const char* const NAMES[] = {
      [GPS_SENDER_SEQUENCE] = "PLL",
      [GPS_SENDER_SETTING] = "SCPI1",
      [GPS_SENDER_SETTING_OTHER] = "SCPI2",
      [GPS_SENDER_COMMAND_MONITOR] = "GPSc",
      [GPS_SENDER_STATUS_MONITOR] = "GPSs",
  };
  static const uint8_t COUNT = sizeof NAMES / sizeof NAMES[0];

  return sender < COUNT && NAMES[sender] != NULL ? NAMES[sender] : "?";
}

/* Prints a command log, taking its entries (0x58b86). */
void gps_print_command_log(gps_command_log* log) {
  /* the most a log holds, one entry short of its ring */
  static const uint8_t MOST_COMMANDS = 20;
  char line[62];
  uint8_t printed;

  psos_pause(1);
  console_write("\r\n");
  console_write("GT-74 Command Log ________________________________");
  printf(" [last %2d]\r\n", MOST_COMMANDS);
  console_write(
      "Task   ID      CID  Command          Date and Time Issued\r\n");
  console_write(
      "------------------------------------------------------------\r\n");
  for (printed = 0; printed < MOST_COMMANDS; printed++) {
    if ((uint8_t)gps_print_command(log, line) != 1) {
      break;
    }
    printf("%s\r\n", line);
  }
  if (printed == 0) {
    console_write("(no commands issued since last queried)\r\n\r\n");
  }
}

/* Prints the command log. */
void gps_print_commands(void) { gps_print_command_log(&command_log); }

/* Prints the status snapshots frozen at a GPS receiver error, the
 * latest and the flagged one, and the command log copied with them,
 * leaving the copy to print again (0x58c28). */
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

/* Prints tst's receiver identification (0x54822). */
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

/* Takes a command log's oldest entry and prints it into a line, its
 * translated code after the one received where they differ, and its
 * sentence's address or query; 0 when the log is empty, else 1
 * (0x5893a). */
uint32_t gps_print_command(gps_command_log* log, char* line) {
  if (log->read == log->write) {
    return 0;
  }
  gps_command_log_entry entry = *log->read;
  char date[12];
  char time[9];
  char translated[7];

  log->read = gps_reports_log_next(log, log->read);
  format_date(clock_date_of(entry.date), date);
  format_time(entry.time, time);
  const char* sender = gps_reports_sender_name(entry.sender);

  if (entry.untranslated_code == entry.code) {
    translated[0] = '\0';
  } else {
    sprintf(translated, "->%3d", (int32_t)entry.code);
  }
  const gps_sentence_descriptor* descriptor =
      gps_request_descriptor(entry.message);
  char* sentence = entry.query == GPS_ARGUMENTS_SET ? descriptor->address
                                                    : descriptor->query;

  sprintf(line, "%-5s %3d%-5s %3d  %-7.7s %-2s [%s]   %11s  %8s %s", sender,
          (int32_t)entry.untranslated_code, translated, (int32_t)entry.message,
          sentence, entry.argument_text,
          entry.query != GPS_ARGUMENTS_SET ? "q" : "c", date, time,
          entry.from_clock != 0 ? " " : "e");
  return 1;
}

/* Prints a status record: time, position, heights, DOP and the channels'
 * C/N (0x54498). */
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
  printf("LON  %s\r\n", text);
  printf("HGT (msl)%18.2f\tTRACK SATS   %d\r\n",
         gps_reports_scaled(status->height_msl, GPS_REPORTS_HUNDREDTHS),
         (uint32_t)status->tracked_count);
  printf("HGT (gps)%18.2f\tRX STATUS   %02X\r\n",
         gps_reports_scaled(status->height_gps, GPS_REPORTS_HUNDREDTHS),
         (uint32_t)status->receiver_status);
  char* format =
      status->dop_type == ONCORE_DOP_PDOP ? "PDOP%23.1f\r\n" : "HDOP%23.1f\r\n";

  printf(format, gps_reports_scaled_unsigned(status->dop, GPS_REPORTS_TENTHS));
  console_write("\r\n");
  console_write("CHN      SVID      MODE      C/N      STATUS\r\n");
  for (int32_t channel = 0; channel < gps.channel_count; channel++) {
    oncore_channel_status* tracked = &status->channels[channel];

    /* The mode sign-extended, as the image passes it. */
    printf("%2d%10d%10d%10d        %02X\r\n", channel + 1,
           (uint32_t)tracked->satellite, (int32_t)(int8_t)tracked->mode,
           (uint32_t)tracked->signal_strength, (uint32_t)tracked->status);
  }
}

/* Prints gps.status as three "gs: " lines through debug_printf, for the
 * console word abr_stat (0x54654), its height as gps_reports_height
 * picks it; the last channel's comma dropped. */
void gps_debug_status(void) {
  char part[16];
  char line[102];

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
  debug_printf("%s,%4.2f\r\n", line,
               gps_reports_scaled(gps_reports_height(&gps.status),
                                  GPS_REPORTS_HUNDREDTHS));
  debug_sprintf(line, "gs: %d,%d,%02X,", (uint32_t)gps.status.visible_count,
                (uint32_t)gps.status.tracked_count,
                (uint32_t)gps.status.receiver_status);
  debug_printf("%s%2.1f\r\n", line,
               gps_reports_scaled_unsigned(gps.status.dop, GPS_REPORTS_TENTHS));
  libc_copy(line, "gs: ");
  for (int32_t channel = 0; channel < gps.channel_count; channel++) {
    oncore_channel_status* tracked = &gps.status.channels[channel];

    debug_sprintf(part, "%02d,%1d,%03d,%02X,", (uint32_t)tracked->satellite,
                  (int32_t)(int8_t)tracked->mode,
                  (uint32_t)tracked->signal_strength,
                  (uint32_t)tracked->status);
    if (channel == gps.channel_count - 1) {
      part[libc_length(part) - 1] = 0; /* the last comma */
    }
    strcat(line, part);
  }
  strcat(line, "\r\n");
  console_write(line);
}

/* Prints the satellites GSV reports visible (0x543fe). */
void gps_print_visibility(void) {
  psos_pause(1);
  printf("Visible Sats: %2d\r\n", (uint32_t)gps.visibility.count);
  console_write("\r\n");
  console_write("PN Code ID    Elevation     Azimuth    Health\r\n");
  for (int32_t index = 0; index < gps.visibility.count; index++) {
    oncore_visible_satellite* visible = &gps.visibility.satellites[index];

    /* The health sign-extended, as the image passes it. */
    printf("   %2d             %2d          %3d        %02X\r\n",
           (uint32_t)visible->satellite, (uint32_t)visible->elevation,
           (uint32_t)visible->azimuth, (int32_t)(int8_t)visible->health);
  }
}

/* Prints the best satellite combinations by xDOP (0x53902).  Its count
 * is taken against a signed byte, as the image's loop does. */
void gps_print_best_combinations(void) {
  static const int8_t COMBINATION_SATELLITES =
      sizeof gps.xdop_table.combinations[0].satellites /
      sizeof gps.xdop_table.combinations[0].satellites[0];
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
    for (int8_t satellite = 0; satellite < COMBINATION_SATELLITES;
         satellite++) {
      printf("  %2d", (uint32_t)combination->satellites[satellite]);
    }
    console_write("\r\n");
  }
  console_write("\r\n");
}

/* Prints the extended navigation data: DOPs, velocity, the ECEF
 * position, each channel's URA and the least squares matrix's triangle
 * (0x539de). */
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

/* Prints the Time RAIM status from rrm and rsd, or that Time RAIM is not
 * installed (0x53bbc).  The 58503A's lists each channel's time estimate
 * too; this one formats the sigma and prints a blank in its place. */
void gps_print_time_raim(void) {
  /* the pulse reference past oncore_pulse_sync's two */
  static const uint8_t PULSE_EPOCH = 2;
  /* alarm_limit's unit */
  static const int32_t ALARM_LIMIT_NS = 10;
  oncore_raim_fields* raim = &gps.raim;
  const char* text;
  const char* output;
  char limit[10];

  psos_pause(1);
  console_write("\r\n\r\n");
  if ((gps.field_592 & GPS_FIELD_592_TIME_RAIM) == 0) {
    console_write("Time RAIM capability is not installed.\r\n");
    return;
  }
  printf(
      "------------------------ Time RAIM Status ------------------------\r\n");
  printf("MODE       %-16s", raim->raim_on == 1 ? "On (in Pos Hold)" : "Off");
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
  printf("%11sAssessment: %-14s\r\n", "", text);
  printf("%38s# Alarms:   %d   (%d detected)\r\n", "",
         (uint32_t)gps_counts.raim_alarms, (uint32_t)gps_counts.raim_detected);
  console_write(
      "\r\nTime Solution _____________________   1PPS Pulse "
      "_________________\r\n");
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
  printf("RAIM STAT  %-24s\r\n", text);
  switch (raim->raim_status) {
    case ONCORE_RAIM_DETECT_AND_ISOLATE:
      text = "Detection/Isolation";
      break;
    case ONCORE_RAIM_DETECT_ONLY:
      text = "Detection Only";
      break;
    case ONCORE_RAIM_NEITHER:
      text = "Neither";
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
  printf("\r\nISOL/DET   %-24s   OUTPUT    %s\r\n", text, output);
  /* An unknown reference leaves the isolation's text, as the image's
   * switch, which has no default, does. */
  if (raim->pulse_sync == ONCORE_PULSE_UTC) {
    text = "UTC";
  } else if (raim->pulse_sync == ONCORE_PULSE_GPS) {
    text = "GPS";
  } else if (raim->pulse_sync == PULSE_EPOCH) {
    text = "Epoch";
  }
  sprintf(limit, "%d ns", raim->alarm_limit * ALARM_LIMIT_NS);
  printf("ALARM LIM  %-24s   TIME REF  %s\r\n", limit, text);
  sprintf(limit, "%d ns", (uint32_t)raim->sigma);
  printf("%-38sSAWT ERR  %+d ns\r\n", "", (int32_t)raim->sawtooth);
}

/* Prints the pseudorange corrections, and their average and deviation
 * (0x53e1c). */
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
  oncore_correction_channel* corrections = gps.corrections.channels;

  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    oncore_correction_channel* correction = &corrections[channel];

    if (correction->satellite != 0) {
      /* The rate converted first, as the image does. */
      double rate = gps_reports_scaled(correction->rate_correction,
                                       GPS_REPORTS_THOUSANDTHS);
      double range = gps_reports_scaled(correction->range_correction,
                                        GPS_REPORTS_HUNDREDTHS);

      printf(" %d      %2d    %9.2f       %6.3f      %3d\r\n", channel + 1,
             (uint32_t)correction->satellite, range, rate,
             (uint32_t)correction->issue_of_data);
    }
  }
  console_write("\r\nDerived Statistics ___________________________\r\n");
  printf("AVG  %.2f m    STD DEV  %.2f m\r\n", gps.correction_average,
         gps.correction_deviation);
}

/* Prints the Instrument State Settings: the receiver's time and date,
 * position hold, position, ignored satellites and elevation mask, time
 * scale, leap second and its date, 1PPS offset, GPS-UTC offset and cable
 * delay (0x53fca).  The 58503A 3704's without the Oncore's
 * settings: no GMT offset, application, fix algorithm or operation, hold
 * and initial positions, satellite selection, DOP or corrections; the
 * position is the status', and the leap second's adjustment "+1" or "-1"
 * while one is pending, else tps' sign of one with "0", " ?" for a
 * pending value past 2. */
void gps_print_instrument_state(void) {
  static const uint8_t LAST_GPS = 32;
  /* The ignored PRNs' list, built as one text across four columns of
   * 31 characters and split by a NUL at each column's end, as the
   * image's adjacent buffers are; the last three columns go on the three
   * lines after. */
  char ignored[GPS_REPORTS_IGNORED_COLUMNS * GPS_REPORTS_IGNORED_COLUMN_SIZE +
               1];
  char other[16];
  const char* adjustment;

  psos_pause(1);
  console_write(
      "\r\n------------------------- Instrument State Settings "
      "--------------------------\r\n");
  printf("TIME   %02d:%02d:%02d%46s", (uint32_t)gps.status.time.hour,
         (uint32_t)gps.status.time.minute, (uint32_t)gps.status.time.second,
         "");
  printf("DATE   %02d/%02d/%04d\r\n", (uint32_t)gps.status.date.month,
         (uint32_t)gps.status.date.day, (uint32_t)gps.status.date.year);
  enabled_string(gps.position_hold, ignored);
  printf("\r\nPOS HOLD   %-8s\r\n", ignored);
  console_write("\r\nPosition __________________________________\r\n");
  format_angle(ignored, gps.status.latitude, 1);
  printf("LAT        %s  = %+10d ms\r\n", ignored, gps.status.latitude);
  format_angle(ignored, gps.status.longitude, 0);
  printf("LON        %s  = %+10d ms\r\n", ignored, gps.status.longitude);
  printf("GPS HGT           %+8d cm\r\n", gps.status.height_gps);
  printf("MSL HGT           %+8d cm\r\n", gps.status.height_msl);

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
  for (uint8_t column = 1; column < GPS_REPORTS_IGNORED_COLUMNS; column++) {
    printf("%46s%s\r\n", " ",
           &ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  }

  console_write("\r\nCorrection Factors and Thresholds _________   ");
  console_write("General Status _________________\r\n");
  switch (gps.leap_second_pending) {
    case ONCORE_NO_LEAP_SECOND:
      adjustment = gps.tps.leap_second == 1    ? "+0"
                   : gps.tps.leap_second == -1 ? "-0"
                                               : " 0";
      break;
    case ONCORE_LEAP_SECOND_ADD:
      adjustment = "+1";
      break;
    case ONCORE_LEAP_SECOND_SUBTRACT:
      adjustment = "-1";
      break;
    default:
      adjustment = " ?";
      break;
  }
  printf("TIME SCAL  %-15s%20sLEAP ADJ %s",
         gps.utc_time == ONCORE_TIME_UTC ? "UTC" : "GPS Time", "", adjustment);
  if (gps.tps.leap_date.year == 0) {
    console_write("\r\n");
  } else {
    printf(", %02d:%02d:%02d %02d/%02d/%04d\r\n",
           (uint32_t)gps.tps.leap_time.hour, (uint32_t)gps.tps.leap_time.minute,
           (uint32_t)gps.tps.leap_time.second,
           (uint32_t)gps.tps.leap_date.month, (uint32_t)gps.tps.leap_date.day,
           (uint32_t)gps.tps.leap_date.year);
  }
  printf("1PPS OFFS  %9d ns%23sUTC OFFS %+d s\r\n", gps.pps_offset, "",
         (uint32_t)gps.tps.utc_offset);
  printf("CABLE DLY     %6d ns\r\n", gps.pps_cable_delay_given);
}

/* Prints the survey's averaged position and height, the MSL one while
 * field_592's bit 6 is set, else the GPS one, its count while averaging,
 * and how many channels' status has bit 7 (0x542e2). */
void gps_print_average(void) {
  static const double HUNDREDTHS = 100.0;
  static const uint8_t CHANNEL_BIT_7 = 0x80;
  char text[80];
  uint8_t used = 0;

  psos_pause(1);
  console_write("\r\n");
  format_dms_precise(text, gps.average_latitude, 1);
  printf("AVG LAT  %+3.6f = %s\r\n", gps.average_latitude, text);
  format_dms_precise(text, gps.average_longitude, 0);
  printf("AVG LON  %+3.6f = %s\r\n", gps.average_longitude, text);
  double* height = (gps.field_592 & GPS_FIELD_592_BIT_6) != 0
                       ? &gps.average_height_msl
                       : &gps.average_height_gps;

  printf("AVG HGT  %+3.6f\r\n",
         double_divide_reversed_pointer(HUNDREDTHS, height));
  if (gps.position_hold == 0) {
    printf("N %11d\r\n", gps.average_count);
    printf("Averaging: ON ");
  } else {
    printf("Averaging: OFF");
  }
  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    if ((gps.status.channels[channel].status & CHANNEL_BIT_7) != 0) {
      used++;
    }
  }
  printf("  Sats with bit 7: %2d\r\n", (uint32_t)used);
}

/* "OK" or "Fail" for a monitor's failed flag. */
static const char* gps_reports_failed(uint8_t failed) {
  return failed == 0 ? "OK" : "Fail";
}

/* Prints the GPS Process Monitor: the GPS task's state and counters,
 * its last request and program sentence, the response buffer's text,
 * and the monitors' health (0x58cb2).  This image's counts the framer's
 * address and data errors where the 58503A's prints its responses
 * buffer in hex. */
void gps_print_process_monitor(void) {
  /* The response buffer's first 40 characters, each NUL a comma, to a NUL
   * before an LF. */
  static const uint32_t SHOWN = 40;
  const char* text;
  const char* detail;
  char shown[41];
  char character[5];

  psos_pause(1);
  console_write("\r\n");
  switch (gps_task_state) {
    case GPS_PROCESS_TASK_IDLE:
      text = "________ State: Task Idle";
      break;
    case GPS_PROCESS_SEND_COMMAND:
      text = "_____ State: Send Command";
      break;
    case GPS_PROCESS_WAIT_FOR_RESPONSE:
      text = " State: Wait for Response";
      break;
    default:
      text = "________________ State: ?";
      break;
  }
  printf("GPS Process Monitor _________________%s\r\n", text);
  printf("\r\nProcess Messages   Net:   %+6d%14s",
         (uint32_t)gps_counts.jam_errors - gps_counts.jammed +
             gps_counts.received - gps_counts.sent,
         "");
  console_write("GT-74 Messages\r\n");
  console_write("--------------------------------");
  console_write("              ----------------\r\n");
  printf("%-9s  %5d   %6s  %5d%14s%-4s  %10d\r\n", "Msgs Rcvd",
         (uint32_t)gps_counts.received, "Errors",
         (uint32_t)gps_counts.received_errors, "", "Resp",
         gps_counts.responses_read);
  printf("%-9s  %5d   %6s  %5d%14s%-9s  %5d\r\n", "Msgs Sent",
         (uint32_t)gps_counts.sent, "Errors", (uint32_t)gps_counts.sent_errors,
         "", "Cmds Sent", (uint32_t)gps_counts.commands_sent);
  printf("%-9s  %5d   %6s  %5d%16s%-7s  %5d\r\n", "Msgs Jam",
         (uint32_t)gps_counts.jammed, "Errors", (uint32_t)gps_counts.jam_errors,
         "", "E Retry", (uint32_t)gps_counts.e_retries);
  printf("%48s%-7s  %5d\r\n", "", "T Retry", (uint32_t)gps_counts.t_retries);
  console_write(
      "Execution Status:                             Response Errors:\r\n");
  printf("%-9s  %5d   %-3s     %5d%14s%-9s  %5d\r\n", "Success",
         (uint32_t)gps_counts.successes, "NOP", (uint32_t)gps_counts.field_0e,
         "", "? Resp", (uint32_t)gps_counts.unknown_response);
  printf("%-9s  %5d   %-6s  %5d%14s%-9s  %5d\r\n", "Reject",
         (uint32_t)gps_counts.rejects, "? Cmd",
         (uint32_t)gps_counts.unknown_command, "", "Frame",
         (uint32_t)gps_counts.frame_errors);
  printf("%-9s  %5d   %-6s  %5d%14s%-9s  %5d\r\n", "Timeout",
         (uint32_t)gps_counts.timeouts, "? Sndr",
         (uint32_t)gps_counts.unknown_sender, "", "Cksum",
         (uint32_t)gps_counts.checksum_errors);
  printf("%-9s  %5d   %-6s  %5d%14s%-9s  %5d\r\n", "Busy",
         (uint32_t)gps_counts.busy, "No Cmd", (uint32_t)gps_counts.no_command,
         "", "Addr", (uint32_t)gps_counts.address_errors);
  printf("%-9s  %5d   %-6s  %5d%14s%-9s  %5d\r\n", "Abort",
         (uint32_t)gps_counts.aborts, "No Qry", (uint32_t)gps_counts.no_query,
         "", "Data", (uint32_t)gps_counts.data_errors);

  console_write("\r\nLast Valid pSOS Msg: ");
  if (gps_last_command == NULL) {
    console_write("(none received)\r\n");
  } else {
    gps_request_entry* request = &gps_last_command->request;

    if (request->argument_mode == GPS_ARGUMENTS_SET) {
      detail = "change/execute";
    } else if (request->argument_mode == GPS_ARGUMENTS_QUERY) {
      detail = "query setting";
    } else {
      detail = "? action";
    }
    printf("Sender: %-5s Cmd #: %-3d (%s)\r\n",
           gps_reports_sender_name(request->sender), (int32_t)request->code,
           detail);
  }
  console_write("Last Rcvr Prgm Msg:  ");
  if (gps_last_program_message[0] == 0) {
    console_write("(none sent)\r\n");
  } else {
    printf("%s\r\n", gps_last_program_message);
  }

  console_write("Response Buffer:  ");
  shown[0] = 0;
  for (char* cursor = gps_response_buffer; cursor < gps_response_buffer + SHOWN;
       cursor++) {
    if (*cursor == '\0') {
      if (cursor[1] == '\n') {
        break;
      }
      character[0] = ',';
    } else {
      character[0] = *cursor;
    }
    character[1] = '\0';
    strcat(shown, character);
  }
  printf("%s\r\n", shown);
  if (gps_commands_blocked != 0) {
    console_write("\r\n** GPS receiver commands are blocked **\r\n");
  }

  printf("\r\nGPS Receiver Health _____________________________________%s\r\n",
         gps.receiver_health_failed == 0 ? "__ OK" : " Fail");
  switch (gps.command_monitor_state) {
    case GPS_COMMAND_MONITOR_DISABLED:
      text = "disabled";
      break;
    case GPS_COMMAND_MONITOR_STANDBY:
      text = "monitoring";
      break;
    case GPS_COMMAND_MONITOR_QUERY:
      text = "querying";
      break;
    case GPS_COMMAND_MONITOR_QUERY_ACK:
      text = "wait for ack";
      break;
    default:
      text = "? state";
      break;
  }
  printf("Command Monitor   %-12s %-7s          %5d failures\r\n", text,
         gps_reports_failed(gps.command_monitor_failed),
         (uint32_t)gps_counts.command_monitor_failures);
  switch (gps.status_monitor_state) {
    case GPS_STATUS_MONITOR_DISABLED:
      text = "disabled";
      break;
    case GPS_STATUS_MONITOR_STANDBY:
      text = "monitoring";
      break;
    case GPS_STATUS_MONITOR_RECOVERY:
      text = "recovering";
      break;
    default:
      text = "? state";
      break;
  }
  printf("Status Monitor    %-12s %-7s          %5d failures\r\n", text,
         gps_reports_failed(gps.status_monitor_failed),
         (uint32_t)gps_counts.status_monitor_failures);
}

/* Empties the command log (0x587f8). */
void command_log_clear(void) {
  command_log.write = command_log.entries;
  command_log.read = command_log.entries;
}
