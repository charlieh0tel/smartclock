/* The Z3805A 1.01.03's oncore_position_message, which @@Ba's and
 * @@Ea's decoders call: the 58503B 1.01.04's, from its gps_oncore.c,
 * less the time status that one keeps, with this image's bit for the
 * MSL height and its tracking log's countdown, where the decompiler
 * tested the receiver status against ~ of every bit where the image
 * tests for none (docs/firmware/z3805a-1.01.03.md, "What fails").
 * Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype.
 * With them, from the 58503B 1.01.04's C, the functions this image's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"), their comments' addresses this image's but where they
 * name another image. */

/* Longitudes in milliarcseconds: a quarter, a half and a whole turn.
 * The survey's average is kept within half a turn of 0 either way, and
 * a fix across the antimeridian from it is moved a turn before it is
 * averaged in. */
static const int32_t GPS_ONCORE_QUARTER_TURN = 324000000;

static const double GPS_ONCORE_HALF_TURN = 648000000.0;

static const int32_t GPS_ONCORE_TURN = 1296000000;

/* After @@Ba or @@Ea is decoded: while a survey runs and the fix is 3D,
 * averages the position, the longitude kept across the antimeridian
 * and the ellipsoid's height or, once the receiver ID is seen, the
 * MSL's; marks a fix, 2D or 3D, seen; qualifies the reference; with a
 * satellite in a channel available for position, counts the repeats of
 * the status' time; prints the status every gps_status_print_interval
 * messages; restarts the tracking log's countdown; and keeps the time of
 * this status (0x52446).  The MSL's bit is gps.field_592's bit 3
 * (0x52570). */
void oncore_position_message(void) {
  /* The repeats of one status time it counts to, and the tracking log's
   * countdown, gpsm_task's passes. */
  static const uint8_t MOST_REPEATS = 46;
  static const uint8_t TRACKING_LOG_PASSES = 11;

  if (gps.position_hold == 0 &&
      (nv_settings.douglas_duration_infinite == 1 ||
       gps.average_count < (uint32_t)nv_settings.douglas_duration) &&
      (gps.status.receiver_status & ONCORE_STATUS_3D_FIX) != 0) {
    int32_t longitude = gps.status.longitude;

    running_average_update(gps.status.latitude, &gps.average_latitude,
                           gps.average_count);
    if (gps.average_count != 0) {
      if (double_compare(-GPS_ONCORE_QUARTER_TURN, gps.average_longitude) < 0 &&
          longitude > GPS_ONCORE_QUARTER_TURN) {
        double_add_in_place(&gps.average_longitude, GPS_ONCORE_TURN);
      } else if (longitude < -GPS_ONCORE_QUARTER_TURN &&
                 double_compare(GPS_ONCORE_QUARTER_TURN,
                                gps.average_longitude) > 0) {
        longitude += GPS_ONCORE_TURN;
      }
    }
    running_average_update(longitude, &gps.average_longitude,
                           gps.average_count);
    if (double_compare(GPS_ONCORE_HALF_TURN, gps.average_longitude) > 0) {
      double_add_in_place(&gps.average_longitude, -GPS_ONCORE_TURN);
    } else if (double_compare(-GPS_ONCORE_HALF_TURN, gps.average_longitude) <
               0) {
      double_add_in_place(&gps.average_longitude, GPS_ONCORE_TURN);
    }
    if ((gps.field_592 & GPS_FIELD_592_HEIGHT_MSL) != 0) {
      running_average_update(gps.status.height_msl, &gps.average_height_msl,
                             gps.average_count);
    } else {
      running_average_update(gps.status.height_gps, &gps.average_height_gps,
                             gps.average_count);
      gps.average_height_msl = gps.average_height_gps;
    }
    gps.average_count++;
    gps.field_6cb = 1;
  }
  if ((gps.status.receiver_status &
       (ONCORE_STATUS_3D_FIX | ONCORE_STATUS_2D_FIX)) != 0) {
    gps.surveying = 1;
  }
  gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);

  bool usable = false;
  const oncore_channel_status* channel = gps.status.channels;

  for (uint8_t number = 1; number <= gps.channel_count; number++, channel++) {
    if ((satellite_id_valid(SATELLITE_GPS, channel->satellite) ||
         satellite_id_valid(SATELLITE_PSEUDOLITE, channel->satellite)) &&
        channel->mode == ONCORE_AVAILABLE_FOR_POSITION) {
      usable = true;
      break;
    }
  }
  if (usable) {
    if (clock_time_bits(gps.status.time) ==
        clock_time_bits(gps.status_last_time)) {
      if (gps.status_repeat_count < MOST_REPEATS) {
        gps.status_repeat_count++;
      }
    } else {
      gps.status_last_time = gps.status.time;
      gps.status_repeat_count = 0;
    }
  }
  if (gps_status_print_interval != 0 &&
      ++gps_status_print_count >= gps_status_print_interval) {
    gps_status_print_count = 0;
    gps_debug_status();
  }
  if (nv_settings.tracking_log != 0) {
    tracking_log_countdown = TRACKING_LOG_PASSES;
  }
  gps.status_monitor_time = get_uptime();
}

