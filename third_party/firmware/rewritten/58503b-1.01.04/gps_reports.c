/* The 58503B 1.01.04's command_log_add, the Z3815A 4010's in its shape,
 * written by hand in place of the decompiler's, which copied the entry
 * into the log a field at a time where the image copies five longs
 * (docs/firmware/58503b-1.01.04.md, "The command log";
 * docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

/* The log's entry after one, back to its first past its last. */
static gps_command_log_entry* gps_reports_log_next(
    gps_command_log* log, gps_command_log_entry* entry) {
  static const uint32_t ENTRIES = sizeof log->entries / sizeof log->entries[0];

  entry++;
  return entry >= log->entries + ENTRIES ? log->entries : entry;
}

/* Logs a command gpsm_task sends, with the sent text's fifth and sixth
 * characters, and the local date and time gps_date_time_local gives;
 * the oldest entry goes once the log is full; 1 (0x53732). */
uint32_t command_log_add(int32_t command_address, int32_t text_address) {
  /* The sent text's characters the entry keeps. */
  static const uint8_t ARGUMENT_FIRST = 4;
  const gps_running_command* command =
      (const gps_running_command*)command_address;
  const char* text = (const char*)text_address;
  gps_command_log_entry entry;

  entry.sender = command->request.sender;
  entry.code = command->request.code;
  entry.message = command->message;
  entry.argument_text[0] = text[ARGUMENT_FIRST];
  entry.argument_text[1] = text[ARGUMENT_FIRST + 1];
  entry.argument_text[2] = '\0';
  entry.query = command->query;
  gps_date_time_local(&entry.date, &entry.time, &entry.from_clock);

  gps_command_log_entry* next =
      gps_reports_log_next(&command_log, command_log.write);

  if (next == command_log.read) {
    command_log.read = gps_reports_log_next(&command_log, command_log.read);
  }
  /* The whole entry, field_01 and field_0b never set as the image's frame
   * leaves them. */
  *command_log.write = entry;
  command_log.write = next;
  return 1;
}

/* The divisors that make an Oncore's integer units printable: tenths
 * (DOPs), hundredths (cm to m) and thousandths (mm/s to m/s, ms to s). */
static const float GPS_REPORTS_TENTHS = 10.0f;

static const float GPS_REPORTS_HUNDREDTHS = 100.0f;

/* A signed count of units, divided, for a "%f". */
static double gps_reports_scaled(int32_t value, float divisor) {
  return float_to_double(float_divide(integer_to_float(value), divisor));
}

/* An unsigned count of units, divided, for a "%f". */
static double gps_reports_scaled_unsigned(uint32_t value, float divisor) {
  return float_to_double(float_divide(unsigned_to_float(value), divisor));
}

/* Prints @@Cj, the receiver's identification (0x4f624). */
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

/* Prints @@Bb, the satellites predicted visible (0x4f1ce). */
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

/* Prints @@Bc, the best satellite combinations by xDOP (0x4e0ce).  Its
 * count is taken against a signed byte, as the image's loop does. */
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

/* Prints @@Bk, the extended navigation data: DOPs, velocity, the ECEF
 * position, each channel's URA and the least squares matrix's triangle
 * (0x4e1aa). */
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

/* Prints @@Ce, the pseudorange corrections, and their average and
 * deviation (0x4e750). */
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
  /* The image reads past @@Ce's six channels with eight
   * (docs/firmware/quirks.md). */
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

/* Empties the command log. */
void command_log_clear(void) {
  command_log.write = command_log.entries;
  command_log.read = command_log.entries;
}

/* Prints the command log. */
void gps_print_commands(void) { gps_print_command_log(&command_log); }

/* Prints @@Bn, the Time RAIM status, and each channel's time estimate,
 * starred when the channel is used in the time solution, beside the
 * channels raim_monitor kept at the last alarm, if any, with their date,
 * time and sigma estimate; or that Time RAIM is not installed
 * (0x4e388).  The 58503A 3704's lists only the channels with a
 * satellite, starred when used for position, and keeps no alarm; its
 * headings are narrower and some of its labels differ. */
