/* From the 58503B 1.01.04's rewritten/gps_reports.c, the Z3805A 1.01.03's
 * own beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list").  Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image.  Each keeps its generated prototype.  With them
 * gps_print_time_raim and gps_print_average, the Z3815A 4010's read
 * against this image's instructions and changed where they differ. */

/* Prints the command log. */
void gps_print_commands(void) { gps_print_command_log(&command_log); }

/* A channel's signal strength in two bits, 0 to 3, by the thresholds 43,
 * 47 and 51, the GT-74's, where the 58503A's are 30, 60 and 120 in line
 * (0x4f0ce). */
uint8_t gps_strength_bits(uint8_t strength) {
  static const uint8_t WEAK = 43;
  static const uint8_t FAIR = 47;
  static const uint8_t GOOD = 51;

  if (strength < WEAK) {
    return 0;
  }
  if (strength < FAIR) {
    return 1;
  }
  return strength < GOOD ? 2 : 3;
}

/* Prints @@Cj, the receiver's identification (0x4a640). */
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

/* Prints the satellites GSV reports visible (0x4a218). */
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

static const float GPS_REPORTS_HUNDREDTHS = 100.0f;

/* A signed count of units, divided, for a "%f". */
static double gps_reports_scaled(int32_t value, float divisor) {
  return float_to_double(float_divide(integer_to_float(value), divisor));
}

/* Prints @@Ce, the pseudorange corrections, and their average and
 * deviation (0x49c3c). */
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

/* The divisors that make the receiver's integer units printable: tenths
 * (DOPs) and hundredths (cm to m). */
static const float GPS_REPORTS_TENTHS = 10.0f;

/* Prints the best satellite combinations by xDOP (0x496e6).  Its count
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

/* An unsigned count of units, divided, for a "%f". */
static double gps_reports_scaled_unsigned(uint32_t value, float divisor) {
  return float_to_double(float_divide(unsigned_to_float(value), divisor));
}

/* Prints the extended navigation data: DOPs, velocity, the ECEF
 * position, each channel's URA and the least squares matrix's triangle
 * (0x497c2). */
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
 * installed, and the last alarm's time and date once there has been one
 * (0x499a0).  The Z3815A 4010's with the last alarm (0x49bea) and this
 * image's texts; it formats the sigma and prints a blank in its place
 * too. */
void gps_print_time_raim(void) {
  /* the pulse reference past oncore_pulse_sync's two */
  static const uint8_t PULSE_EPOCH = 2;
  /* alarm_limit's unit */
  static const int32_t ALARM_LIMIT_NS = 10;
  oncore_raim_fields* raim = &gps.raim;
  const char* text;
  const char* output;
  char limit[9];

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
      text = "Detection and Isolation";
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
  printf("\r\nDET/ISOL   %-24s   CONTROL   %s\r\n", text, output);
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
  const bool alarmed = gps.raim_alarm_date != 0;

  if (alarmed) {
    char time_text[9];
    char date_text[12];
    /* The date as the long the image keeps and passes. */
    union {
      uint32_t packed;
      calendar_date parts;
    } date = {.packed = gps.raim_alarm_date};

    format_time(gps.raim_alarm_time, time_text);
    format_date(date.parts, date_text);
    printf("Last alarm: %s, %s\r\n", time_text, date_text);
  }
}

/* Prints the survey's averaged position and height, the MSL one while
 * field_592's HEIGHT_MSL bit is set (0x4a184), else the GPS one, its
 * count while averaging, and how many channels' status has bit 7
 * (0x4a0fc).  The Z3815A 4010's. */
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
  double* height = (gps.field_592 & GPS_FIELD_592_HEIGHT_MSL) != 0
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

/* The entry after one in a command log's ring, the first once past the
 * last, where the cursors start (0x4e93e, 0x4e9c2). */
static gps_command_log_entry* gps_reports_log_next(
    gps_command_log* log, gps_command_log_entry* entry) {
  entry++;
  return entry >= (gps_command_log_entry*)&log->read ? log->entries : entry;
}

/* Empties the command log (0x4e886). */
void command_log_clear(void) {
  command_log.write = command_log.entries;
  command_log.read = command_log.entries;
}

/* Logs a sentence gpsm_task sends, with up to two characters of its
 * text after its descriptor's address, and gps_date_time_local's date
 * and time (0x4e930), where the Z3815A 4010's makes them itself; the
 * oldest entry goes once the log is full; 1 (0x4e896). */
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
  gps_date_time_local(&entry.date, &entry.time, &entry.from_clock);

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

