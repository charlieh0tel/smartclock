/* The functions of gps_reports.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_reports.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

static const float GPS_REPORTS_THOUSANDTHS = 1000.0f;

/* An angle in milliarcseconds as "%c%4d:%02d:%06.3f": its hemisphere,
 * N or S for a latitude, E or W for a longitude, and its degrees,
 * minutes and seconds, each of the first two taken as a byte. */
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
 * minutes and seconds and its day's name.  The names are the
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

/* Empties the command log. */
void command_log_clear(void) {
  command_log.write = command_log.entries;
  command_log.read = command_log.entries;
}

/* Prints the command log. */
void gps_print_commands(void) { gps_print_command_log(&command_log); }

/* Copies "Enabled" for 1, "Disabled" for 0, else "?". */
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