void gps_print_time_raim(void) {
  /* @@Bn's pulse reference past oncore_pulse_sync's two */
  static const uint8_t PULSE_EPOCH = 2;
  /* alarm_limit's unit */
  static const int32_t ALARM_LIMIT_NS = 10;
  oncore_raim_fields* raim = &gps.raim;
  const char* text;
  const char* output;
  char limit[10];
  char time[9];
  char date[13];

  psos_pause(1);
  console_write("\r\n\r\n");
  if ((gps.field_608 & GPS_FIELD_608_TIME_RAIM) == 0) {
    console_write("Time RAIM capability is not installed.\r\n");
    return;
  }
  printf(
      "------------------------ Time RAIM Status ------------------------\r\n");
  printf("MODE       %-16s", raim->raim_on == 1 ? "On" : "Off");
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
  printf("RAIM STAT  %-24s   STATUS    %s\r\n", text,
         raim->pulse_status != 0 ? "On" : "Off");
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
  /* An unknown reference leaves the isolation's text: the image's
   * default sets another register. */
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
  printf("SIGMA EST  %-24s   SAWT ERR  %+d ns\r\n", limit,
         (int32_t)raim->sawtooth);
  console_write(
      "\r\nFractional Time "
      "__________________________________________________\r\n\r\n");
  const bool alarmed = gps.raim_alarm_date != 0;

  if (alarmed) {
    format_time(gps.raim_alarm_time, time);
    format_date(clock_date_of(gps.raim_alarm_date), date);
    console_write("      ___ Current ___    ____ Last Alarm ____");
    printf(" %s %s\r\n", time, date);
    printf(
        "Chn   PRN   Time (ns)    PRN   Time (ns)  Acq   Sigma Est %d ns\r\n",
        (uint32_t)gps.raim_alarm_sigma);
    console_write("---------------------------------------------\r\n");
  } else {
    console_write("\r\nChn   PRN   Time (ns)\r\n");
    console_write("---------------------\r\n");
  }
  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    const oncore_raim_channel* now = &raim->channels[channel];
    const oncore_raim_channel* last = &gps.raim_alarm_channels[channel];

    printf(" %d     %2d%s  %9d", channel + 1, (uint32_t)now->satellite,
           now->mode == ONCORE_AVAILABLE_FOR_POSITION ? "*" : " ",
           now->time_estimate);
    if (alarmed) {
      printf("     %2d%s  %9d    %d\r\n", (uint32_t)last->satellite,
             last->mode == ONCORE_AVAILABLE_FOR_POSITION ? "*" : " ",
             last->time_estimate, (int32_t)(int8_t)last->mode);
    } else {
      console_write("\r\n");
    }
  }
  console_write("\r\n* used in time solution\r\n");
}

/* The name of a GPS request's sender, as the logs print it, by its
 * gps_request_sender; "?" for none or another (0x5384c's table).  A
 * table, so that gcc's code holds no jump table, whose words objdump
 * would decode as instructions the CPU32 does not have. */
static const char* gps_reports_sender_name(gps_request_sender sender) {
  static const char* const NAMES[] = {
      [GPS_SENDER_SEQUENCE] = "PLL",
      [GPS_SENDER_SETTING] = "SCPI1",
      [GPS_SENDER_SETTING_OTHER] = "SCPI2",
      [GPS_SENDER_POLL_MONITOR] = "GPSq",
      [GPS_SENDER_COMMAND_MONITOR] = "GPSc",
      [GPS_SENDER_STATUS_MONITOR] = "GPSs",
  };
  static const uint8_t COUNT = sizeof NAMES / sizeof NAMES[0];

  return sender < COUNT && NAMES[sender] != NULL ? NAMES[sender] : "?";
}

