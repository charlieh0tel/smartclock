/* The functions of gps_receiver.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_receiver.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Whether an ID is one of a kind's satellites; 0 for an unknown kind. */
int32_t satellite_id_valid(satellite_kind kind, uint8_t id) {
  uint8_t first;
  uint8_t last;

  switch (kind) {
    case SATELLITE_GPS:
      first = GPS_FIRST_SATELLITE;
      last = GPS_LAST_SATELLITE;
      break;
    case SATELLITE_PSEUDOLITE:
      first = GPS_FIRST_PSEUDOLITE;
      last = GPS_LAST_PSEUDOLITE;
      break;
    case SATELLITE_ANY:
      first = GPS_FIRST_SATELLITE;
      last = GPS_LAST_PSEUDOLITE;
      break;
    default:
      return 0;
  }
  return first <= id && id <= last;
}

/* Adds a satellite to a set: a GPS satellite as bit id - 1 of a long, a
 * pseudolite as bit id - 0x21 of a byte. */
void satellite_add_to_set(satellite_kind kind, uint8_t id, void* set) {
  if (kind == SATELLITE_GPS) {
    *(uint32_t*)set |= cpu_long_bit(id - GPS_FIRST_SATELLITE);
  } else {
    *(uint8_t*)set |= (uint8_t)cpu_long_bit(id - GPS_FIRST_PSEUDOLITE);
  }
}

/* The alternate receiver's status, which status_snapshot hands to
 * gps_receiver_status_update: none. */
uint8_t gps_status_alternate(void) { return 0; }

/* The milliarcseconds of a position's coordinate, as degrees, minutes
 * and seconds of its magnitude. */
void mas_to_dms(int32_t mas, uint16_t* degrees, uint8_t* minutes,
                double* seconds) {
  int32_t rest = labs(mas);

  *degrees = libc_divide(&rest, GPS_MAS_PER_DEGREE);
  *minutes = libc_divide(&rest, GPS_MAS_PER_MINUTE);
  *seconds =
      double_divide_reversed(GPS_MAS_PER_SECOND, integer_to_double(rest));
}

/* Degrees, minutes and seconds as milliarcseconds, the seconds'
 * magnitude. */
void dms_to_mas(int32_t* mas, uint16_t degrees, uint8_t minutes,
                double seconds) {
  const double whole = integer_to_double(minutes * GPS_MAS_PER_MINUTE +
                                         degrees * GPS_MAS_PER_DEGREE);

  *mas = double_to_integer(
      double_add(whole, double_multiply(GPS_MAS_PER_SECOND, fabs(seconds))));
}
