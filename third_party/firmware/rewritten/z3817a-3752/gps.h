/* What this image's GPS task, request filter, satellite lists and
 * status screen share (gps_oncore.c, gps_receiver.c, gps_reports.c,
 * gps_requests.c, scpi_source_gpsystem.c, screen.c): the receiver's channels,
 * the status's repeats, the GPS 1 PPS failure's bit and an angle's text in
 * degrees, minutes and seconds.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c files, so
 * every one of them can use it. */

/* The most channels an engine has, its own count being
 * gps.channel_count, 6 or 8 (oncore_filter_channels, 0x56f66); the
 * satellite lists' limit (satellite_list_limits, 0x3df5c). */
static const uint8_t GPS_CHANNELS = 8;

/* A six-channel engine's channels (oncore_filter_channels, 0x56f60). */
static const uint8_t GPS_SIX_CHANNELS = 6;

/* The repeats of the status's time oncore_position_message counts to
 * (0x56948), past which the status monitor misses (0x52014). */
static const uint8_t GPS_STATUS_MOST_REPEATS = 46;

/* The hardware condition's bit of the GPS 1 PPS failure, :HARDware bit 8,
 * which gps_receiver_status_update takes as the GPS reference's 1PPS dead
 * (0x51a60) and bits_8_9_any tests (0x50ae2). */
static const int32_t GPS_HARDWARE_PPS_BIT = 8;

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then degrees, minutes and seconds by format, which gives the
 * hemisphere and the three (format_dms_precise, 0x4e2c6). */
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