/* The engine's family, as the pages head it, by the signal its
 * channels report: VP for SS, UT for C/N, else ?? (0x5396e to
 * 0x53992). */
static const char* gps_reports_engine(void) {
  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
    return "VP";
  }
  return (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0 ? "UT" : "??";
}

/* Takes a command log's oldest entry and prints it into a line: its
 * sender, code, message and the message's ID, "**" for none, its
 * argument text, query or command, date and time; 0 when the log is
 * empty, else 1 (0x537d6).  The Z3815A 4010's prints the code received
 * and the sentence's address. */
int32_t gps_print_command(gps_command_log* log, char* line) {
  /* The message a command sent as none of oncore_messages' carries. */
  static const int16_t NO_MESSAGE = 63;

  if (log->read == log->write) {
    return 0;
  }
  gps_command_log_entry entry = *log->read;
  char date[12];
  char time[10];

  log->read = gps_reports_log_next(log, log->read);
  format_date(clock_date_of(entry.date), date);
  format_time(entry.time, time);
  sprintf(line, "%-5s %3d %3d  %-2.2s %-2s [%s]   %11s  %8s %s",
          gps_reports_sender_name(entry.sender), (int32_t)entry.code,
          (int32_t)entry.message,
          entry.message == NO_MESSAGE
              ? "**"
              : oncore_message_entry_at(entry.message)->id,
          entry.argument_text, entry.query != GPS_ARGUMENTS_SET ? "q" : "c",
          date, time, entry.from_clock != 0 ? " " : "e");
  return 1;
}

/* Prints a command log, taking its entries, headed by the engine's
 * family (0x53946); the Z3815A 4010's heads it "GT-74". */
void gps_print_command_log(gps_command_log* log) {
  /* the most a log holds, one entry short of its ring */
  static const uint8_t MOST_COMMANDS = 50;
  char line[50];
  uint8_t printed;

  psos_pause(1);
  console_write("\r\n");
  printf("%s Oncore Command Log __________________", gps_reports_engine());
  printf(" [last %2d]\r\n", MOST_COMMANDS);
  console_write("Task   ID CID  Command     Date and Time Issued\r\n");
  console_write("--------------------------------------------------\r\n");
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

/* A channel's signal strength in two bits, 0 to 3: for an engine giving
 * C/N (field_608's bit 2) by the Z3815A 4010's thresholds, 43, 47 and
 * 51, else by the 58503A 3704's, 30, 60 and 120, at most (0x53f48). */
uint8_t gps_strength_bits(uint8_t strength) {
  static const uint8_t CN_WEAK = 43;
  static const uint8_t CN_FAIR = 47;
  static const uint8_t CN_GOOD = 51;
  static const uint8_t SS_WEAK = 30;
  static const uint8_t SS_FAIR = 60;
  static const uint8_t SS_GOOD = 120;

  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0) {
    if (strength < CN_WEAK) {
      return 0;
    }
    if (strength < CN_FAIR) {
      return 1;
    }
    return strength < CN_GOOD ? 2 : 3;
  }
  if (strength <= SS_WEAK) {
    return 0;
  }
  if (strength <= SS_FAIR) {
    return 1;
  }
  return strength <= SS_GOOD ? 2 : 3;
}

/* The ignored PRNs' list of the Instrument State Settings: four
 * columns of 31 characters and a NUL. */
#define GPS_REPORTS_IGNORED_COLUMNS 4

#define GPS_REPORTS_IGNORED_COLUMN_SIZE 32

