/* From the 58503A 3704's rewritten/gps.h, the Z3816A 4001's own beside
 * common/'s: what the GPS engine's files and the status screen share, the
 * receiver's channels and an angle's text in degrees, minutes and seconds.
 * The exported C includes the hand-written headers after its own declarations
 * and before the hand-written .c files, so every one of them can use it. */

/* The most channels an engine has, 1 to 8, its own count being
 * gps.channel_count; the satellite lists' limit (satellite_list_limits,
 * 0x3cc32). */
static const uint8_t GPS_CHANNELS = 8;

/* A six-channel engine's channels, gps.channel_count for one not known
 * to have eight. */
static const uint8_t GPS_SIX_CHANNELS = 6;

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then degrees, minutes and seconds by format, which gives the
 * hemisphere and the three (format_dms_precise, 0x4bb3a). */
static void gps_format_dms(char* text, double mas, char latitude,
                           const char* format) {
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
      double_divide_reversed_pointer((double)GPS_MAS_PER_DEGREE, &mas));
  double_subtract_in_place(&mas,
                           integer_to_double(degrees * GPS_MAS_PER_DEGREE));
  minutes = double_to_unsigned(
      double_divide_reversed_pointer((double)GPS_MAS_PER_MINUTE, &mas));
  double_subtract_in_place(&mas,
                           integer_to_double(minutes * GPS_MAS_PER_MINUTE));
  seconds = double_divide_reversed_pointer(GPS_MAS_PER_SECOND, &mas);
  sprintf(text, format, hemisphere, degrees, minutes, seconds);
}
