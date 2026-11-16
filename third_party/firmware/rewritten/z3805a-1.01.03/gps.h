/* From the 58503B 1.01.04's rewritten/gps.h, copied for the Z3805A 1.01.03:
 * what its copied functions use.  Its comments' addresses are the
 * Z3805A 1.01.03's, but where they name another image. */

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then degrees, minutes and seconds by format, which gives the
 * hemisphere and the three (format_dms_precise). */
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