/* Puts @@Az's cable delay argument, in ns, at its index, the image's
 * pps_cable_delay_offset added, at the next, and encodes that as a long
 * (0x51adc).  It writes into the arguments it is given, a script step's
 * in ROM among them. */
uint8_t* oncore_cable_delay_encode(int32_t* arguments, uint8_t index,
                                   uint8_t* out) {
  long_array_set(pps_cable_delay_offset + long_array_get(arguments, index),
                 arguments, index + 1);
  return long_array_get_long(arguments, index + 1, out);
}

/* Decodes @@Ai's byte into record at its channel, 1 to gps.channel_count
 * (0x51c4c). */
uint8_t* oncore_ai_decode(uint8_t* cursor, uint8_t* record) {
  const uint8_t channel = cursor[0];

  if (channel != 0 && channel <= gps.channel_count) {
    byte_copy_optional(&cursor[1], &record[channel - 1]);
  }
  return cursor + 2;
}

/* Decodes @@Az's cable delay, keeping it as given and, the image's
 * pps_cable_delay_offset taken off, in record (0x51c9c). */
uint8_t* oncore_cable_delay_decode(uint8_t* cursor, int32_t* record) {
  uint32_t delay = 0;

  for (uint8_t i = 0; i < sizeof delay; i++) {
    delay = delay << CPU_BYTE_BITS | *cursor++;
  }
  gps.pps_cable_delay_given = delay;
  *record = delay - pps_cable_delay_offset;
  return cursor;
}

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x51c7a). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* An argument's low three bytes, high byte first (0x5197e). */
uint8_t* long_array_get_three_bytes(int32_t* array, uint8_t index,
                                    uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 16);
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)value;
  return out + 3;
}

/* 1 once more time has passed since start than the message's timeout;
 * start is restarted if the clock went back (0x4d592). */
uint32_t gps_response_timed_out(gps_request_code_word message,
                                uint32_t* start) {
  int32_t elapsed;

  return uptime_elapsed(start, 1, &elapsed) &&
         (uint32_t)elapsed > gps_request_descriptor(message)->timeout;
}

/* The text after a receiver ID line's label, for the line of a field
 * whose text starts with its label; NULL for none (0x5274e). */
char* oncore_receiver_id_value(oncore_receiver_id_field field) {
  for (const oncore_receiver_id_line* line = oncore_receiver_id_lines;
       line->index != ONCORE_ID_END; line++) {
    if (field == line->index &&
        strncmp(line->text, line->label, line->label_length) == 0) {
      return line->text + line->label_length;
    }
  }
  return NULL;
}

/* @@Ea's receiver status byte's antenna bits (oncore_antenna_sense_set). */
static const uint8_t GPS_ONCORE_ANTENNA_OVER_CURRENT = 0x40;

static const uint8_t GPS_ONCORE_ANTENNA_UNDER_CURRENT = 0x80;

/* Takes the antenna's current from @@Ea's receiver status byte into
 * gps.antenna_sense, bit 6 over, else bit 7 under, else normal; the bit
 * it found, or 0 (0x51cce). */
uint8_t oncore_antenna_sense_set(uint8_t status_high) {
  uint8_t bit = status_high & GPS_ONCORE_ANTENNA_OVER_CURRENT;

  if (bit != 0) {
    gps.antenna_sense = GPS_ANTENNA_OVER_CURRENT;
    return bit;
  }
  bit = status_high & GPS_ONCORE_ANTENNA_UNDER_CURRENT;
  gps.antenna_sense = bit != 0 ? GPS_ANTENNA_UNDER_CURRENT : GPS_ANTENNA_NORMAL;
  return bit;
}

/* Decodes @@Sz, the power-on failure message, which has no data: marks it
 * received in record, gps.sz_received, which nothing reads (0x5272a). */
uint8_t* oncore_power_on_failure_message(uint8_t* cursor, uint8_t* record) {
  *record = 1;
  return cursor;
}

/* The first argument's high word, high byte first, whatever the index:
 * the image passes long_array_get none (0x519e0). */
