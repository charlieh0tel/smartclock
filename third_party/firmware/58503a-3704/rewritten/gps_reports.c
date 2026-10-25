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

static const float GPS_REPORTS_THOUSANDTHS = 1000.0f;

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

/* An angle in milliarcseconds as "%c%4d:%02d:%06.3f": its hemisphere,
 * N or S for a latitude, E or W for a longitude, and its degrees,
 * minutes and seconds, each of the first two taken as a byte (0x48470). */
void format_angle(char* text, int32_t mas, uint8_t latitude) {
  char hemisphere;

  if (mas < 0) {
    hemisphere = latitude != 0 ? 'S' : 'W';
    mas = -mas;
  } else {
    hemisphere = latitude != 0 ? 'N' : 'E';
  }
  uint8_t degrees = mas / GPS_MAS_PER_DEGREE;
  int32_t rest = mas - degrees * GPS_MAS_PER_DEGREE;
  uint8_t minutes = rest / GPS_MAS_PER_MINUTE;
  float seconds =
      float_divide(integer_to_float(rest - minutes * GPS_MAS_PER_MINUTE),
                   GPS_REPORTS_THOUSANDTHS);

  sprintf(text, "%c%4d:%02d:%06.3f", hemisphere, degrees, minutes,
          float_to_double(seconds));
}

/* A time of the week in seconds as "%02d:%02d:%04.1f [%s]", its hours,
 * minutes and seconds and its day's name (0x4851a).  The names are the
 * image's table, indexed unchecked by the day plus one: "---" and "Sun"
 * to "Sat". */
void format_duration(double time, char* text) {
  int32_t seconds = double_to_integer(time);
  uint8_t day = libc_divide(&seconds, CLOCK_SECONDS_PER_DAY) + 1;
  uint8_t hours = libc_divide(&seconds, CLOCK_SECONDS_PER_HOUR);
  uint8_t minutes = libc_divide(&seconds, CLOCK_SECONDS_PER_MINUTE);
  char* day_name = day_names[day];

  sprintf(text, "%02d:%02d:%04.1f [%s]", hours, minutes,
          float_to_double(integer_to_float(seconds)), day_name);
}

/* Empties the command log (0x4d156). */
void command_log_clear(void) {
  command_log.write = command_log.entries;
  command_log.read = command_log.entries;
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
 * full; 1 (0x4d166). */
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

/* Prints a command log, taking its entries (0x4d432). */
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
    console_write("(no commands were sent since last read)\r\n\r\n");
  }
}

/* Prints the command log (0x4d4c6). */
void gps_print_commands(void) { gps_print_command_log(&command_log); }

/* Prints the status snapshots frozen at a GPS receiver error, the
 * latest and the flagged one, and the command log copied with them,
 * leaving the copy to print again (0x4d4d4). */
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

/* "OK" or "Fail" for a monitor's failed flag. */
static const char* gps_reports_failed(uint8_t failed) {
  return failed == 0 ? "OK" : "Fail";
}

/* Prints @@Cj, the receiver's identification (0x497ee). */
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

/* Copies "Enabled" for 1, "Disabled" for 0, else "?" (0x48d2c). */
void enabled_string(uint8_t enabled, char* text) {
  const char* name;

  if (enabled == 1) {
    name = "Enabled";
  } else if (enabled == 0) {
    name = "Disabled";
  } else {
    name = "?";
  }
  libc_copy(text, name);
}
