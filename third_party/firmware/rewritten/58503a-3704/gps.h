/* What the GPS engine's files, the status screen and the front panel
 * share (gps_receiver.c, gps_requests.c, screen.c, front_panel.c): the
 * satellite IDs, GPS 1 to 32 and pseudolites 33 to 37 (satellite_kind),
 * the receiver's channels, an angle's milliarcseconds and its text in
 * degrees, minutes and seconds, and gps_request_post's code for a
 * step.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The most channels an engine has, 1 to 8, its own count being
 * gps.channel_count, 6 or 8; the satellite lists' limit (0x2d184). */
static const uint8_t GPS_CHANNELS = 8;

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then degrees, minutes and seconds by format, which gives the
 * hemisphere and the three: format_dms_precise's (0x48364) and the
 * front panel's (front_panel_format_dms, 0x5274e), which differ in the
 * seconds' digits. */
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