uint8_t* long_array_get_first_high_word(int32_t* array, uint8_t index,
                                        uint8_t* out) {
  uint16_t word = (uint16_t)((uint32_t)long_array_get(array, 0) >> 16);

  out[0] = (uint8_t)(word >> CPU_BYTE_BITS);
  out[1] = (uint8_t)word;
  return out + 2;
}

/* As many bytes as the first argument's low byte gives, at most
 * MOST_BYTES, from the arguments after it, each long high byte first;
 * the index unread (0x51a0c). */
uint8_t* long_array_get_bytes_counted(int32_t* array, uint8_t index,
                                      uint8_t* out) {
  static const uint8_t MOST_BYTES = 12;
  static const uint8_t LONG_BYTES = sizeof(int32_t);
  uint8_t count = (uint8_t)long_array_get(array, 0);
  uint8_t source = 0;

  if (count >= MOST_BYTES) {
    count = MOST_BYTES;
  }
  for (uint8_t i = 1; i <= count; i++) {
    const uint8_t byte = LONG_BYTES - 1 - (i - 1) % LONG_BYTES;

    if (byte == LONG_BYTES - 1) {
      source++;
    }
    *out++ = (uint8_t)((uint32_t)long_array_get(array, source) >>
                       (byte * CPU_BYTE_BITS));
  }
  return out;
}

/* Decodes a message's one byte into record's first, and copies it to
 * the byte 0x12 on, which nothing here names: nothing calls it
 * (0x51b84). */
uint8_t* oncore_leap_pending_decode(uint8_t* cursor, uint8_t* record) {
  /* where the record keeps the byte's copy */
  static const uint8_t COPY_OFFSET = 0x12;

  if (record != NULL) {
    record[0] = cursor[0];
    record[COPY_OFFSET] = record[0];
  }
  return cursor + 1;
}

/* Copies a message's byte to its place in the record, if it has one,
 * and keeps whether it is nonzero in gps.field_6e2 (0x51da4).  Nothing
 * here calls it. */
uint8_t* oncore_utc_offset_message(uint8_t* cursor, uint8_t* record) {
  cursor = byte_copy_optional(cursor, record);
  if (record != NULL) {
    gps.field_6e2 = *record != 0;
  }
  return cursor;
}

/* The tracked satellites, listed for as many as the engine has channels;
 * false before a status has come (0x5214c). */
bool satellites_used_list(uint8_t* count, satellite_list_entry* list) {
  /* satellite_status_list's list of the tracked. */
  static const uint8_t TRACKED = 1;
  satellite_sets sets;

  if (gps.status_received == GPS_STATUS_NONE) {
    return false;
  }
  satellite_sets_build(gps.status.channels, &sets);
  satellite_status_list(TRACKED, gps.channel_count, &sets, gps.status.channels,
                        (gps_visibility_record*)&gps.visibility_received, list,
                        count);
  return true;
}

/* The Oncore receiver ID's commit for one model: the GT-77's value in
 * field_592's model bits and the channel count for it, the height's bit
 * clear and the time RAIM's set, field_715's bit 7 set and bit 6 clear,
 * tps's date flag clear and the fix mode a position fix (0x51ba2).
 * Nothing here calls it. */
void oncore_receiver_r_setup(void) {
  const gps_field_592_bit models =
      GPS_FIELD_592_MODEL_GT77 | GPS_FIELD_592_MODEL_GT80;

  gps.field_592 = (gps.field_592 & ~models) | GPS_FIELD_592_MODEL_GT77;
  gps_channel_count_from_model();
  gps.field_592 &= ~GPS_FIELD_592_HEIGHT_MSL;
  gps.field_715 |= GPS_FIELD_715_BIT_7;
  gps.field_592 |= GPS_FIELD_592_TIME_RAIM;
  gps.field_715 &= ~GPS_FIELD_715_BIT_6;
  gps.field_6e2 = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
}

/* An engine's self-test response as the Oncore's generic bits
 * (0x51cfe).  With field_592's bit 1 it keeps GENERIC_BITS and takes the
 * antenna's current from the high byte.  Else with bit 0, the antenna
 * normal, it moves a six-channel engine's bits above the channels' six
 * up two, the model bits both clear, then folds the channels' bits 9
 * and 10 into bit 9 and bits 11 and 12 into bit 10, and moves bits 13
 * and 15 down two.  Else the response as it came, the antenna normal. */
