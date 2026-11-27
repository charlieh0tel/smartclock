/* gps_reports.c's functions that are this image's own: the rest are
 * common/gps_reports.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/gps_reports.c:
 * command_log_add, whose decompiler's reading writes the entry field by field
 * where the image's builds it in its frame and copies it whole, the 58503A's
 * code but for this image's entry, its sender first, and its date and time,
 * status_snapshot_time's (docs/firmware/z3816a-4001.md, "The function check").
 * Its comments' addresses are the Z3816A's.  Each function keeps its generated
 * prototype. With them, from the 58503A 3704's rewritten/gps_reports.c, of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Their comments' addresses
 * are the Z3816A's. */

/* The entry after one in a command log's ring. */
static gps_command_log_entry* gps_reports_log_next(
    gps_command_log* log, gps_command_log_entry* entry) {
  static const uint32_t ENTRIES = sizeof log->entries / sizeof log->entries[0];

  entry++;
  return entry >= log->entries + ENTRIES ? log->entries : entry;
}

/* Logs a command gpsm_task sends, with the first byte after its ID as
 * the message text gives it in hex, and the status snapshot's date and
 * time; the oldest entry goes once the log is full; 1 (0x513f4). */
int32_t command_log_add(gps_running_command* command, char* text) {
  /* the hex digits' place in gps_message_to_text's text, after "@@" and
   * the ID */
  static const uint8_t ARGUMENT_TEXT = 4;
  gps_command_log_entry entry;

  entry.sender = command->request.sender;
  entry.code = command->request.code;
  entry.message = command->message;
  entry.argument_text[0] = text[ARGUMENT_TEXT];
  entry.argument_text[1] = text[ARGUMENT_TEXT + 1];
  entry.argument_text[2] = 0;
  entry.query = command->query;
  status_snapshot_time(&entry.date, &entry.time, &entry.from_clock);

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

/* Prints @@Cj, the receiver's identification (0x4d2fe). */
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

/* Prints @@Bb, the satellites predicted visible (0x4cea8). */
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

/* Prints @@Bc, the best satellite combinations by xDOP (0x4bda8).  Its
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
 * (0x4be84). */
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
 * deviation (0x4c42a). */
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

/* Clears the satellite view, field by field (0x51ede). */
void satellite_view_clear(void) {
  memset(satellite_view.grid, 0, sizeof satellite_view.grid);
  /* the hours' ranges and the hour after them */
  memset(satellite_view.used_range, 0,
         offsetof(satellite_view_state, tracked_mean) -
             offsetof(satellite_view_state, used_range));
  memset(satellite_view.tracked_mean, 0, sizeof satellite_view.tracked_mean);
  satellite_view.hours_seen = 0;
  satellite_view.hour_mean_count = 0;
  satellite_view.hour_mean = 0.0f;
  memset(satellite_view.doppler, 0, sizeof satellite_view.doppler);
}

/* A status record's height, MSL for an engine that gives MSL heights,
 * else GPS. */
static int32_t gps_reports_height(const oncore_status_fields* status) {
  return (gps.engine & GPS_ENGINE_HEIGHT_MSL) != 0 ? status->height_msl
                                                   : status->height_gps;
}

/* Prints a status record: time, position, heights, DOP and the channels'
 * signal levels, headed C/N for a UT Plus, else SS (0x4cf50).  The
 * Z3815A 4010's but for the visible count beside the longitude and the
 * DOP type's bit 0 alone choosing HDOP. */
void gps_print_status(oncore_status_fields* status) {
  static const uint8_t DOP_TYPE_HDOP = 0x01;
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
  char* format = (status->dop_type & DOP_TYPE_HDOP) != 0 ? "HDOP%23.1f\r\n"
                                                         : "PDOP%23.1f\r\n";

  printf(format, gps_reports_scaled_unsigned(status->dop, GPS_REPORTS_TENTHS));
  console_write("\r\n");
  printf("CHN      SVID      MODE      %3s      STATUS\r\n",
         (gps.engine & GPS_ENGINE_UT_PLUS) != 0 ? "C/N" : "SS");
  for (int32_t channel = 0; channel < gps.channel_count; channel++) {
    oncore_channel_status* tracked = &status->channels[channel];

    /* The mode sign-extended, as the image passes it. */
    printf("%2d%10d%10d%10d        %02X\r\n", channel + 1,
           (uint32_t)tracked->satellite, (int32_t)(int8_t)tracked->mode,
           (uint32_t)tracked->signal_strength, (uint32_t)tracked->status);
  }
}

/* Prints gps.status as three "gs: " lines, for the console word
 * abr_stat (0x4d130), its height as gps_reports_height picks it; the
 * last channel's comma dropped.  The Z3815A 4010's but that its first
 * two lines go through printf, where the Z3815A's go through
 * debug_printf (0x4d14a). */
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

/* Prints the survey's averages, its height MSL or GPS as the engine
 * gives them, and how many channels have their status's bit 7
 * (0x4cd8c).  The Z3815A 4010's. */
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
  double* height = (gps.engine & GPS_ENGINE_HEIGHT_MSL) != 0
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

/* Prints a command log, taking its entries, headed by the engine's kind
 * (0x51608).  The 58503A 3704's but for the heading, the log's 50
 * entries and the texts. */
void gps_print_command_log(gps_command_log* log) {
  /* the most a log holds, one entry short of its ring */
  static const uint8_t MOST_COMMANDS = 50;
  char line[50];
  uint8_t printed;

  psos_pause(1);
  console_write("\r\n");
  printf("%s Oncore Command Log __________________",
         (gps.engine & GPS_ENGINE_VP) != 0        ? "VP"
         : (gps.engine & GPS_ENGINE_UT_PLUS) != 0 ? "UT"
                                                  : "??");
  printf(" [last %2d]\r\n", MOST_COMMANDS);
  console_write("Task   ID CID  Command     Date and Time Issued\r\n");
  console_write("--------------------------------------------------\r\n");
  for (printed = 0; printed < MOST_COMMANDS; printed++) {
    if (gps_print_command(log, line) != 1) {
      break;
    }
    printf("%s\r\n", line);
  }
  if (printed == 0) {
    console_write("(no commands issued since last queried)\r\n\r\n");
  }
}

/* The Time RAIM status, pr_time_raim's: the mode, raim_monitor's
 * assessment and alarm counts, @@Bn's solution and 1PPS, and each
 * channel's fractional time estimate, beside raim_monitor's record of
 * the last alarm detected once there is one (0x4c062).  The 58503A
 * 3704's but for its layout and texts, the engine's Time RAIM bit
 * (gps.engine), the alarm limit's unit and every channel listed by its
 * mode. */
void gps_print_time_raim(void) {
  /* @@Bn's pulse reference past oncore_pulse_sync's two */
  static const uint8_t PULSE_EPOCH = 2;
  /* alarm_limit's unit */
  static const int32_t ALARM_LIMIT_NS = 10;
  oncore_raim_fields* raim = &gps.raim;
  const char* text;
  const char* output;
  char limit[9];
  char time[9];
  char date[13];

  psos_pause(1);
  console_write("\r\n\r\n");
  if ((gps.engine & GPS_ENGINE_TRAIM) == 0) {
    console_write("Time RAIM capability is not installed.\r\n");
    return;
  }
  printf(
      "------------------------ Time RAIM Status "
      "------------------------\r\n");
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
  /* An unknown reference leaves the detection's text: the image's
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
  bool alarmed = gps.raim_alarm_date != 0;

  if (!alarmed) {
    console_write("\r\nChn   PRN   Time (ns)\r\n");
    console_write("---------------------\r\n");
  } else {
    format_time(gps.raim_alarm_time, time);
    format_date(clock_date_of(gps.raim_alarm_date), date);
    console_write("      ___ Current ___    ____ Last Alarm ____");
    printf(" %s %s\r\n", time, date);
    printf(
        "Chn   PRN   Time (ns)    PRN   Time (ns)  Acq   Sigma Est %d ns\r\n",
        (uint32_t)gps.raim_alarm_sigma);
    console_write("---------------------------------------------\r\n");
  }
  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    const oncore_raim_channel* now = &raim->channels[channel];
    uint8_t satellite = now->satellite;

    printf(" %d     %2d%s  %9d", channel + 1, (uint32_t)satellite,
           now->mode == ONCORE_AVAILABLE_FOR_POSITION ? "*" : " ",
           now->time_estimate);
    if (!alarmed) {
      console_write("\r\n");
    } else {
      const oncore_raim_channel* last = &gps.raim_alarm_channels[channel];

      printf("     %2d%s  %9d    %d\r\n", (uint32_t)last->satellite,
             last->mode == ONCORE_AVAILABLE_FOR_POSITION ? "*" : " ",
             last->time_estimate, (int32_t)(int8_t)last->mode);
    }
  }
  console_write("\r\n* used in time solution\r\n");
}

/* gps_print_instrument_state's list of the ignored PRNs, one text
 * across four columns of 31 characters, split by a NUL at each column's
 * end, as the image's adjacent buffers are; the columns go on the four
 * lines after. */
#define GPS_REPORTS_IGNORED_COLUMNS 4

#define GPS_REPORTS_IGNORED_COLUMN_SIZE 32

/* gps_print_instrument_state's padding where a VP's thresholds go. */
static const char GPS_REPORTS_NO_THRESHOLD[] = "                    ";

/* @@Ar's fix algorithm and its two option bits, which a VP reports
 * (VPCommands.pdf, @@Ar). */
static const uint8_t GPS_REPORTS_FIX_ALGORITHM = 0x3f;
static const uint8_t GPS_REPORTS_FIX_AFC_TRACK_OFF = 0x40;
static const uint8_t GPS_REPORTS_FIX_FILTER_OFF = 0x80;

/* The instrument state settings, pr_gps_istate's: the time, date and GMT
 * correction, the position hold, the held and initial positions, the
 * ignored PRNs, the elevation mask, the atmospheric corrections, the
 * time scale and leap second, the 1PPS offset and cable delay, and for a
 * VP its application, fix algorithm, satellite selection, DOP settings,
 * thresholds and DCXO; for a UT Plus @@Bo's UTC offset and, with @@Gj,
 * its fraction and the leap seconds (0x4c5d8).  The 58503A 3704's but
 * for what the engine's kind (gps.engine) leaves out, the atmospheric
 * corrections, the leap second's date and time, and its layout. */
void gps_print_instrument_state(void) {
  static const uint8_t LAST_GPS = 32;
  char ignored[GPS_REPORTS_IGNORED_COLUMNS * GPS_REPORTS_IGNORED_COLUMN_SIZE];
  char other[16];
  const char* name;
  const char* operation;
  char sign;
  int8_t minutes;
  bool vp = (gps.engine & GPS_ENGINE_VP) != 0;

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
  if (!vp) {
    console_write("\r\n");
  } else {
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
  }
  if (vp) {
    uint8_t options = gps.fix_algorithm & ~GPS_REPORTS_FIX_ALGORITHM;

    switch (gps.fix_algorithm & GPS_REPORTS_FIX_ALGORITHM) {
      case ONCORE_FIX_BEST_FOUR:
        name = "Best 4 satellites";
        break;
      case ONCORE_FIX_ALL_IN_VIEW:
        libc_copy(ignored, "N-in-view");
        if ((options & GPS_REPORTS_FIX_AFC_TRACK_OFF) != 0) {
          strcat(ignored, " / AFC Track Off");
        }
        if ((options & GPS_REPORTS_FIX_FILTER_OFF) != 0) {
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
  for (int8_t column = GPS_REPORTS_IGNORED_COLUMNS - 1; column >= 0; column--) {
    ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE] = 0;
  }
  if ((gps.engine & GPS_ENGINE_IGNORED_SATELLITES) == 0) {
    console_write("\r\n");
  } else {
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
    for (uint8_t column = GPS_REPORTS_IGNORED_COLUMNS - 1; column > 0;
         column--) {
      ignored[column * GPS_REPORTS_IGNORED_COLUMN_SIZE - 1] = 0;
    }
  }
  printf("ELEV MASK  %-2d%33s%s\r\n", (uint32_t)gps.mask_angle, "", ignored);
  if (vp) {
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
  switch (gps.ionospheric_correction) {
    case ONCORE_ATMOSPHERE_NONE:
      name = "Disabled";
      break;
    case ONCORE_ATMOSPHERE_IONOSPHERIC:
      name = "Ionospheric";
      break;
    case ONCORE_ATMOSPHERE_TROPOSPHERIC:
      name = "Tropospheric";
      break;
    case ONCORE_ATMOSPHERE_BOTH:
      name = "Ion and Trop";
      break;
    default:
      name = "?";
      break;
  }
  printf("ATM CORR   %-12s", name);
  if (vp) {
    printf("   DOP 3D/2D    %4.1f   %s\r\n",
           gps_reports_scaled_unsigned(gps.xdop_threshold, GPS_REPORTS_TENTHS),
           &ignored[3 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  } else if ((gps.engine & GPS_ENGINE_IGNORED_SATELLITES) == 0) {
    console_write("\r\n");
  } else {
    printf("%23s%s\r\n", " ", &ignored[1 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
    printf("%46s%s\r\n", " ", &ignored[2 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
    printf("%46s%s\r\n", " ", &ignored[3 * GPS_REPORTS_IGNORED_COLUMN_SIZE]);
  }

  printf(
      "\r\nCorrection Factors %s_________   General Status "
      "_________________\r\n",
      vp ? "and Thresholds " : "_______________");
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
  if (gps.leap_status.leap_year == 0) {
    console_write("\r\n");
  } else {
    printf(", %02d:%02d:%02d %02d/%02d/%04d\r\n",
           (uint32_t)gps.leap_status.leap_hour,
           (uint32_t)gps.leap_status.leap_minute,
           (uint32_t)gps.leap_status.leap_second,
           (uint32_t)gps.leap_status.leap_month,
           (uint32_t)gps.leap_status.leap_day,
           (uint32_t)gps.leap_status.leap_year);
  }
  printf("1PPS OFFS  %9d ns   ", gps.pps_offset);
  if (!vp) {
    console_write(GPS_REPORTS_NO_THRESHOLD);
  } else {
    printf("POS FIX THR %3d s   ", (uint32_t)gps.position_fix_threshold);
  }
  if ((gps.engine & GPS_ENGINE_UT_PLUS) != 0) {
    printf("UTC OFFS %+d s", (int32_t)gps.utc_offset);
  }
  if ((gps.engine & GPS_ENGINE_UT_PLUS) == 0 ||
      (gps.engine & GPS_ENGINE_LEAP_STATUS) == 0 ||
      (gps.leap_status.utc_offset_seconds == 0 &&
       gps.leap_status.utc_offset_fraction == 0)) {
    console_write("\r\n");
  } else {
    printf("  [%d.%09d s]\r\n", (int32_t)gps.leap_status.utc_offset_seconds,
           gps.leap_status.utc_offset_fraction);
  }
  /* the delay as the engine gives it */
  printf("CABLE DLY     %6d ns   ", gps.pps_cable_delay_given);
  if (!vp) {
    console_write(GPS_REPORTS_NO_THRESHOLD);
  } else {
    printf("DIF OUT THR %3d s   ", (uint32_t)gps.differential_output_threshold);
  }
  if ((gps.engine & GPS_ENGINE_UT_PLUS) == 0 ||
      (gps.engine & GPS_ENGINE_LEAP_STATUS) == 0 ||
      gps.leap_status.present_leap_seconds == 0) {
    console_write("\r\n");
  } else {
    printf("Tls/Tlsf %+d s/%+d s\r\n",
           (int32_t)gps.leap_status.present_leap_seconds,
           (int32_t)gps.leap_status.future_leap_seconds);
  }
  if (!vp) {
    console_write("\r\n");
  } else {
    printf("DCXO FREQ  Offs %d  Warp %d  Step %d\r\n",
           (uint32_t)gps.dcxo_drift_reference, (uint32_t)gps.dcxo_setting,
           (uint32_t)gps.dcxo_hz_per_step);
  }
}

/* The name of a GPS request's sender, as the command log prints it (its
 * jump table at 0x51512). */
static const char* gps_reports_sender_name(gps_request_sender sender) {
  switch (sender) {
    case GPS_SENDER_SEQUENCE:
      return "PLL";
    case GPS_SENDER_SETTING:
      return "SCPI1";
    case GPS_SENDER_SETTING_OTHER:
      return "SCPI2";
    case GPS_SENDER_POLL_MONITOR:
      return "GPSq";
    case GPS_SENDER_COMMAND_MONITOR:
      return "GPSc";
    case GPS_SENDER_STATUS_MONITOR:
      return "GPSs";
    default:
      return "?";
  }
}

/* oncore_messages' entry for the raw message, whose ID is empty; the
 * command log prints it "**". */
static const int16_t GPS_REPORTS_RAW_MESSAGE = 0x3f;

/* Takes a command log's oldest entry and prints it into a line; 0 when
 * the log is empty, else 1 (0x51498).  The 58503A 3704's but for its
 * senders, the message's index printed after the code, a raw message's
 * ID "**" and the ID's width. */
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
  sprintf(line, "%-5s %3d %3d  %-2.2s %-2s [%s]   %11s  %8s %s",
          gps_reports_sender_name(entry.sender), (int32_t)entry.code,
          (int32_t)entry.message,
          entry.message == GPS_REPORTS_RAW_MESSAGE
              ? "**"
              : oncore_message_entry_at(entry.message)->id,
          entry.argument_text, entry.query != GPS_ARGUMENTS_SET ? "q" : "c",
          date, time, entry.from_clock != 0 ? " " : "e");
  return 1;
}

/* "OK" or "Fail" for a monitor's failed flag. */
static const char* gps_reports_failed(uint8_t failed) {
  return failed == 0 ? "OK" : "Fail";
}

/* Prints the GPS Process Monitor: the GPS task's state and counters,
 * its last request and program message, the response buffer in hex,
 * and the monitors' health (0x516da).  The 58503A 3704's but for its
 * senders and spacing and the monitors' state names, the Z3816A's own
 * (their jump tables at 0x51b32 and 0x51ba6). */
void gps_print_process_monitor(void) {
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

  /* Each byte as two hex digits; the loop that would strip a third
   * digit's leading 0 or f never finds one. */
  char hex[5];
  /* The buffer's first 20 bytes, two digits each. */
  static const uint32_t SHOWN = 20;
  char bytes[2 * 20 + 1];

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
         gps_reports_failed(gps.status_monitor_failed),
         (uint32_t)gps_counts.status_monitor_failures);
}

/* The satellite view's grid: its azimuths, a column each 5 degrees, and
 * its elevations, a byte each 10 degrees, two cells of 5 degrees. */
#define GPS_REPORTS_VIEW_AZIMUTHS 72

static const uint8_t GPS_REPORTS_VIEW_ELEVATIONS = 9;

static const uint8_t GPS_REPORTS_VIEW_CELL_BITS = 4;

static const uint8_t GPS_REPORTS_VIEW_CELL_MASK = 0x0f;

/* Degrees a column of the grid spans, and a cell. */
static const uint8_t GPS_REPORTS_VIEW_AZIMUTH_STEP = 5;

static const uint8_t GPS_REPORTS_VIEW_ELEVATION_STEP = 10;

static const uint8_t GPS_REPORTS_VIEW_CELL_STEP = 5;

/* The hours of the day the view keeps, and its Doppler ranges. */
#define GPS_REPORTS_VIEW_HOURS 24

static const uint8_t GPS_REPORTS_VIEW_DOPPLER_RANGES = 14;

/* The satellite view: the grid of where satellites on a channel are
 * seen, each elevation's two rows of cells, each shown by its character
 * (satellite_view_characters), ':' beside those below the elevation
 * mask; then the satellites tracked by hour, each hour's range of those
 * used and mean of those tracked, from the engine's channels or 6 down,
 * with the Doppler ranges seen beside the last four rows; the hour last
 * gathered marked under them (0x4d3b2). */
void satellite_view_print(void) {
  /* The least the hours' rows start from. */
  static const int8_t FEWEST_ROWS = 6;
  /* The four rows the Doppler ranges' title, labels, rule and bars go
   * beside, which the hours' text is printed whole on; above them it is
   * cut after its last mark. */
  static const int8_t DOPPLER_TITLE_ROW = 3;
  static const int8_t DOPPLER_LABELS_ROW = 2;
  static const int8_t DOPPLER_RULE_ROW = 1;
  static const int8_t DOPPLER_BARS_ROW = 0;
  char upper[GPS_REPORTS_VIEW_AZIMUTHS + 1];
  char lower[GPS_REPORTS_VIEW_AZIMUTHS + 1];
  char hours[GPS_REPORTS_VIEW_HOURS + 2];

  psos_pause(1);
  console_write(
      "\r\n\r\nS a t   V i e w  "
      "_______________________________________________________ 1.0\r\n\r\n");
  printf("   Scaled %s vs. Satellite Position\r\n",
         (gps.engine & GPS_ENGINE_VP) == 0 ? "C/N" : "SS");
  console_write(
      "El n=====:=====:=====e=====:=====:=====s=====:=====:=====w=====:=====:"
      "=====\r\n");
  for (int8_t elevation = GPS_REPORTS_VIEW_ELEVATIONS - 1; elevation >= 0;
       elevation--) {
    uint8_t azimuth;

    for (azimuth = 0; azimuth < GPS_REPORTS_VIEW_AZIMUTHS; azimuth++) {
      const uint8_t cells =
          satellite_view
              .grid[elevation + azimuth * GPS_REPORTS_VIEW_ELEVATIONS];

      upper[azimuth] =
          satellite_view_characters[cells >> GPS_REPORTS_VIEW_CELL_BITS];
      lower[azimuth] =
          satellite_view_characters[cells & GPS_REPORTS_VIEW_CELL_MASK];
    }
    upper[azimuth] = 0;
    lower[azimuth] = 0;
    const uint8_t degrees = elevation * GPS_REPORTS_VIEW_ELEVATION_STEP;

    printf("%02d%c%s|\r\n", degrees + GPS_REPORTS_VIEW_CELL_STEP,
           degrees + GPS_REPORTS_VIEW_CELL_STEP < (uint32_t)gps.mask_angle
               ? ':'
               : '|',
           upper);
    printf("%02d%c%s|\r\n", (uint32_t)degrees,
           degrees < gps.mask_angle ? ':' : '|', lower);
  }
  console_write(
      "   n=====:=====:=====e=====:=====:=====s=====:=====:=====w=====:=====:"
      "=====\r\n");
  console_write(
      "Az 0    030   060   090   120   150   180   210   240   270   300   "
      "330\r\n");
  console_write("\r\n\r\n   Tracked Satellites/Hour\r\n");
  console_write("   ========================\r\n");
  for (int8_t row = gps.channel_count <= FEWEST_ROWS ? FEWEST_ROWS
                                                     : gps.channel_count;
       row >= DOPPLER_BARS_ROW; row--) {
    const char* text;
    uint8_t marked = 0;
    uint8_t hour;

    for (hour = 0; hour < GPS_REPORTS_VIEW_HOURS; hour++) {
      const bool seen = (satellite_view.hours_seen & (1 << hour)) != 0;
      const uint8_t range = satellite_view.used_range[hour];

      if (seen && (range & GPS_REPORTS_VIEW_CELL_MASK) >= row &&
          row >= range >> GPS_REPORTS_VIEW_CELL_BITS) {
        hours[hour] = row == satellite_view.tracked_mean[hour] ? ':' : '|';
        marked = hour + 1;
      } else if (seen && row == satellite_view.tracked_mean[hour]) {
        hours[hour] = '.';
        marked = hour + 1;
      } else {
        hours[hour] = ' ';
      }
    }
    hours[row > DOPPLER_TITLE_ROW ? marked : hour] = 0;
    printf("%2d %s", (int32_t)row, hours);
    if (row == DOPPLER_TITLE_ROW) {
      text = "Doppler Range      [x100 Hz]";
    } else if (row == DOPPLER_LABELS_ROW) {
      text = "ascent     apex      descent";
    } else if (row == DOPPLER_RULE_ROW) {
      text = "============================";
    } else if (row == DOPPLER_BARS_ROW) {
      uint16_t tracked = 0;
      uint16_t used = 0;

      for (hour = 0; hour < GPS_REPORTS_VIEW_HOURS; hour++) {
        tracked |= satellite_view.doppler[hour].tracked;
        used |= satellite_view.doppler[hour].used;
      }
      printf("      ");
      uint16_t bit = 1;

      for (uint8_t range = 0; range < GPS_REPORTS_VIEW_DOPPLER_RANGES;
           range++) {
        printf("%s", (bit & used) != 0      ? "||"
                     : (bit & tracked) != 0 ? ".."
                                            : "  ");
        bit <<= 1;
      }
      text = "";
    } else {
      text = "";
    }
    printf("      %s\r\n", text);
  }
  libc_copy(hours, "========================");
  hours[satellite_view.hour] = '-';
  printf("   %s      ============================\r\n", hours);
  console_write(
      "Hr 0 0 0 0 0 1 1 1 1 1 2 2       +3 2 2 1 1 0 0 0 1 1 2 2 3-\r\n");
  console_write(
      "   0 2 4 6 8 0 2 4 6 8 0 2        0 5 0 5 0 5   5 0 5 0 5 0\r\n");
}

/* A used satellite's signal level as the view's cell, 1 to 9: a UT
 * Plus's C/N, 1 below 32, else (C/N - 26) / 3; a VP's signal strength,
 * by satellite_view_vp_levels (0x52060). */
static uint8_t gps_reports_view_level(int16_t level) {
  static const int16_t LEAST_CN = 32;
  static const int16_t CN_OFFSET = 26;
  static const int16_t CN_STEP = 3;

  if ((gps.engine & GPS_ENGINE_VP) == 0) {
    if (level < LEAST_CN) {
      return SATELLITE_VIEW_LEVEL_LOWEST;
    }
    int32_t scaled = (level - CN_OFFSET) / CN_STEP;

    return scaled < SATELLITE_VIEW_LEVEL_HIGHEST ? (uint8_t)scaled
                                                 : SATELLITE_VIEW_LEVEL_HIGHEST;
  }
  uint8_t cell = SATELLITE_VIEW_LEVEL_LOWEST;

  while (cell < SATELLITE_VIEW_LEVEL_HIGHEST &&
         satellite_view_vp_levels[cell - SATELLITE_VIEW_LEVEL_LOWEST] < level) {
    cell++;
  }
  return cell;
}

/* A tracked satellite's cell by its Doppler: within 100 Hz its apex,
 * else ascent or descent; a cell already one of the three, another,
 * mixed. */
static uint8_t gps_reports_view_motion(int16_t doppler, uint8_t old) {
  static const int32_t APEX_HZ = 100;
  uint8_t cell;

  if (abs(doppler) <= APEX_HZ) {
    cell = SATELLITE_VIEW_APEX;
  } else if (doppler < 1) {
    cell = SATELLITE_VIEW_DESCENT;
  } else {
    cell = SATELLITE_VIEW_ASCENT;
  }
  if (cell != old && old > SATELLITE_VIEW_TRACKED &&
      old < SATELLITE_VIEW_MIXED) {
    cell = SATELLITE_VIEW_MIXED;
  }
  return cell;
}

/* Gathers the satellite view from gps.status's channels, as the
 * tracking log's countdown ends (gpsm_task, 0x51160): each satellite on
 * a channel with a position into the grid's cell, a used one by its
 * signal level, averaged with the cell's, else its motion; each
 * satellite's Doppler range into the hour's; and the hour's range of
 * satellites used and running mean, to 7201 passes, of those tracked but
 * not used, the hour's record starting afresh as it changes (0x51f38). */
void satellite_view_gather(void) {
  /* satellite_status_blank_entry's Doppler, none known */
  static const int16_t DOPPLER_UNKNOWN = -0x8000;
  /* Doppler's ranges: 500 Hz, from +3000 Hz, ranges 0 to 13. */
  static const int32_t DOPPLER_RANGE_HZ = -500;
  static const int32_t DOPPLER_ROUND = 499;
  static const int16_t DOPPLER_RANGE_ZERO = 6;
  static const uint16_t LAST_AZIMUTH = 359;
  static const uint8_t LAST_ELEVATION = 89;
  static const uint16_t MOST_PASSES = 7200;
  static const double HALF = 0.5;
  static const uint8_t RANGE_START = 0xf0;
  uint16_t tracked_dopplers = 0;
  uint16_t used_dopplers = 0;
  satellite_sets sets;
  satellite_list_entry list[8];
  uint8_t listed;
  uint8_t used = 0;
  satellite_list_entry* entry = list;

  satellite_sets_build(gps.status.channels, &sets);
  satellite_status_list(2, gps.channel_count, &sets, gps.status.channels,
                        (gps_visibility_record*)&gps.visibility_received, list,
                        &listed);
  for (uint8_t i = 0; i < listed; i++, entry++) {
    if (entry->attempting == 0) {
      used++;
    }
    if (entry->cell.cell.status != SATELLITE_CELL_POSITION) {
      continue;
    }
    const uint32_t azimuth = entry->cell.cell.azimuth < LAST_AZIMUTH
                                 ? entry->cell.cell.azimuth
                                 : LAST_AZIMUTH;
    const uint8_t column = azimuth / GPS_REPORTS_VIEW_AZIMUTH_STEP;
    const uint8_t elevation = entry->cell.cell.elevation < LAST_ELEVATION
                                  ? entry->cell.cell.elevation
                                  : LAST_ELEVATION;
    const uint8_t row = elevation / GPS_REPORTS_VIEW_ELEVATION_STEP;
    const bool upper = (elevation / GPS_REPORTS_VIEW_CELL_STEP) % 2 != 0;
    uint8_t* cells =
        &satellite_view.grid[row + column * GPS_REPORTS_VIEW_ELEVATIONS];
    const uint8_t old =
        (upper ? *cells >> GPS_REPORTS_VIEW_CELL_BITS : *cells) &
        GPS_REPORTS_VIEW_CELL_MASK;
    uint8_t cell = SATELLITE_VIEW_NONE;

    if (entry->attempting == 0) {
      if (entry->level == -1) {
        if (old == SATELLITE_VIEW_NONE || old > SATELLITE_VIEW_LEVEL_HIGHEST) {
          cell = SATELLITE_VIEW_USED;
        }
      } else {
        cell = gps_reports_view_level(entry->level);
        if (old != SATELLITE_VIEW_NONE && old < SATELLITE_VIEW_USED) {
          cell = (old + cell) >> 1;
        }
      }
    } else if ((old == SATELLITE_VIEW_NONE || old > SATELLITE_VIEW_USED) &&
               old != SATELLITE_VIEW_MIXED) {
      if (entry->cell.doppler == DOPPLER_UNKNOWN) {
        if (old == SATELLITE_VIEW_NONE) {
          cell = SATELLITE_VIEW_TRACKED;
        }
      } else {
        cell = gps_reports_view_motion(entry->cell.doppler, old);
      }
    }
    if (cell != SATELLITE_VIEW_NONE) {
      *cells &= upper
                    ? GPS_REPORTS_VIEW_CELL_MASK
                    : GPS_REPORTS_VIEW_CELL_MASK << GPS_REPORTS_VIEW_CELL_BITS;
      *cells |= upper ? cell << GPS_REPORTS_VIEW_CELL_BITS : cell;
    }
    if (entry->cell.doppler != DOPPLER_UNKNOWN) {
      int16_t range =
          (int16_t)((entry->cell.doppler -
                     (entry->cell.doppler < 0 ? DOPPLER_ROUND : 0)) /
                    DOPPLER_RANGE_HZ) +
          DOPPLER_RANGE_ZERO;

      if (range >= GPS_REPORTS_VIEW_DOPPLER_RANGES) {
        range = GPS_REPORTS_VIEW_DOPPLER_RANGES - 1;
      }
      if (range < 1) {
        range = 0;
      }
      if (entry->attempting == 0) {
        used_dopplers |= 1 << range;
      } else {
        tracked_dopplers |= 1 << range;
      }
    }
  }
  uint32_t date;
  time_of_day time;
  uint8_t from_clock;

  status_snapshot_time(&date, &time, &from_clock);
  if (time.hour >= GPS_REPORTS_VIEW_HOURS) {
    return;
  }
  if (time.hour != satellite_view.hour) {
    satellite_view.hour_mean_count = 0;
    satellite_view.used_range[time.hour] = RANGE_START;
    satellite_view.hour = time.hour;
    satellite_view.tracked_mean[time.hour] = 0;
    satellite_view.hour_mean = 0.0f;
    satellite_view.doppler[time.hour].tracked = 0;
    satellite_view.doppler[time.hour].used = 0;
  }
  satellite_view.doppler[time.hour].tracked |= tracked_dopplers;
  satellite_view.doppler[time.hour].used |= used_dopplers;
  uint8_t* range = &satellite_view.used_range[time.hour];

  if (used < *range >> GPS_REPORTS_VIEW_CELL_BITS) {
    *range &= GPS_REPORTS_VIEW_CELL_MASK;
    *range |= used << GPS_REPORTS_VIEW_CELL_BITS;
  } else if ((*range & GPS_REPORTS_VIEW_CELL_MASK) < used) {
    *range &= GPS_REPORTS_VIEW_CELL_MASK << GPS_REPORTS_VIEW_CELL_BITS;
    *range |= used;
  }
  satellite_view.hours_seen |= 1 << time.hour;
  if (satellite_view.hour_mean_count <= MOST_PASSES) {
    float sum = float_add(
        integer_to_float(listed - used),
        float_multiply(unsigned_to_float(satellite_view.hour_mean_count),
                       satellite_view.hour_mean));

    satellite_view.hour_mean =
        float_divide(sum, integer_to_float(satellite_view.hour_mean_count + 1));
    satellite_view.tracked_mean[time.hour] = (uint8_t)double_to_unsigned(
        double_add(HALF, float_to_double(satellite_view.hour_mean)));
    satellite_view.hour_mean_count++;
  }
}