/* Prints the Instrument State Settings: the receiver's time, date,
 * settings, hold and initial positions, ignored satellites, DOP,
 * correction settings and the leap second @@Gj announces (0x4e8fe).
 * The 58503A 3704's, but that the settings a VP engine (SS) alone has,
 * the application, fix algorithm, satellite selection and DOP lines,
 * the position fix and differential output thresholds and the DCXO,
 * print only for one; the ignored PRNs only while field_608's bit 6 is
 * set; the UTC offset only for a UT engine (C/N), and its fraction and
 * the leap second's offsets while field_609's bit 6 is set too; its fix
 * algorithm marks the AFC track and the
 * position filter off, and it names the time scale and the atmospheric
 * correction where the 58503A's prints the UTC and ionospheric
 * corrections enabled. */
void gps_print_instrument_state(void) {
  /* The ignored PRNs' list, built as one text across four columns of
   * 31 characters and split by a NUL at each column's end, as the
   * image's adjacent buffers are; the columns go on the lines after. */
  static const uint8_t LAST_GPS = 32;
  /* @@Ar's fix algorithm in its low six bits, and the two option bits
   * above, each set for an option off. */
  static const uint8_t FIX_ALGORITHM_MASK = 0x3f;
  static const uint8_t FIX_AFC_TRACK_OFF = 0x40;
  static const uint8_t FIX_POSITION_FILTER_OFF = 0x80;
  /* @@Aq's atmospheric corrections: ionospheric, tropospheric, both. */
  static const uint8_t ATMOSPHERE_NONE = 0;
  static const uint8_t ATMOSPHERE_IONOSPHERIC = 1;
  static const uint8_t ATMOSPHERE_TROPOSPHERIC = 2;
  static const uint8_t ATMOSPHERE_BOTH = 3;
  char ignored[GPS_REPORTS_IGNORED_COLUMNS * GPS_REPORTS_IGNORED_COLUMN_SIZE];
  char other[16];
  const char* name;
  const char* operation;
  char sign;
  int8_t minutes;
  const bool ss = (gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0;
  const bool cn = (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0;
  const bool ignoring = (gps.field_608 & GPS_FIELD_608_BIT_6) != 0;
  const bool field_609_bit_6 = (gps.field_609 & GPS_FIELD_609_BIT_6) != 0;

  psos_pause(1);
  console_write(
      "\r\n------------------------- Instrument State Settings "
      "--------------------------\r\n");
  minutes = gps.gmt_offset.minutes;
  if (gps.gmt_offset.minutes < 0) {
    sign = '-';
    minutes = -gps.gmt_offset.minutes;
  } else {
    sign = gps.gmt_offset.sign == 0 ? '+' : '-';
  }
  printf("TIME   %02d:%02d:%02d    GMT CORR  %c%02dh:%02dm%24s",
         (uint32_t)gps.time.hour, (uint32_t)gps.time.minute,
         (uint32_t)gps.time.second, sign, (uint32_t)gps.gmt_offset.hours,
         (int32_t)minutes, "");
  printf("DATE   %02d/%02d/%04d\r\n", (uint32_t)gps.date.month,
         (uint32_t)gps.date.day, (uint32_t)gps.date.year);

  enabled_string(gps.position_hold, ignored);
  printf("\r\nPOS HOLD   %-8s", ignored);
  if (ss) {
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
    printf("%42sAPPL   %s\r\n", "", name);
  } else {
    console_write("\r\n");
  }
  if (ss) {
    const uint8_t options = gps.fix_algorithm & ~FIX_ALGORITHM_MASK;

    switch (gps.fix_algorithm & FIX_ALGORITHM_MASK) {
      case ONCORE_FIX_BEST_FOUR:
        name = "Best 4 satellites";
        break;
      case ONCORE_FIX_ALL_IN_VIEW:
        libc_copy(ignored, "N-in-view");
        if ((options & FIX_AFC_TRACK_OFF) != 0) {
          strcat(ignored, " / AFC Track Off");
        }
        if ((options & FIX_POSITION_FILTER_OFF) != 0) {
          strcat(ignored, " / Pos Filter Off");
        }
        name = ignored;
        break;
      default:
        name = "?";
        break;
    }
    switch (gps.fix_mode) {
      case ONCORE_IDLE:
        operation = "Idle";
        break;
      case ONCORE_POSITION_FIX:
        operation = "Pos Fix";
        break;
      default:
        operation = "?";
        break;
    }
    printf("FIX ALG    %-50sOPER   %s\r\n", name, operation);
  }

  console_write("\r\nHold Position _____________________________   ");
  console_write("Initial Position _______________\r\n");
  format_angle(ignored, gps.hold_position.latitude, 1);
  format_angle(other, gps.position.latitude, 1);
  printf("LAT        %s  = %+10d ms   %s  = %+10d ms\r\n", ignored,
         gps.hold_position.latitude, other, gps.position.latitude);
  format_angle(ignored, gps.hold_position.longitude, 0);
  format_angle(other, gps.position.longitude, 0);
  printf("LON        %s  = %+10d ms   %s  = %+10d ms\r\n", ignored,
         gps.hold_position.longitude, other, gps.position.longitude);
  /* the held height in the column of its type, the ellipsoid's or mean
   * sea level's */
  if (gps.hold_height_type == ONCORE_HEIGHT_GPS) {
    sprintf(ignored, "%+8d cm", gps.hold_position.height);
    other[0] = 0;
  } else {
    ignored[0] = 0;
    sprintf(other, "%+8d cm", gps.hold_position.height);
  }
  printf("GPS HGT           %11s%24s%+8d cm\r\n", ignored, "",
         gps.position.height_gps);
  printf("MSL HGT           %11s%24s%+8d cm\r\n", other, "",
         gps.position.height_msl);

  console_write("\r\nSatellite Tracking ________________________   ");
  for (uint8_t column = GPS_REPORTS_IGNORED_COLUMNS; column-- != 0;) {
    ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE] = 0;
  }
  if (ignoring) {
    console_write("PRNs Ignored ___________________\r\n");
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
    for (uint8_t column = GPS_REPORTS_IGNORED_COLUMNS - 1; column != 0;
         column--) {
      ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE - 1] = 0;
    }
  } else {
    console_write("\r\n");
  }
  printf("ELEV MASK  %-2d%33s%s\r\n", (uint32_t)gps.mask_angle, "", ignored);
  if (ss) {
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
  }
  if (gps.ionospheric_correction == ATMOSPHERE_NONE) {
    name = "Disabled";
  } else if (gps.ionospheric_correction == ATMOSPHERE_IONOSPHERIC) {
    name = "Ionospheric";
  } else if (gps.ionospheric_correction == ATMOSPHERE_TROPOSPHERIC) {
    name = "Tropospheric";
  } else if (gps.ionospheric_correction == ATMOSPHERE_BOTH) {
    name = "Ion and Trop";
  } else {
    name = "?";
  }
  printf("ATM CORR   %-12s", name);
  if (ss) {
    printf("   DOP 3D/2D    %4.1f   %s\r\n",
           gps_reports_scaled_unsigned(gps.xdop_threshold, GPS_REPORTS_TENTHS),
           &ignored[3 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  } else if (ignoring) {
    printf("%23s%s\r\n", " ", &ignored[1 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
    printf("%46s%s\r\n", " ", &ignored[2 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
    printf("%46s%s\r\n", " ", &ignored[3 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  } else {
    console_write("\r\n");
  }

  printf(
      "\r\nCorrection Factors %s_________   General Status "
      "_________________\r\n",
      ss ? "and Thresholds " : "_______________");
  switch (gps.leap_second_pending) {
    case ONCORE_NO_LEAP_SECOND:
      name = " 0";
      break;
    case ONCORE_LEAP_SECOND_ADD:
      name = "+1";
      break;
    case ONCORE_LEAP_SECOND_SUBTRACT:
      name = "-1";
      break;
    default:
      name = "?";
      break;
  }
  printf("TIME SCAL  %-15s%20sLEAP ADJ %s",
         gps.utc_time == ONCORE_TIME_UTC ? "UTC" : "GPS Time", "", name);
  if (gps.field_5c6 != 0) {
    printf(", %02d:%02d:%02d %02d/%02d/%04d\r\n", (uint32_t)gps.field_5d1,
           (uint32_t)gps.field_5d2, (uint32_t)gps.field_5d3,
           (uint32_t)gps.field_5c8, (uint32_t)gps.field_5c9,
           (uint32_t)gps.field_5c6);
  } else {
    console_write("\r\n");
  }
  printf("1PPS OFFS  %9d ns   ", gps.pps_offset);
  if (ss) {
    printf("POS FIX THR %3d s   ", (uint32_t)gps.position_fix_threshold);
  } else {
    console_write("                    ");
  }
  if (cn) {
    printf("UTC OFFS %+d s", (int32_t)(int8_t)gps.field_5c3);
  }
  if (cn && field_609_bit_6 && (gps.field_5ca != 0 || gps.field_5cc != 0)) {
    printf("  [%d.%09d s]\r\n", (int32_t)(int8_t)gps.field_5ca, gps.field_5cc);
  } else {
    console_write("\r\n");
  }
  printf("CABLE DLY     %6d ns   ", gps.pps_cable_delay_given);
  if (ss) {
    printf("DIF OUT THR %3d s   ", (uint32_t)gps.differential_output_threshold);
  } else {
    console_write("                    ");
  }
  if (cn && field_609_bit_6 && gps.field_5c4 != 0) {
    printf("Tls/Tlsf %+d s/%+d s\r\n", (int32_t)(int8_t)gps.field_5c4,
           (int32_t)(int8_t)gps.field_5c5);
  } else {
    console_write("\r\n");
  }
  if (ss) {
    printf("DCXO FREQ  Offs %d  Warp %d  Step %d\r\n", (uint32_t)gps.field_566,
           (uint32_t)gps.field_568, (uint32_t)gps.field_56a);
  } else {
    console_write("\r\n");
  }
}

/* Prints the survey's averaged position and height, the MSL one while
 * field_608's HEIGHT_MSL bit is set, else the GPS one, its count while
 * averaging, and how many channels' status has bit 7 (0x4f0b2): the
 * Z3815A 4010's on this image's flags. */
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
  double* height = (gps.field_608 & GPS_FIELD_608_HEIGHT_MSL) != 0
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

/* Prints an @@Ba record: time, position, heights, DOP and the channels,
 * their signal headed SS or C/N as the engine gives it (0x4f276).  The
 * 58503A 3704's, but that the DOP is HDOP while the byte after the
 * record's dop_type has bit 0 set, else PDOP (0x4f37a), where the
 * 58503A's reads dop_type. */
void gps_print_status(oncore_status_fields* status) {
  /* field_23's bit for an HDOP */
  static const uint8_t STATUS_HDOP = 0x01;
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
  char* format = (status->field_23 & STATUS_HDOP) != 0 ? "HDOP%23.1f\r\n"
                                                       : "PDOP%23.1f\r\n";

  printf(format, gps_reports_scaled_unsigned(status->dop, GPS_REPORTS_TENTHS));
  console_write("\r\n");
  printf("CHN      SVID      MODE      %3s      STATUS\r\n",
         (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0 ? "C/N" : "SS");
  for (int32_t channel = 0; channel < gps.channel_count; channel++) {
    oncore_channel_status* tracked = &status->channels[channel];

    /* The mode sign-extended, as the image passes it. */
    printf("%2d%10d%10d%10d        %02X\r\n", channel + 1,
           (uint32_t)tracked->satellite, (int32_t)(int8_t)tracked->mode,
           (uint32_t)tracked->signal_strength, (uint32_t)tracked->status);
  }
}

/* A status record's height, the MSL one while field_608's HEIGHT_MSL
 * bit is set, else the GPS one. */
static int32_t gps_reports_height(const oncore_status_fields* status) {
  return (gps.field_608 & GPS_FIELD_608_HEIGHT_MSL) != 0 ? status->height_msl
                                                         : status->height_gps;
}

/* Prints gps.status as three "gs: " lines, for the console word
 * abr_stat (0x4f456), its height as gps_reports_height picks it; the
 * last channel's comma dropped.  The Z3815A 4010's, but that the first
 * two lines go through printf, not debug_printf. */
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

/* "OK" or "Fail" for a monitor's failed flag. */
static const char* gps_reports_failed(uint8_t failed) {
  return failed == 0 ? "OK" : "Fail";
}

/* Prints the GPS Process Monitor: the GPS task's state and counters,
 * its last request and program message, the response buffer in hex,
 * and the monitors' health (0x53a18).  The 58503A 3704's, but that its
 * labels for the last request and program message are a column
 * narrower, the code wider, the monitors' states named otherwise and
 * the status monitor's fourth, "wait-recover", and the status monitor's
 * failure is gps.status_monitor_timed_out. */
void gps_print_process_monitor(void) {
  /* Each byte as two hex digits; the loop that would strip a third
   * digit's leading 0 or f never finds one.  The buffer's first 20
   * bytes, two digits each. */
  static const uint32_t SHOWN = 20;
  char bytes[2 * 20 + 1];
  char hex[5];
  const char* text;
  const char* detail;

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
  console_write("ONCORE Messages\r\n");
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
  printf("%-9s  %5d   %-6s  %5d\r\n", "Busy", (uint32_t)gps_counts.busy,
         "No Cmd", (uint32_t)gps_counts.no_command);
  printf("%-9s  %5d   %-6s  %5d\r\n", "Abort", (uint32_t)gps_counts.aborts,
         "No Qry", (uint32_t)gps_counts.no_query);

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
  bytes[0] = 0;
  for (uint8_t* cursor = gps_response_buffer;
       cursor < gps_response_buffer + SHOWN; cursor++) {
    char* digits = hex;

    sprintf(hex, "%hx", (uint16_t)*cursor);
    if (libc_length(digits) < 2) {
      hex[2] = 0;
      hex[1] = hex[0];
      hex[0] = '0';
    } else {
      while (libc_length(digits) >= 3 && (*digits == '0' || *digits == 'f')) {
        digits++;
      }
    }
    strcat(bytes, digits);
  }
  printf("%s\r\n", bytes);
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

/* Prints the Sat View page, from sky_statistics, for the console's
 * pr_satview (gps_console_request, 0x5367a): the map of the satellites' places,
 * a row for each 5 degrees of elevation from 85 down, its edge ':' below the
 * elevation mask, each place its symbol of sky_map_symbols; then a
 * histogram of the satellites used in each hour seen, a row for each
 * count from the engine's channels, at least 6, down to 0, '|' within
 * the hour's fewest and most and ':' at the average of those tracked but
 * not used, '.' there outside them, beside a key and the Doppler bins
 * seen, '||' for a used satellite's and '..' for another's; and a line
 * marking the hour last sampled (0x4f6d8).  The Z3816A 4001's 0x4d3b2
 * is the same. */
void gps_print_sat_view(void) {
  static const uint8_t COLUMNS = 72;
  static const uint8_t ROWS = 9;
  static const uint8_t DEGREES_PER_ROW = 10;
  static const uint8_t DEGREES_PER_HALF = 5;
  static const int8_t FEWEST_ROWS = 6;
  /* the histogram's rows beside a line of the key, the rows above them
   * stopping at their last mark; row 0's key is the Doppler bins */
  static const int8_t KEY_ROWS = 3;
  static const char* const KEYS[] = {
      "",
      "============================",
      "ascent     apex      descent",
      "Doppler Range      [x100 Hz]",
  };
  static const uint8_t DOPPLER_BINS = 14;
  char lower[73];
  char upper[73];
  char hours[26];

  psos_pause(1);
  console_write(
      "\r\n\r\nS a t   V i e w  "
      "_______________________________________________________ 1.0\r\n\r\n");
  printf("   Scaled %s vs. Satellite Position\r\n",
         (gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0 ? "SS" : "C/N");
  console_write(
      "El n=====:=====:=====e=====:=====:=====s=====:=====:=====w=====:=====:="
      "====\r\n");
  for (int8_t row = ROWS - 1; row >= 0; row--) {
    uint8_t column;

    for (column = 0; column < COLUMNS; column++) {
      uint8_t cell = sky_statistics.map[column][row];

      upper[column] = sky_map_symbols[cell >> SKY_HIGH_SHIFT];
      lower[column] = sky_map_symbols[cell & SKY_LOW_NIBBLE];
    }
    upper[column] = 0;
    lower[column] = 0;
    uint8_t elevation = (uint8_t)(row * DEGREES_PER_ROW);

    printf("%02d%c%s|\r\n", elevation + DEGREES_PER_HALF,
           elevation + DEGREES_PER_HALF < (uint32_t)gps.mask_angle ? ':' : '|',
           upper);
    printf("%02d%c%s|\r\n", (uint32_t)elevation,
           elevation < gps.mask_angle ? ':' : '|', lower);
  }
  console_write(
      "   n=====:=====:=====e=====:=====:=====s=====:=====:=====w=====:=====:="
      "====\r\n");
  console_write(
      "Az 0    030   060   090   120   150   180   210   240   270   300   "
      "330\r\n");
  console_write("\r\n\r\n   Tracked Satellites/Hour\r\n");
  console_write("   ========================\r\n");
  for (int8_t used = gps.channel_count < 7 ? FEWEST_ROWS : gps.channel_count;
       used >= 0; used--) {
    uint8_t end = 0;
    uint8_t hour;

    for (hour = 0; hour < SKY_HOURS; hour++) {
      const bool seen = (sky_statistics.hours_seen & (1u << hour)) != 0;
      const uint8_t range = sky_statistics.used_range[hour];

      if (seen && used <= (int32_t)(range & SKY_LOW_NIBBLE) &&
          used >= (int32_t)(range >> SKY_HIGH_SHIFT)) {
        hours[hour] = used == sky_statistics.unused_average[hour] ? ':' : '|';
        end = hour + 1;
      } else if (seen && used == sky_statistics.unused_average[hour]) {
        hours[hour] = '.';
        end = hour + 1;
      } else {
        hours[hour] = ' ';
      }
    }
    hours[used > KEY_ROWS ? end : hour] = 0;
    printf("%2d %s", (int32_t)used, hours);
    if (used == 0) {
      uint16_t unused_bins = 0;
      uint16_t used_bins = 0;

      for (hour = 0; hour < SKY_HOURS; hour++) {
        unused_bins |= sky_statistics.doppler_bins[hour].unused;
        used_bins |= sky_statistics.doppler_bins[hour].used;
      }
      printf("      ");
      uint16_t bit = 1;

      for (uint8_t bin = 0; bin < DOPPLER_BINS; bin++) {
        printf("%s", (bit & used_bins) != 0     ? "||"
                     : (bit & unused_bins) != 0 ? ".."
                                                : "  ");
        bit <<= 1;
      }
    }
    printf("      %s\r\n", used <= KEY_ROWS ? KEYS[used] : "");
  }
  libc_copy(hours, "========================");
  hours[sky_statistics.hour] = '-';
  printf("   %s      ============================\r\n", hours);
  console_write(
      "Hr 0 0 0 0 0 1 1 1 1 1 2 2       +3 2 2 1 1 0 0 0 1 1 2 2 3-\r\n");
  console_write(
      "   0 2 4 6 8 0 2 4 6 8 0 2        0 5 0 5 0 5   5 0 5 0 5 0\r\n");
}