uint16_t oncore_selftest_generic(uint16_t response) {
  static const uint16_t GENERIC_BITS = 0x37ff;
  static const uint16_t SIX_CHANNELS = 0x003f;
  static const uint8_t EIGHT_CHANNEL_SHIFT = 2;
  static const uint16_t CHANNELS_9_10 = 0x0600;
  static const uint16_t CHANNELS_11_12 = 0x1800;
  static const uint16_t FOLDED_9_10 = 0x0200;
  static const uint16_t FOLDED_11_12 = 0x0400;
  static const uint16_t MOVED_BITS = 0xa000;
  static const uint16_t KEPT_BITS = 0x41ff;
  static const uint8_t BYTE_BITS = 8;

  if ((gps.field_592 & GPS_FIELD_592_BIT_1) != 0) {
    oncore_antenna_sense_set((uint8_t)(response >> BYTE_BITS));
    return response & GENERIC_BITS;
  }
  if ((gps.field_592 & GPS_FIELD_592_BIT_0) == 0) {
    gps.antenna_sense = GPS_ANTENNA_NORMAL;
    return response;
  }
  const uint16_t eight =
      (gps.field_592 & (GPS_FIELD_592_MODEL_GT77 | GPS_FIELD_592_MODEL_GT80)) ==
              0
          ? (uint16_t)((response & SIX_CHANNELS) |
                       ((response & (uint16_t)~SIX_CHANNELS)
                        << EIGHT_CHANNEL_SHIFT))
          : response;
  const uint16_t generic =
      (uint16_t)((eight & KEPT_BITS) |
                 ((eight & MOVED_BITS) >> EIGHT_CHANNEL_SHIFT) |
                 ((eight & CHANNELS_11_12) != 0 ? FOLDED_11_12 : 0) |
                 ((eight & CHANNELS_9_10) != 0 ? FOLDED_9_10 : 0));

  gps.antenna_sense = GPS_ANTENNA_NORMAL;
  return generic;
}

/* A send filter of the Oncore's carried code: 1 while gps.field_592's
 * bit 5 is set (0x527ec), where the 58503B 1.01.04's oncore_filter_signal_cn
 * tests its field_608 for an "R" receiver. */
uint8_t oncore_filter_field_592_bit_5(void) {
  return (gps.field_592 & GPS_FIELD_592_BIT_5) != 0;
}

/* A send filter of the Oncore's carried code: 1 while gps.field_715's
 * bit 6 is set (0x527fc), where the 58503B 1.01.04's
 * oncore_filter_field_609_bit_6, @@Gj's, tests its field_609's. */
uint8_t oncore_filter_field_715_bit_6(void) {
  return (gps.field_715 & GPS_FIELD_715_BIT_6) != 0;
}

/* The Oncore receiver ID's commit's settings for a "B" receiver, carried:
 * the MSL height's bit and bit 5 set, field_715's bits 6 and 5 clear,
 * field_6e2 set, the position fix mode and the antenna normal
 * (0x51be2).  Nothing calls it. */
void oncore_receiver_b_setup(void) {
  gps.field_592 |= GPS_FIELD_592_HEIGHT_MSL;
  gps.field_592 |= GPS_FIELD_592_BIT_5;
  gps.field_715 &= ~GPS_FIELD_715_BIT_6;
  gps.field_715 &= ~GPS_FIELD_715_BIT_5;
  gps.field_6e2 = 1;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
}

/* The Oncore receiver ID's commit's settings for a receiver neither "B"
 * nor "R", carried: the bits the other two set cleared, the position fix
 * mode and the antenna normal (0x51c12).  Nothing calls it. */
void oncore_receiver_other_setup(void) {
  gps.field_592 &= ~GPS_FIELD_592_HEIGHT_MSL;
  gps.field_592 &= ~GPS_FIELD_592_BIT_5;
  gps.field_715 &= ~GPS_FIELD_715_BIT_7;
  gps.field_592 &= ~GPS_FIELD_592_TIME_RAIM;
  gps.field_715 &= ~GPS_FIELD_715_BIT_6;
  gps.field_715 &= ~GPS_FIELD_715_BIT_5;
  gps.field_6e2 = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
}

/* @@Aq's encoder, carried: the argument's low byte, the atmospheric
 * correction mode, its bit 1, the tropospheric model, cleared unless
 * gps.field_715's bit 7 is set (0x51aa6). */
uint8_t* oncore_atmosphere_encode(int32_t* arguments, uint8_t index,
                                  uint8_t* out) {
  static const uint32_t TROPOSPHERE = 0x02;
  uint32_t mode = (uint32_t)long_array_get(arguments, index);

  if ((gps.field_715 & GPS_FIELD_715_BIT_7) == 0) {
    mode &= ~TROPOSPHERE;
  }
  *out++ = (uint8_t)mode;
  return out;
}