/* The name of a GPS request's sender, as the log prints it, by its
 * gps_request_sender; "?" for none or another: gps_print_command's jump
 * table's (0x4ea02), this image's with "GPSq" for the other settings'.
 * A table, so that gcc's code holds no jump table, whose words objdump
 * would decode as instructions the CPU32 does not have. */
static const char* gps_reports_sender_name(gps_request_sender sender) {
  static const char* const NAMES[] = {
      [GPS_SENDER_SEQUENCE] = "PLL",
      [GPS_SENDER_SETTING] = "SCPI1",
      [GPS_SENDER_SETTING_OTHER] = "GPSq",
      [GPS_SENDER_COMMAND_MONITOR] = "GPSc",
      [GPS_SENDER_STATUS_MONITOR] = "GPSs",
  };
  static const uint8_t COUNT = sizeof NAMES / sizeof NAMES[0];

  return sender < COUNT && NAMES[sender] != NULL ? NAMES[sender] : "?";
}

/* Takes a command log's oldest entry and prints it into a line, its
 * translated code after the one received where they differ, and its
 * sentence's address or query; 0 when the log is empty, else 1
 * (0x4e986). */
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

/* Prints a command log, taking its entries (0x4eb26). */
void gps_print_command_log(gps_command_log* log) {
  /* the most a log holds, one entry short of its ring */
  static const uint8_t MOST_COMMANDS = 50;
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

/* Prints a status record: time, position, heights, DOP and the channels'
 * C/N (0x4a2b2). */
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
  char* format = (status->dop_type & ONCORE_DOP_HDOP) != 0 ? "HDOP%23.1f\r\n"
                                                           : "PDOP%23.1f\r\n";

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

/* The status' height, the MSL one while field_592 says so, else the GPS
 * one. */
static int32_t gps_reports_height(const oncore_status_fields* status) {
  return (gps.field_592 & GPS_FIELD_592_HEIGHT_MSL) != 0 ? status->height_msl
                                                         : status->height_gps;
}

/* Prints gps.status as three "gs: " lines, the first two through printf
 * where the Z3815A 4010's go through debug_printf (0x4a472), its height
 * as gps_reports_height picks it; the last channel's comma dropped. */
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
  printf("%s,%4.2f\r\n", line,
         gps_reports_scaled(gps_reports_height(&gps.status),
                            GPS_REPORTS_HUNDREDTHS));
  debug_sprintf(line, "gs: %d,%d,%02X,", (uint32_t)gps.status.visible_count,
                (uint32_t)gps.status.tracked_count,
                (uint32_t)gps.status.receiver_status);
  printf("%s%2.1f\r\n", line,
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

/* The Sat View page's sky: cells of SKY_AZIMUTH_STEP degrees of azimuth
 * and SKY_ELEVATION_STEP of elevation, two to a byte of
 * sky_statistics.cells, and the hours of its second half. */
static const uint16_t SKY_AZIMUTH_LAST = 359; /* degrees */
static const uint8_t SKY_ELEVATION_LAST = 89; /* degrees */
static const uint8_t SKY_AZIMUTH_STEP = 5;    /* degrees */
static const uint8_t SKY_ELEVATION_STEP = 5;  /* degrees */
static const uint8_t SKY_ELEVATION_BAND = 10; /* degrees, a byte's two */
static const uint8_t SKY_NIBBLE_BITS = 4;
static const uint8_t SKY_NIBBLE = 0x0f;
static const uint8_t SKY_HIGH_NIBBLE = 0xf0;
static const uint8_t SKY_AZIMUTHS = 72;
static const int8_t SKY_BANDS = 9;
static const uint8_t SKY_HOURS = 24;

/* Clears the Sat View page's statistics (0x4f386). */
void sky_statistics_clear(void) {
  memset(sky_statistics.cells, 0, sizeof sky_statistics.cells);
  memset(&sky_statistics.hours, 0, sizeof sky_statistics.hours);
  memset(sky_statistics.hour_average, 0, sizeof sky_statistics.hour_average);
  sky_statistics.hours_seen = 0;
  sky_statistics.samples = 0;
  sky_statistics.average = 0.0f;
}

/* A used satellite's cell: USED with no signal strength, unless the
 * cell has a level; else the strength scaled to a level, 1 below
 * WEAKEST and a level each LEVEL_STEP from LEVEL_BASE up to the last,
 * averaged with the cell's level (0x4f4ca to 0x4f520). */
static uint8_t sky_cell_used(int16_t level, uint8_t old) {
  static const int16_t WEAKEST = 40;
  static const int16_t LEVEL_BASE = 36;
  static const int16_t LEVEL_STEP = 2;
  const bool leveled =
      old >= SKY_CELL_LEVEL_FIRST && old <= SKY_CELL_LEVEL_LAST;

  if (level == -1) {
    return leveled ? SKY_CELL_NONE : SKY_CELL_USED;
  }
  uint8_t scaled = SKY_CELL_LEVEL_FIRST;

  if (level >= WEAKEST) {
    const int32_t steps = (level - LEVEL_BASE) / LEVEL_STEP;

    scaled = steps < SKY_CELL_LEVEL_LAST ? (uint8_t)steps : SKY_CELL_LEVEL_LAST;
  }
  return leveled ? (uint8_t)((old + scaled) >> 1) : scaled;
}

/* A cell of a satellite on a channel and not used, which leaves a
 * cell that has a level, USED or MIXED as it is: ATTEMPTED in an empty
 * cell with no Doppler, else the Doppler's sign, near zero within
 * NEAR_ZERO, and MIXED where the cell has another of the three
 * (0x4f524 to 0x4f5a2). */
static uint8_t sky_cell_attempted(int16_t doppler, uint8_t old) {
  static const int16_t NO_DOPPLER = -32768;
  static const int32_t NEAR_ZERO = 100;

  if ((old >= SKY_CELL_LEVEL_FIRST && old <= SKY_CELL_USED) ||
      old == SKY_CELL_DOPPLER_MIXED) {
    return SKY_CELL_NONE;
  }
  if (doppler == NO_DOPPLER) {
    return old == SKY_CELL_NONE ? SKY_CELL_ATTEMPTED : SKY_CELL_NONE;
  }
  const uint8_t sign = abs(doppler) <= NEAR_ZERO ? SKY_CELL_DOPPLER_NEAR_ZERO
                       : doppler > 0             ? SKY_CELL_DOPPLER_POSITIVE
                                                 : SKY_CELL_DOPPLER_NEGATIVE;

  return sign != old && old >= SKY_CELL_DOPPLER_POSITIVE &&
                 old <= SKY_CELL_DOPPLER_NEGATIVE
             ? SKY_CELL_DOPPLER_MIXED
             : sign;
}

/* Gathers the Sat View page's statistics from the satellites used or
 * on a channel: each with a position into its cell of the sky, then
 * the local hour's fewest and most used and, for SAMPLES_MOST updates
 * of an hour, the running mean of those on a channel and not used,
 * rounded into the hour's average (0x4f3d2).  The image also gathers
 * each list's Dopplers in bins of 500 into two words of its frame,
 * which nothing reads (0x4f5fc to 0x4f65c). */
void sky_statistics_update(void) {
  /* satellite_status_list's list of those used or on a channel. */
  static const uint8_t ON_CHANNEL = 2;
  static const uint16_t SAMPLES_MOST = 7200;
  static const double ROUND = 0.5;
  satellite_sets sets;
  satellite_list_entry list[12];
  uint8_t count;
  uint8_t used = 0;
  uint32_t date;
  time_of_day time;
  uint8_t from_clock;

  satellite_sets_build(gps.status.channels, &sets);
  satellite_status_list(
      ON_CHANNEL, gps.channel_count, &sets, gps.status.channels,
      (gps_visibility_record*)&gps.visibility_received, list, &count);
  for (uint8_t index = 0; index < count; index++) {
    const satellite_list_entry* entry = &list[index];

    if (entry->attempting == 0) {
      used++;
    }
    if (entry->cell.cell.status != SATELLITE_CELL_POSITION) {
      continue;
    }
    const uint16_t azimuth = entry->cell.cell.azimuth < SKY_AZIMUTH_LAST
                                 ? entry->cell.cell.azimuth
                                 : SKY_AZIMUTH_LAST;
    const uint8_t elevation = entry->cell.cell.elevation < SKY_ELEVATION_LAST
                                  ? entry->cell.cell.elevation
                                  : SKY_ELEVATION_LAST;
    uint8_t* cell = &sky_statistics.cells[(uint8_t)(azimuth / SKY_AZIMUTH_STEP)]
                                         [elevation / SKY_ELEVATION_BAND];
    const bool upper = (elevation / SKY_ELEVATION_STEP) % 2 != 0;
    const uint8_t old = (upper ? *cell >> SKY_NIBBLE_BITS : *cell) & SKY_NIBBLE;
    const uint8_t code = entry->attempting == 0
                             ? sky_cell_used(entry->level, old)
                             : sky_cell_attempted(entry->cell.doppler, old);

    if (code == SKY_CELL_NONE) {
      continue;
    }
    if (upper) {
      *cell = (uint8_t)((*cell & SKY_NIBBLE) | (code << SKY_NIBBLE_BITS));
    } else {
      *cell = (*cell & SKY_HIGH_NIBBLE) | code;
    }
  }
  gps_date_time_local(&date, &time, &from_clock);
  const uint8_t hour = time.hour;

  if (hour >= SKY_HOURS) {
    return;
  }
  uint8_t* range = &sky_statistics.hours.range[hour];

  if (hour != sky_statistics.hours.current) {
    sky_statistics.samples = 0;
    *range = SKY_HIGH_NIBBLE;
    sky_statistics.hours.current = hour;
    sky_statistics.hour_average[hour] = 0;
    sky_statistics.average = 0.0f;
  }
  if (used < *range >> SKY_NIBBLE_BITS) {
    *range = (uint8_t)((*range & SKY_NIBBLE) | (used << SKY_NIBBLE_BITS));
  } else if ((*range & SKY_NIBBLE) < used) {
    *range = (*range & SKY_HIGH_NIBBLE) | used;
  }
  sky_statistics.hours_seen |= 1U << hour;
  if (sky_statistics.samples > SAMPLES_MOST) {
    return;
  }
  sky_statistics.average =
      ((float)sky_statistics.samples * sky_statistics.average +
       (float)(int32_t)(count - used)) /
      (float)(int32_t)(sky_statistics.samples + 1);
  sky_statistics.hour_average[hour] =
      (uint8_t)(uint32_t)(ROUND + (double)sky_statistics.average);
  sky_statistics.samples++;
}

/* Prints the Sat View page: the sky's cells, a line for each
 * SKY_ELEVATION_STEP degrees from the top, its edge a ':' below the
 * mask angle; then for each count of satellites from the engine's
 * channels, six at least, down to none, the hours whose range of
 * satellites used takes it in, '|', or ':' where it is the hour's
 * average, which alone is '.', each line cut after its last mark
 * above three; and the current hour marked '-' under them (0x4a6f4). */
void gps_print_sat_view(void) {
  /* A cell's character by its sky_cell_code (0x4f96e). */
  static const char SYMBOLS[] = " 123456789*.^-v+";
  static const uint8_t COUNTS_LEAST = 6;
  static const int8_t CUT_ABOVE = 3;
  char lower[SKY_AZIMUTHS + 1];
  char upper[SKY_AZIMUTHS + 1];
  char line[SKY_HOURS + 2];

  psos_pause(1);
  console_write(
      "\r\n\r\nS a t   V i e w  ___________________________________"
      "__________________ /-o-/\r\n\r\n");
  printf("   Scaled %s vs. Satellite Position\r\n", "C/N");
  console_write(
      "El n=====:=====:=====e=====:=====:=====s=====:=====:=====w="
      "====:=====:=====\r\n");
  for (int8_t band = SKY_BANDS - 1; band >= 0; band--) {
    for (uint8_t azimuth = 0; azimuth < SKY_AZIMUTHS; azimuth++) {
      const uint8_t cell = sky_statistics.cells[azimuth][band];

      upper[azimuth] = SYMBOLS[cell >> SKY_NIBBLE_BITS];
      lower[azimuth] = SYMBOLS[cell & SKY_NIBBLE];
    }
    upper[SKY_AZIMUTHS] = '\0';
    lower[SKY_AZIMUTHS] = '\0';
    const uint8_t elevation = (uint8_t)(band * SKY_ELEVATION_BAND);

    printf("%02d%c%s|\r\n", elevation + SKY_ELEVATION_STEP,
           elevation + SKY_ELEVATION_STEP < gps.mask_angle ? ':' : '|', upper);
    printf("%02d%c%s|\r\n", (uint32_t)elevation,
           elevation < gps.mask_angle ? ':' : '|', lower);
  }
  console_write(
      "   n=====:=====:=====e=====:=====:=====s=====:=====:=====w="
      "====:=====:=====\r\n");
  console_write(
      "Az 0    030   060   090   120   150   180   210   240   270"
      "   300   330\r\n");
  console_write("\r\n\r\n   Tracked Satellites/Hour\r\n");
  console_write("   ========================\r\n");
  for (int8_t satellites =
           (int8_t)(gps.channel_count > COUNTS_LEAST ? gps.channel_count
                                                     : COUNTS_LEAST);
       satellites >= 0; satellites--) {
    uint8_t last = 0;

    for (uint8_t hour = 0; hour < SKY_HOURS; hour++) {
      const bool seen = (sky_statistics.hours_seen & (1U << hour)) != 0;
      const uint8_t range = sky_statistics.hours.range[hour];
      const bool average = satellites == sky_statistics.hour_average[hour];

      if (seen && (range & SKY_NIBBLE) >= satellites &&
          satellites >= range >> SKY_NIBBLE_BITS) {
        line[hour] = average ? ':' : '|';
        last = hour + 1;
      } else if (seen && average) {
        line[hour] = '.';
        last = hour + 1;
      } else {
        line[hour] = ' ';
      }
    }
    line[satellites > CUT_ABOVE ? last : SKY_HOURS] = '\0';
    printf("%2d %s", (int32_t)satellites, line);
    console_write("\r\n");
  }
  const char* rule = "========================";
  char* out = line;

  do {
    *out++ = *rule;
  } while (*rule++ != '\0');
  line[sky_statistics.hours.current] = '-';
  printf("   %s\r\n", line);
  console_write("Hr 0 0 0 0 0 1 1 1 1 1 2 2\r\n");
  console_write("   0 2 4 6 8 0 2 4 6 8 0 2\r\n");
}

/* "OK" or "Fail" for a monitor's failed flag. */
static const char* gps_reports_failed(uint8_t failed) {
  return failed == 0 ? "OK" : "Fail";
}

/* Prints the GPS Process Monitor: the GPS task's state and counters,
 * its last request and program sentence, the response buffer's text,
 * and the monitors' health (0x4ebc8; its jump tables at 0x4ec22, 0x4ee82,
 * 0x4eed4, 0x4eff6 and 0x4f06a). */
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
    case GPS_STATUS_MONITOR_RECOVERED:
      text = "wait-recover";
      break;
    default:
      text = "? state";
      break;
  }
  printf("Status Monitor    %-12s %-7s          %5d failures\r\n", text,
         gps_reports_failed(gps.status_monitor_timed_out),
         (uint32_t)gps_counts.status_monitor_failures);
}

