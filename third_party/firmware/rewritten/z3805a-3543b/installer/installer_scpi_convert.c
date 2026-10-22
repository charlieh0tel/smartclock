/* The installer's copies of SCPI's number conversions
 * (`applications/scpi/convert`; scpi_convert.c has the primary's),
 * whose bodies, scpi_convert.h's, both share.  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* A number at a scale as a short, rounded (installer::0x104236). */
int32_t installer_scpi_number_to_short(scpi_number* number, int16_t* value,
                                       int32_t scale) {
  return scpi_engine_number_to_short(number, value, scale);
}

/* A value's decimal digits right-aligned in digits, to its index 16,
 * and the first's index (installer::0x1045d0). */
int32_t installer_scpi_unsigned_to_digits(uint32_t value, char* digits,
                                          int16_t* start) {
  return scpi_engine_unsigned_to_digits(value, digits, start);
}

/* Its magnitude at a scale as an unsigned short, rounded
 * (installer::0x1042dc). */
int32_t installer_scpi_number_to_unsigned_short(scpi_number* number,
                                                uint16_t* value,
                                                int32_t scale) {
  return scpi_engine_number_to_unsigned_short(number, value, scale);
}

/* A number at a scale as a long, rounded (installer::0x104380). */
int32_t installer_scpi_number_to_long(scpi_number* number, int32_t* value,
                                      int32_t scale) {
  return scpi_engine_number_to_long(number, value, scale);
}

/* Its magnitude at a scale as an unsigned long, rounded
 * (installer::0x104426). */
int32_t installer_scpi_number_to_unsigned_long(scpi_number* number,
                                               uint32_t* value, int32_t scale) {
  return scpi_engine_number_to_unsigned_long(number, value, scale);
}