/* The ignored PRNs' list of the Instrument State Settings: four
 * columns of 31 characters and a NUL. */
#define GPS_REPORTS_IGNORED_COLUMNS 4

#define GPS_REPORTS_IGNORED_COLUMN_SIZE 32

/* Prints the Instrument State Settings: the receiver's time and date,
 * position hold, position, ignored satellites and elevation mask, time
 * scale, leap second and its date, 1PPS offset, GPS-UTC offset and cable
 * delay (0x49dea; its jump table at 0x4a010).  The position is the
 * status', and the leap second's adjustment "+1" or "-1" while one is
 * pending, else the sign of the one tps announces with "0", " ?" for a
 * pending value past 2.  The leap fields are gps's own, which the tps
 * commit fills. */
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

  console_write("\r\nSatellite Tracking ________________________   ");
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
      adjustment = gps.leap_second_announced == ONCORE_LEAP_SECOND_ADD ? "+0"
                   : gps.leap_second_announced == ONCORE_LEAP_SECOND_SUBTRACT
                       ? "-0"
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
  if (gps.leap_year == 0) {
    console_write("\r\n");
  } else {
    printf(", %02d:%02d:%02d %02d/%02d/%04d\r\n", (uint32_t)gps.leap_time.hour,
           (uint32_t)gps.leap_time.minute, (uint32_t)gps.leap_time.second,
           (uint32_t)gps.leap_month, (uint32_t)gps.leap_day,
           (uint32_t)gps.leap_year);
  }
  printf("1PPS OFFS  %9d ns%23sUTC OFFS %+d s\r\n", gps.pps_offset, "",
         (int32_t)(int8_t)gps.utc_offset);
  printf("CABLE DLY     %6d ns\r\n", gps.pps_cable_delay_given);
}
