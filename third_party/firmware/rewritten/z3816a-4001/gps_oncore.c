/* The Z3816A 4001's own of the GPS task's side of the Oncore's binary
 * messages: what follows @@Ba and @@Ea, which surveys, wraps the
 * longitude's average and keeps the time's status, and the response
 * decoder, which finds the message itself in the framing buffer it is
 * given (docs/firmware/z3816a-4001.md, "The function check").  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype.
 * With them, from the 58503A 3704's rewritten/gps_oncore.c, of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static
 * helpers they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Their comments'
 * addresses are the Z3816A's. */

/* An angle in mas: 90 degrees, a half and a whole turn. */
static const int32_t GPS_ONCORE_QUARTER_TURN = 324000000;
static const double GPS_ONCORE_HALF_TURN = 648000000.0;
static const double GPS_ONCORE_TURN = 1296000000.0;

/* Runs an Oncore decoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte is in D0
 * alone, where gcc takes a pointer result from A0, so it is called as
 * returning a long. */
static uint8_t* gps_oncore_decode(const oncore_decoder* decoder,
                                  uint8_t* cursor) {
  typedef uint32_t decode_in_d0(uint8_t* cursor, uint8_t* record);

  return (uint8_t*)((decode_in_d0*)decoder->decode)(cursor, decoder->record);
}

/* Folds a longitude into the survey's average, which it first takes a
 * turn either way when the two straddle 180 degrees, and keeps the
 * average within a half turn of 0 (0x555f2 to 0x556b8). */
static void gps_oncore_average_longitude(void) {
  int32_t longitude = gps.status.longitude;

  if (gps.average_count != 0) {
    /* double_compare(a, b) is negative when b < a. */
    if (double_compare(-(double)GPS_ONCORE_QUARTER_TURN,
                       gps.average_longitude) < 0 &&
        longitude > GPS_ONCORE_QUARTER_TURN) {
      double_add_in_place(&gps.average_longitude, GPS_ONCORE_TURN);
    } else if (longitude < -GPS_ONCORE_QUARTER_TURN &&
               double_compare((double)GPS_ONCORE_QUARTER_TURN,
                              gps.average_longitude) > 0) {
      longitude += (int32_t)GPS_ONCORE_TURN;
    }
  }
  running_average_update(longitude, &gps.average_longitude, gps.average_count);
  if (double_compare(GPS_ONCORE_HALF_TURN, gps.average_longitude) > 0) {
    double_add_in_place(&gps.average_longitude, -GPS_ONCORE_TURN);
  } else if (double_compare(-GPS_ONCORE_HALF_TURN, gps.average_longitude) < 0) {
    double_add_in_place(&gps.average_longitude, GPS_ONCORE_TURN);
  }
}

/* Whether a channel of @@Ba's tracks a satellite, a GPS one or a
 * pseudolite, available for position (0x55728 to 0x55750). */
static bool gps_oncore_channel_usable(const oncore_channel_status* channel) {
  return ((uint8_t)satellite_id_valid(SATELLITE_GPS, channel->satellite) != 0 ||
          (uint8_t)satellite_id_valid(SATELLITE_PSEUDOLITE,
                                      channel->satellite) != 0) &&
         channel->mode == ONCORE_AVAILABLE_FOR_POSITION;
}

/* After @@Ba or @@Ea is decoded: while a survey runs and the fix is 3D,
 * averages the position, its height MSL or GPS by gps.engine;
 * marks any fix; qualifies the reference; counts a repeated time while
 * a channel is usable; keeps the time's status; prints the status every
 * gps_status_print_interval messages; restarts the tracking log's
 * countdown; and stamps the position (0x55594). */
void oncore_position_message(void) {
  /* The tracking log's countdown each @@Ba restarts. */
  static const uint8_t TRACKING_COUNTDOWN = 61;
  static const uint8_t MOST_REPEATS = 46;
  bool usable = false;

  if (gps.position_hold == 0 &&
      (nv_settings.douglas_duration_infinite == 1 ||
       gps.average_count < (uint32_t)nv_settings.douglas_duration) &&
      (gps.status.receiver_status & ONCORE_STATUS_3D_FIX) != 0) {
    running_average_update(gps.status.latitude, &gps.average_latitude,
                           gps.average_count);
    gps_oncore_average_longitude();
    if ((gps.engine & GPS_ENGINE_HEIGHT_MSL) != 0) {
      running_average_update(gps.status.height_msl, &gps.average_height_msl,
                             gps.average_count);
    } else {
      running_average_update(gps.status.height_gps, &gps.average_height_gps,
                             gps.average_count);
      gps.average_height_msl = gps.average_height_gps;
    }
    gps.average_count++;
    gps.surveying = 1;
  }
  if ((gps.status.receiver_status &
       (ONCORE_STATUS_3D_FIX | ONCORE_STATUS_2D_FIX)) != 0) {
    gps.fix_seen = 1;
  }
  gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    if (gps_oncore_channel_usable(&gps.status.channels[channel])) {
      usable = true;
      break;
    }
  }
  if (usable) {
    const uint32_t time = clock_time_bits(gps.status.time);

    if (time != gps.status_last_time) {
      gps.status_last_time = time;
      gps.status_repeat_count = 0;
    } else if (gps.status_repeat_count < MOST_REPEATS) {
      gps.status_repeat_count++;
    }
  }
  if (gps.utc_time == ONCORE_TIME_UTC && gps.utc_offset_received != 0) {
    gps.time_status = GPS_TIME_STATUS_UTC;
  } else if (gps.time_status == GPS_TIME_STATUS_UNTRACKED &&
             gps.utc_offset_received == 0 && !usable) {
    gps.time_status = GPS_TIME_STATUS_UNTRACKED;
  } else {
    gps.time_status = GPS_TIME_STATUS_GPS;
  }
  if (gps_status_print_interval != 0 &&
      ++gps_status_print_count >= gps_status_print_interval) {
    gps_status_print_count = 0;
    gps_debug_status();
  }
  if (nv_settings.tracking_log != 0) {
    gps_tracking_countdown = TRACKING_COUNTDOWN;
  }
  gps.status_message_time = get_uptime();
}

/* Runs a response's decoders over the framing buffer it is given, past
 * its "@@" and ID, with preemption off, its message found by its ID;
 * the cursor, the pointer after the buffer's bytes, gps_response_cursor
 * for gps_response_buffer, past what they read.  0, counted as an
 * unknown response, for an ID not in oncore_messages; else 1 (0x562bc). */
uint32_t gps_response_decode(uint8_t* response) {
  static const uint8_t DECODERS = sizeof oncore_messages[0].decoders /
                                  sizeof oncore_messages[0].decoders[0];
  uint8_t** cursor = (uint8_t**)(response + sizeof gps_response_buffer);
  int16_t length;
  gps_request_code_word message;

  *cursor = response;
  *cursor += (uint8_t)libc_length("@@");
  if ((uint8_t)gps_response_lookup((*cursor)[0], (*cursor)[1], &length,
                                   &message) == 0) {
    gps_counts.unknown_response++;
    return 0;
  }
  const oncore_message_entry* entry = oncore_message_entry_at(message);

  psos_mode(PSOS_MODE_NO_PREEMPTION, PSOS_MODE_NO_PREEMPTION, NULL);
  *cursor += libc_length(entry->id);
  for (uint8_t i = 0; i < DECODERS && entry->decoders[i].decode != NULL; i++) {
    *cursor = gps_oncore_decode(&entry->decoders[i], *cursor);
  }
  psos_mode(0, PSOS_MODE_NO_PREEMPTION, NULL);
  return 1;
}

/* The entry of oncore_messages for a message, by its index (0x560fc). */
oncore_message_entry* oncore_message_entry_at(int16_t index) {
  return &oncore_messages[index];
}

/* Puts @@Az's cable delay argument, in ns, at its index, the image's
 * pps_cable_delay_offset added, at the next, and encodes that as a long
 * (0x54b24).  It writes into the arguments it is given, a script step's
 * in ROM among them. */
uint8_t* oncore_cable_delay_encode(int32_t* arguments, uint8_t index,
                                   uint8_t* out) {
  long_array_set(pps_cable_delay_offset + long_array_get(arguments, index),
                 arguments, index + 1);
  return long_array_get_long(arguments, index + 1, out);
}

/* Decodes @@Ai's byte into record at its channel, 1 to gps.channel_count
 * (0x54c8e). */
uint8_t* oncore_ai_decode(uint8_t* cursor, uint8_t* record) {
  const uint8_t channel = cursor[0];

  if (channel != 0 && channel <= gps.channel_count) {
    byte_copy_optional(&cursor[1], &record[channel - 1]);
  }
  return cursor + 2;
}

/* Decodes @@Az's cable delay, keeping it as given and, the image's
 * pps_cable_delay_offset taken off, in record (0x54cde). */
uint8_t* oncore_cable_delay_decode(uint8_t* cursor, int32_t* record) {
  uint32_t delay = 0;

  for (uint8_t i = 0; i < sizeof delay; i++) {
    delay = delay << CPU_BYTE_BITS | *cursor++;
  }
  gps.pps_cable_delay_given = delay;
  *record = delay - pps_cable_delay_offset;
  return cursor;
}

/* Decodes @@Ba, the six-channel position and status (0x558ae). */
uint8_t* oncore_position_status_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_field_codes);
  oncore_position_message();
  return cursor;
}

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x54cbc). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* Decodes @@Ce, the pseudorange corrections, and keeps the mean and
 * standard deviation, in meters, of those of the satellites used for
 * position (0x55a12). */
uint8_t* oncore_corrections_message(uint8_t* cursor, uint8_t* record) {
  static const uint32_t CHANNELS =
      sizeof gps.corrections.channels / sizeof gps.corrections.channels[0];
  static const double CENTIMETERS_TO_METERS = 0.01;
  static const double SQUARE = 2.0;
  double sum = 0.0;
  double sum_of_squares = 0.0;
  uint8_t used = 0;
  uint8_t* next =
      oncore_decode_fields(cursor, record, oncore_corrections_field_codes);

  for (uint32_t i = 0; i < CHANNELS; i++) {
    const oncore_correction_channel* channel = &gps.corrections.channels[i];

    if (satellite_used_for_position(channel->satellite) != 0) {
      double_add_in_place(&sum, integer_to_double(channel->range_correction));
      double_add_in_place(
          &sum_of_squares,
          pow(integer_to_double(channel->range_correction), SQUARE));
      used++;
    }
  }
  if (used == 0) {
    gps.correction_deviation = 0.0;
    gps.correction_average = 0.0;
  } else {
    double mean_square;
    double variance;

    gps.correction_average = double_divide_reversed(
        unsigned_to_double(used),
        double_multiply_pointer(CENTIMETERS_TO_METERS, &sum));
    mean_square =
        double_divide_reversed(unsigned_to_double(used), pow(sum, SQUARE));
    variance = double_divide_reversed(
        unsigned_to_double(used),
        double_subtract_reversed_pointer(mean_square, &sum_of_squares));
    gps.correction_deviation =
        double_multiply(sqrt(variance), CENTIMETERS_TO_METERS);
  }
  return next;
}

/* Decodes @@Bk, the position, status and data extension (0x55b7c).  Its
 * program, 26 84 84 0e 80, stops after matrix element 7: @@Bk's last
 * three elements, clock bias and oscillator offset are never stored;
 * only @@Ek's program, 40 80, stores them (docs/firmware/gps.md, "The
 * extension messages"). */
uint8_t* oncore_extension_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_extension_field_codes);
}

/* Decodes @@Ek, the eight-channel extension (0x55b96). */
uint8_t* oncore_extension_8_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_extension_8_field_codes);
}

/* A message as text for the command log: its "@@" and ID, each byte
 * after them in hex, and "<cr><lf>"; empty unless it ends in CR LF.  Its
 * length. */
int32_t gps_message_to_text(uint8_t* message, uint8_t length, char* text) {
  const uint32_t header_length = libc_length("@@") + GPS_ONCORE_ID_LENGTH;
  const uint8_t* end = message + (length - 2);

  if (message[0] == 0 || end[0] != '\r' || end[1] != '\n') {
    text[0] = 0;
  } else {
    char header[5];
    char digits[5];
    char hex[54];

    memcpy(header, message, header_length);
    header[header_length] = 0;
    hex[0] = 0;
    for (const uint8_t* byte = message + header_length; byte < end; byte++) {
      char* shown = digits;

      sprintf(digits, "%hx", (uint32_t)*byte);
      if (libc_length(digits) < 2) {
        digits[2] = 0;
        digits[1] = digits[0];
        digits[0] = '0';
      } else {
        /* Never more than two digits for a byte, so never trimmed. */
        while (libc_length(shown) >= 3 && (*shown == '0' || *shown == 'f')) {
          shown++;
        }
      }
      strcat(hex, shown);
    }
    sprintf(text, "%s%s<cr><lf>", header, hex);
  }
  return libc_length(text);
}
/* Points gps_response_cursor back at gps_response_buffer's start and
 * expects a message's first @. */
void gps_response_reset(void) {
  gps_response_cursor = gps_response_buffer;
  gps_receive_state = GPS_FRAMING_FIRST_AT;
}

/* Marks each visible GPS satellite of @@Bb's record removed in its health,
 * bit 0, while gps.ignored_satellites holds it, else not, once @@Bb has
 * come; received is gps.visibility_received, the record 2 bytes on
 * (0x55912). */
void oncore_visibility_removed_mark(uint8_t* received) {
  oncore_visibility_fields* visibility =
      (oncore_visibility_fields*)(received +
                                  (offsetof(gps_state, visibility) -
                                   offsetof(gps_state, visibility_received)));
  uint32_t ignored;
  oncore_visible_satellite* satellite;
  oncore_visible_satellite* end;

  if (gps.visibility_received == 0) {
    return;
  }
  ignored = gps.ignored_satellites;
  satellite = visibility->satellites;
  end = satellite + visibility->count;
  for (; satellite < end; satellite++) {
    if ((uint8_t)satellite_id_valid(SATELLITE_GPS, satellite->satellite) != 0) {
      uint8_t bit = satellite->satellite - 1;

      satellite->health = (satellite->health & ~ONCORE_HEALTHY_REMOVED) |
                          ((ignored & (1UL << bit)) != 0);
    }
  }
}

/* Decodes @@Bb, the visible satellites, and marks them received; a count
 * past the list's entries is clamped to them, as the Z3805A's is not; with
 * GPS_ENGINE_UT_PLUS and GPS_ENGINE_IGNORED_SATELLITES, marks the ignored
 * satellites removed, which the 58503A's does not (0x559bc). */
uint8_t* oncore_visibility_message(uint8_t* cursor, uint8_t* record) {
  static const uint8_t ENTRIES =
      sizeof gps.visibility.satellites / sizeof gps.visibility.satellites[0];

  cursor = oncore_decode_fields(cursor, record, oncore_visibility_field_codes);
  gps.visibility_received = 1;
  if (gps.visibility.count > ENTRIES) {
    gps.visibility.count = ENTRIES;
  }
  if ((gps.engine & GPS_ENGINE_UT_PLUS) != 0 &&
      (gps.engine & GPS_ENGINE_IGNORED_SATELLITES) != 0) {
    oncore_visibility_removed_mark(&gps.visibility_received);
  }
  return cursor;
}

/* An argument's low three bytes, high first (0x549ca); nothing calls
 * it. */
uint8_t* long_array_get_three(int32_t* arguments, uint8_t index, uint8_t* out) {
  const uint32_t value = (uint32_t)long_array_get(arguments, index);

  out[0] = (uint8_t)(value >> 16);
  out[1] = (uint8_t)(value >> CPU_BYTE_BITS);
  out[2] = (uint8_t)value;
  return out + 3;
}

/* The raw message's first encoder (oncore_messages' entry 0x3f, whose ID
 * is empty): argument 0's high word, the message's two ID letters, high
 * byte first, whatever the index (0x54a2c). */
uint8_t* oncore_raw_id_encode(int32_t* arguments, uint8_t index, uint8_t* out) {
  const uint16_t id = (uint16_t)((uint32_t)long_array_get(arguments, 0) >> 16);

  out[0] = (uint8_t)(id >> CPU_BYTE_BITS);
  out[1] = (uint8_t)id;
  return out + 2;
}

/* The raw message's second encoder: as many bytes as argument 0's low
 * byte says, at most 12, from argument 1 on, each long high byte first
 * (0x54a58). */
uint8_t* oncore_raw_bytes_encode(int32_t* arguments, uint8_t index,
                                 uint8_t* out) {
  static const uint8_t MOST = 12;
  static const uint8_t LAST_BYTE = sizeof(int32_t) - 1;
  const uint8_t asked = (uint8_t)long_array_get(arguments, 0);
  const uint8_t count = asked < MOST ? asked : MOST;
  uint8_t argument = 0;

  for (uint8_t i = 1; i <= count; i++) {
    const uint8_t shift = LAST_BYTE - (i - 1) % sizeof(int32_t);

    if (shift == LAST_BYTE) {
      argument++;
    }
    *out++ = (uint8_t)((uint32_t)long_array_get(arguments, argument) >>
                       shift * CPU_BYTE_BITS);
  }
  return out;
}

/* @@Aq's encoder: its mode's byte, bit 1 cleared, so that only modes 0
 * and 1 go, unless the engine takes the others (0x54af2). */
uint8_t* oncore_aq_encode(int32_t* arguments, uint8_t index, uint8_t* out) {
  static const uint8_t MODE_BIT_1 = 0x02;
  uint8_t mode = (uint8_t)long_array_get(arguments, index);

  if ((gps.engine & GPS_ENGINE_AQ_HIGH_MODES) == 0) {
    mode &= ~MODE_BIT_1;
  }
  *out = mode;
  return out + 1;
}

/* Decodes @@Bj, the leap second pending status, into its record,
 * gps.leap_second_reported, and copies it to gps.leap_second_pending,
 * which it reaches from the record (0x54bcc). */
uint8_t* oncore_leap_pending_message(uint8_t* cursor, uint8_t* record) {
  static const uint32_t PENDING = offsetof(gps_state, leap_second_pending) -
                                  offsetof(gps_state, leap_second_reported);

  if (record != NULL) {
    *record = *cursor;
    record[PENDING] = *record;
  }
  return cursor + 1;
}

/* gps.engine and the task's state for a UT Plus: eight channels, @@Aq's
 * other modes and Time RAIM, its heights GPS's; @@Bo awaited
 * (0x54bea). */
void gps_engine_ut_plus_set(void) {
  gps.engine |= GPS_ENGINE_EIGHT_CHANNELS;
  gps_channel_count_update();
  gps.engine &= ~GPS_ENGINE_HEIGHT_MSL;
  gps.engine |= GPS_ENGINE_AQ_HIGH_MODES | GPS_ENGINE_TRAIM;
  gps.engine &= ~GPS_ENGINE_SOFTWARE_8_4;
  gps.utc_offset_received = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
}

/* gps.engine and the task's state for a VP: MSL heights and @@Am; its UTC
 * offset taken as come, @@Bo not being sent to a VP (oncore_filter_utc_
 * offset) (0x54c24). */
void gps_engine_vp_set(void) {
  gps.engine |= GPS_ENGINE_HEIGHT_MSL | GPS_ENGINE_IGNORED_SATELLITES;
  gps.engine &= ~(GPS_ENGINE_SOFTWARE_8_4 | GPS_ENGINE_LEAP_STATUS);
  gps.utc_offset_received = 1;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_SENSE_OK;
}

/* gps.engine and the task's state for an engine neither a VP nor a UT
 * Plus: none of the abilities either sets (0x54c54). */
void gps_engine_unknown_set(void) {
  gps.engine &= ~(GPS_ENGINE_HEIGHT_MSL | GPS_ENGINE_IGNORED_SATELLITES |
                  GPS_ENGINE_AQ_HIGH_MODES | GPS_ENGINE_TRAIM |
                  GPS_ENGINE_SOFTWARE_8_4 | GPS_ENGINE_LEAP_STATUS);
  gps.utc_offset_received = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_SENSE_OK;
}

/* gps.antenna_sense from a byte of @@Ea or of a UT Plus's self-test word
 * (0x54d10). */
void gps_antenna_sense_set(uint8_t sense) {
  static const uint8_t OVERCURRENT = 0x40;
  static const uint8_t UNDERCURRENT = 0x80;

  if ((sense & OVERCURRENT) != 0) {
    gps.antenna_sense = GPS_ANTENNA_SENSE_OVERCURRENT;
  } else if ((sense & UNDERCURRENT) != 0) {
    gps.antenna_sense = GPS_ANTENNA_SENSE_UNDERCURRENT;
  } else {
    gps.antenna_sense = GPS_ANTENNA_SENSE_OK;
  }
}

/* The generic self-test word of an engine's response: a UT Plus's less
 * its bits 11, 14 and 15, its high byte passed on as the antenna's sense;
 * a VP's six-channel word laid out as its eight-channel one, then that
 * as the generic word, the RTC and the EEPROM two bits lower, the RAM's
 * two bytes bit 10 and the ROM's bit 9 (VPCommands.pdf, 6 and 8 channel
 * self-test); any other engine's as it is.  Any but a UT Plus clears the
 * antenna's sense (0x54d40). */
uint16_t oncore_selftest_generic(uint16_t response) {
  static const uint16_t UT_PLUS_TESTS = 0x37ff;
  static const uint16_t SIX_CHANNEL_LOW = 0x003f;
  static const uint8_t SIX_CHANNEL_SHIFT = 2;
  /* The eight-channel word's channels, 1 kHz and DCXO SPI bits, kept. */
  static const uint16_t VP_KEPT = 0x41ff;
  static const uint16_t VP_RTC_AND_EEPROM = 0xa000;
  static const uint8_t VP_RTC_AND_EEPROM_SHIFT = 2;
  static const uint16_t VP_RAM = 0x1800;
  static const uint16_t VP_ROM = 0x0600;
  static const uint16_t GENERIC_RAM = 0x0400;
  static const uint16_t GENERIC_ROM = 0x0200;

  if ((gps.engine & GPS_ENGINE_UT_PLUS) != 0) {
    const uint16_t generic = response & UT_PLUS_TESTS;

    gps_antenna_sense_set((uint8_t)(response >> CPU_BYTE_BITS));
    return generic;
  }
  uint16_t generic = response;

  if ((gps.engine & GPS_ENGINE_VP) != 0) {
    uint16_t word = response;

    if ((gps.engine & GPS_ENGINE_EIGHT_CHANNELS) == 0) {
      word = (uint16_t)((word & ~SIX_CHANNEL_LOW) << SIX_CHANNEL_SHIFT |
                        (word & SIX_CHANNEL_LOW));
    }
    generic = (uint16_t)((word & VP_KEPT) |
                         (word & VP_RTC_AND_EEPROM) >> VP_RTC_AND_EEPROM_SHIFT |
                         ((word & VP_RAM) != 0 ? GENERIC_RAM : 0) |
                         ((word & VP_ROM) != 0 ? GENERIC_ROM : 0));
  }
  gps.antenna_sense = GPS_ANTENNA_SENSE_OK;
  return generic;
}

/* Decodes @@Ca or @@Fa, the self-test: the engine's word into its record,
 * and gps.self_test the generic word less the tests that passed at the
 * last response, printed with the engine's kind; the tests failed kept
 * for the next (0x54de4). */
uint8_t* oncore_selftest_message(uint8_t* cursor, uint16_t* record) {
  char line[80];
  uint16_t response = *cursor++;

  response = (uint16_t)(response << CPU_BYTE_BITS | *cursor++);
  if (record == NULL) {
    return cursor;
  }
  *record = response;
  const uint16_t generic = oncore_selftest_generic(response);

  gps.self_test = generic & gps.self_test_mask;
  const char* kind = (gps.engine & GPS_ENGINE_UT_PLUS) != 0 ? "UT"
                     : (gps.engine & GPS_ENGINE_VP) != 0    ? "VP"
                                                            : "??";

  debug_sprintf(line, "%s test: resp %04X -> generic %04X & %04X = %04X\r\n",
                kind, response, generic, gps.self_test_mask, gps.self_test);
  task_dmessage(line);
  gps.self_test_mask = gps.self_test;
  return cursor;
}

/* Decodes @@Bo, the UTC offset status, into its record, gps.utc_offset,
 * and keeps whether it is nonzero (0x54e98). */
uint8_t* oncore_utc_offset_message(uint8_t* cursor, int8_t* record) {
  uint8_t* next = byte_copy_optional(cursor, (uint8_t*)record);

  if (record != NULL) {
    gps.utc_offset_received = *record != 0;
  }
  return next;
}

/* Decodes @@Sz, the power-on failure: its record set (0x55e76). */
uint8_t* oncore_power_on_failure_message(uint8_t* cursor, uint8_t* record) {
  *record = 1;
  return cursor;
}

/* Decodes @@Bn, the Time RAIM setup and status: each channel's @@Ba or
 * @@Ea mode beside its satellite, the alarm limit times 10, gps.engine's
 * 1PPS bit by its mode; stamps it and marks it come (0x55bb0). */
static uint8_t* gps_oncore_traim(uint8_t* cursor, uint8_t* record,
                                 const uint8_t* codes) {
  static const int32_t ALARM_LIMIT_SCALE = 10;
  oncore_raim_fields* raim = (oncore_raim_fields*)record;

  cursor = oncore_decode_fields(cursor, record, codes);
  for (uint8_t channel = 0; channel < gps.channel_count; channel++) {
    raim->channels[channel].mode = gps.status.channels[channel].mode;
  }
  raim->alarm_limit *= ALARM_LIMIT_SCALE;
  if (gps.raim.pps_mode == ONCORE_PPS_ON) {
    gps.engine |= GPS_ENGINE_PPS_ON;
  } else {
    gps.engine &= ~GPS_ENGINE_PPS_ON;
  }
  gps.raim_message_time = get_uptime();
  gps.raim_received = 1;
  return cursor;
}

/* Decodes @@Bn (0x55bb0). */
uint8_t* oncore_traim_message(uint8_t* cursor, uint8_t* record) {
  return gps_oncore_traim(cursor, record, oncore_raim_field_codes);
}

/* Decodes @@En, the eight-channel @@Bn (0x55c40). */
uint8_t* oncore_traim_8_message(uint8_t* cursor, uint8_t* record) {
  return gps_oncore_traim(cursor, record, oncore_raim_8_field_codes);
}

/* Decodes @@Ap, the user datum (0x55cd0). */
uint8_t* oncore_user_datum_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_user_datum_field_codes);
}

/* Decodes @@Gj, the leap second pending status (0x55e5c). */
uint8_t* oncore_leap_second_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_leap_status_field_codes);
}

/* Decodes @@Ea, the eight-channel @@Ba: the engine has eight channels,
 * then as @@Ba, and the antenna's sense from its byte at +0x23
 * (0x558d4). */
uint8_t* oncore_position_status_8_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_8_field_codes);
  gps.engine |= GPS_ENGINE_EIGHT_CHANNELS;
  gps_channel_count_update();
  oncore_position_message();
  /* the byte the status keeps as its DOP type, whose bits 6 and 7 the
   * antenna's sense is read from */
  gps_antenna_sense_set((uint8_t)gps.status.dop_type);
  return cursor;
}

/* Decodes @@Cj, the receiver ID, and learns the engine from it: its
 * model's first character, after "MODEL #    ", B for a VP and R for a
 * UT Plus, and its software, version times 10 plus revision (0x55cea). */
uint8_t* oncore_receiver_id_message(uint8_t* cursor, uint8_t* record) {
  static const char MODEL_LABEL[] = "MODEL #    ";
  static const uint8_t MODEL_LABEL_LENGTH = sizeof MODEL_LABEL - 1;
  static const int16_t VERSION_SCALE = 10;
  static const uint16_t VP_SOFTWARE_AQ = 100;
  static const int16_t VP_SOFTWARE_8_4 = 84;
  static const uint16_t UT_PLUS_SOFTWARE_3_0 = 30;
  static const char VP = 'B';
  static const char UT_PLUS = 'R';

  cursor = oncore_decode_fields(cursor, record, oncore_receiver_id_field_codes);
  gps.receiver_id_received = 1;
  const int16_t software =
      (int16_t)(atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_VERSION)) *
                    VERSION_SCALE +
                atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_REVISION)));
  const char* model = gps.receiver_id.model_number;
  const char line = libc_length(model) >= (size_t)MODEL_LABEL_LENGTH + 1 &&
                            strncmp(model, MODEL_LABEL, MODEL_LABEL_LENGTH) == 0
                        ? model[MODEL_LABEL_LENGTH]
                        : 0;

  if (line == VP) {
    gps.engine |= GPS_ENGINE_VP;
    gps.engine &= ~GPS_ENGINE_UT_PLUS;
    gps_engine_vp_set();
    gps.engine =
        (gps.engine & ~GPS_ENGINE_AQ_HIGH_MODES) |
        ((uint16_t)software >= VP_SOFTWARE_AQ ? GPS_ENGINE_AQ_HIGH_MODES : 0);
    gps.engine =
        (gps.engine & ~GPS_ENGINE_TRAIM) |
        (strchr(oncore_receiver_id_value(ONCORE_ID_OPTIONS), 'I') != NULL
             ? GPS_ENGINE_TRAIM
             : 0);
    gps.engine = (gps.engine & ~GPS_ENGINE_SOFTWARE_8_4) |
                 (software == VP_SOFTWARE_8_4 ? GPS_ENGINE_SOFTWARE_8_4 : 0);
  } else if (line == UT_PLUS) {
    gps.engine |= GPS_ENGINE_UT_PLUS;
    gps.engine &= ~GPS_ENGINE_VP;
    gps_engine_ut_plus_set();
    gps.engine = (gps.engine & ~GPS_ENGINE_IGNORED_SATELLITES) |
                 ((uint16_t)software >= UT_PLUS_SOFTWARE_3_0
                      ? GPS_ENGINE_IGNORED_SATELLITES
                      : 0);
    gps.engine =
        (gps.engine & ~GPS_ENGINE_LEAP_STATUS) |
        ((uint16_t)software >= UT_PLUS_SOFTWARE_3_0 ? GPS_ENGINE_LEAP_STATUS
                                                    : 0);
  } else {
    gps.engine &= ~(GPS_ENGINE_UT_PLUS | GPS_ENGINE_VP);
    gps_engine_unknown_set();
  }
  gps_channel_count_update();
  return cursor;
}

/* The text after a receiver ID line's label, for the line of a field
 * whose text starts with its label; NULL for none (0x55e9a). */
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

/* gps.channel_count, 8 for an engine of eight channels, else 6
 * (0x55ee6). */
void gps_channel_count_update(void) {
  gps.channel_count = (gps.engine & GPS_ENGINE_EIGHT_CHANNELS) != 0
                          ? GPS_CHANNELS
                          : GPS_SIX_CHANNELS;
}

/* The third @@Fa's send filter: 1 for a UT Plus (0x55efe). */
uint8_t oncore_filter_ut_plus(void) {
  return (gps.engine & GPS_ENGINE_UT_PLUS) != 0;
}

/* The send filter of the messages only a VP takes: 1 for a VP
 * (0x55f0e). */
uint8_t oncore_filter_vp(void) { return (gps.engine & GPS_ENGINE_VP) != 0; }

/* @@Bo's send filter: 1 for a UT Plus or an engine not a VP
 * (0x55f1e). */
uint8_t oncore_filter_utc_offset(void) {
  return (gps.engine & GPS_ENGINE_UT_PLUS) != 0 ||
         (gps.engine & GPS_ENGINE_VP) == 0;
}

/* @@Ca's send filter: 1 for an engine of six channels (0x55f48). */
uint8_t oncore_filter_six_channels(void) {
  return (gps.engine & GPS_ENGINE_EIGHT_CHANNELS) == 0;
}

/* @@Fa's send filter: 1 for an engine of eight channels (0x55f58). */
uint8_t oncore_filter_eight_channels(void) {
  return (gps.engine & GPS_ENGINE_EIGHT_CHANNELS) != 0;
}

/* @@Am's send filter (0x5607e). */
uint8_t oncore_filter_ignored_satellites(void) {
  return (gps.engine & GPS_ENGINE_IGNORED_SATELLITES) != 0;
}

/* The second @@Ar's send filter: 1 for a VP of software 8.4
 * (0x5608e). */
uint8_t oncore_filter_software_8_4(void) {
  return (gps.engine & GPS_ENGINE_SOFTWARE_8_4) != 0;
}

/* @@Gj's send filter (0x560a2). */
uint8_t oncore_filter_leap_status(void) {
  return (gps.engine & GPS_ENGINE_LEAP_STATUS) != 0;
}

/* Counts a frame error and starts the message again, from the byte if
 * it is an @. */
static void gps_oncore_restart(uint8_t c) {
  gps_counts.frame_errors++;
  if (c == '@') {
    gps_response_cursor = gps_response_buffer;
    gps_oncore_store(c);
    gps_receive_state = GPS_FRAMING_SECOND_AT;
  } else {
    gps_response_reset();
  }
}

/* A response's index in oncore_messages by its ID, and its length; 0
 * for an ID not there, which gps_receive counts, where the 58503A
 * 3704's counts it here (0x50286). */
uint32_t gps_response_lookup(char first, char second, int16_t* length,
                             gps_request_code_word* index) {
  static const int16_t COUNT =
      sizeof oncore_messages / sizeof oncore_messages[0];

  for (int16_t i = 0; i < COUNT; i++) {
    const oncore_message_entry* entry = oncore_message_entry_at(i);

    /* An empty entry's id is null, so that this reads ROM 0 and 1, as
     * the image does (docs/firmware/quirks.md). */
    if (first == entry->id[0] && second == entry->id[1]) {
      *index = i;
      *length = entry->length;
      return 1;
    }
  }
  return 0;
}

/* Stores a byte that is the one expected and moves to the next state,
 * else starts again (0x502cc). */
void gps_receive_expect(uint8_t c, uint8_t expected) {
  if (c == expected) {
    gps_oncore_store(c);
    gps_receive_state++;
  } else {
    gps_oncore_restart(c);
  }
}

/* Reads the bytes the engine has sent, framing a message from its @@
 * into gps_response_buffer, until none is left or a message is complete;
 * the framing state.  The 58503A 3704's but that an ID not in
 * oncore_messages is counted here (0x503fc) (0x50314). */
uint8_t gps_receive(void) {
  /* A message's bytes besides those its body counts down: "@@", the ID,
   * and CR LF. */
  static const int16_t FRAMING_BYTES = 6;

  while (duarta_rx_pending() && gps_receive_state != GPS_FRAMING_COMPLETE) {
    uint8_t c = drta_get_byte(0);

    switch (gps_receive_state) {
      case GPS_FRAMING_FIRST_AT:
      case GPS_FRAMING_SECOND_AT:
        gps_receive_expect(c, '@');
        break;
      case GPS_FRAMING_ID_FIRST:
        if ((ctype_table[c + 1] & LIBC_CTYPE_UPPER) != 0) {
          gps_oncore_store(c);
          gps_receive_state++;
        } else {
          gps_counts.frame_errors++;
          if (c != '@') {
            gps_response_reset();
          }
        }
        break;
      case GPS_FRAMING_ID_SECOND:
        if ((ctype_table[c + 1] & (LIBC_CTYPE_UPPER | LIBC_CTYPE_LOWER)) == 0) {
          gps_oncore_restart(c);
        } else {
          gps_oncore_store(c);
          if (gps_response_lookup(gps_response_cursor[-2],
                                  gps_response_cursor[-1], &gps_response_length,
                                  &gps_response_index) != 0) {
            gps_response_remaining = gps_response_length - FRAMING_BYTES;
            gps_receive_state++;
          } else {
            gps_counts.unknown_response++;
          }
        }
        break;
      case GPS_FRAMING_BODY:
        gps_oncore_store(c);
        if (--gps_response_remaining == 0) {
          /* The checksum, the body's last byte, covers the ID on. */
          if (c == oncore_checksum(gps_response_buffer + 2,
                                   gps_response_cursor - 1)) {
            gps_receive_state++;
          } else {
            gps_counts.checksum_errors++;
            gps_response_reset();
          }
        }
        break;
      case GPS_FRAMING_CR:
        gps_receive_expect(c, '\r');
        break;
      case GPS_FRAMING_LF:
        gps_receive_expect(c, '\n');
        break;
      default:
        break;
    }
  }
  return gps_receive_state;
}

/* 1 once more time has passed since start than the message's timeout
 * (0x50482). */
uint32_t gps_response_timed_out(gps_request_code_word message,
                                uint32_t* start) {
  int32_t elapsed;

  return uptime_elapsed(start, true, &elapsed) &&
         (uint32_t)elapsed > oncore_message_entry_at(message)->timeout;
}

/* Counts a request's outcome in gps_counts and gives it to its sender:
 * a monitor's in its result, another's sent back to plla_exchange; 1 if
 * it got there.  The 58503A 3704's with the poll monitor too
 * (0x504c2). */
int32_t gps_count_response(gps_request_result kind,
                           gps_request_entry* request) {
  uint8_t error;

  switch (kind) {
    case GPS_RESULT_SUCCESS:
      gps_counts.successes++;
      break;
    case GPS_RESULT_REJECT:
      gps_counts.rejects++;
      break;
    case GPS_RESULT_ABORT:
      gps_counts.aborts++;
      break;
    case GPS_RESULT_UNKNOWN_SENDER:
      gps_counts.unknown_sender++;
      break;
    case GPS_RESULT_UNKNOWN_COMMAND:
      gps_counts.unknown_command++;
      break;
    case GPS_RESULT_BUSY:
      gps_counts.busy++;
      break;
    case GPS_RESULT_NO_COMMAND:
      gps_counts.no_command++;
      break;
    case GPS_RESULT_NO_QUERY:
      gps_counts.no_query++;
      break;
    case GPS_RESULT_TIMEOUT:
      gps_counts.timeouts++;
      break;
    default:
      break;
  }
  switch (request->sender) {
    case GPS_SENDER_COMMAND_MONITOR:
      command_monitor_result = kind;
      error = 0;
      break;
    case GPS_SENDER_STATUS_MONITOR:
      status_monitor_result = kind;
      error = 0;
      break;
    case GPS_SENDER_POLL_MONITOR:
      poll_monitor_result = kind;
      error = 0;
      break;
    default:
      request->result = kind;
      error = (uint8_t)psos_send_x(plla_exchange, request);
      break;
  }
  if (error == 0) {
    gps_counts.sent++;
  } else {
    gps_counts.sent_errors++;
  }
  return error == 0;
}

/* Whether a response answers the command sent, by the message's match
 * kind; for those that compare, whether its bytes after the ID are the
 * command's, by nmea_field_equal (0x5638c).  The 58503A 3704's but for
 * this image's kinds and verdicts (oncore_match_kind, gps_match_result):
 * a GMT offset is compared as it is first, a height, or @@Au's, matched
 * by oncore_hold_heights_match alone, and @@As's held height by it too;
 * the second @@Ar is answered as the entry 0x3e it is. */
gps_match_result gps_response_match(gps_request_code_word message,
                                    uint8_t* sent, uint8_t length,
                                    gps_argument_mode mode,
                                    gps_request_code_word index,
                                    uint8_t* response,
                                    int16_t response_length) {
  /* @@Ab's offset: sign, hours, minutes. */
  static const uint8_t OFFSET_LENGTH = 3;
  static const uint8_t OFFSET_NEGATIVE = 0xff;
  /* @@Af's and @@Au's height, then @@Af's type; the response's GPS
   * height, then its MSL one. */
  static const uint8_t HEIGHT_LENGTH = 4;
  /* @@As's latitude, longitude, height and type. */
  static const uint8_t HOLD_HEIGHT = 8;
  static const uint8_t HOLD_TYPE = 12;
  const oncore_message_entry* entry = oncore_message_entry_at(message);
  const oncore_match_kind kind = entry->match_kind;
  gps_match_result result;
  uint8_t offset[3];
  uint8_t sent_start;
  uint8_t sent_count;
  uint8_t response_start;
  uint16_t response_count;

  switch (kind) {
    case ONCORE_MATCH_ANY:
      return GPS_MATCH_ANSWERED;
    case ONCORE_MATCH_WAIT:
      return GPS_MATCH_WAIT;
    case ONCORE_MATCH_SAME:
    case ONCORE_MATCH_UTC_OFFSET:
      return index == message ? GPS_MATCH_ANSWERED : GPS_MATCH_OTHER;
    case ONCORE_MATCH_USER_DATUM:
      return index == GPS_REQUEST_USER_DATUM ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SECOND_CA:
      return index == GPS_REQUEST_SELFTEST ? GPS_MATCH_ANSWERED
                                           : GPS_MATCH_OTHER;
    case ONCORE_MATCH_CORRECTIONS:
      return index == GPS_REQUEST_CORRECTION_INPUT ? GPS_MATCH_ANSWERED
                                                   : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SECOND_EA:
      return index == GPS_REQUEST_POSITION_STATUS_8 ? GPS_MATCH_ANSWERED
                                                    : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SECOND_FA:
      return index == GPS_REQUEST_SELFTEST_8 ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SECOND_AR:
      index = GPS_REQUEST_FIX_ALGORITHM_SECOND;
      break;
    case ONCORE_MATCH_ECHO:
    case ONCORE_MATCH_GMT_OFFSET:
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_HOLD_POSITION:
    case ONCORE_MATCH_ALTITUDE_HOLD:
    case ONCORE_MATCH_FIX_MODE:
      break;
    default:
      return GPS_MATCH_OTHER;
  }
  result = index == message ? GPS_MATCH_DIFFERS : GPS_MATCH_OTHER;
  if (result != GPS_MATCH_DIFFERS) {
    return result;
  }
  sent_start = libc_length("@@") + libc_length(entry->id);
  sent_count = length - sent_start;
  if (mode == GPS_ARGUMENTS_QUERY ||
      (kind == ONCORE_MATCH_FIX_MODE &&
       sent[sent_start] == ONCORE_FIX_MODE_QUERY)) {
    return GPS_MATCH_ANSWERED;
  }
  response_start =
      libc_length(oncore_message_entry_at(index)->id) + libc_length("@@");
  response_count = response_length - response_start;
  switch (kind) {
    case ONCORE_MATCH_GMT_OFFSET: {
      uint8_t* given = &sent[sent_start];

      if (given[0] == OFFSET_NEGATIVE && given[1] == 0) {
        if ((uint8_t)nmea_field_equal(given, sent_count,
                                      &response[response_start],
                                      response_count) == 1) {
          result = GPS_MATCH_ANSWERED;
          break;
        }
        memcpy(offset, given, OFFSET_LENGTH);
        offset[0] = 0;
        offset[2] = -offset[2];
        sent = offset;
        sent_start = 0;
        sent_count = OFFSET_LENGTH;
        response_count = OFFSET_LENGTH;
      }
      break;
    }
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_ALTITUDE_HOLD:
      if (kind == ONCORE_MATCH_HEIGHT &&
          sent[sent_start + HEIGHT_LENGTH] == ONCORE_HEIGHT_MSL) {
        response += HEIGHT_LENGTH;
      }
      sent_count = HEIGHT_LENGTH;
      response_count = HEIGHT_LENGTH;
      if ((uint8_t)oncore_hold_heights_match(&sent[sent_start],
                                             &response[response_start]) != 0) {
        result = GPS_MATCH_ANSWERED;
      }
      break;
    case ONCORE_MATCH_HOLD_POSITION:
      if (sent[sent_start + HOLD_TYPE] ==
              response[response_start + HOLD_TYPE] &&
          (uint8_t)oncore_hold_heights_match(
              &sent[sent_start + HOLD_HEIGHT],
              &response[response_start + HOLD_HEIGHT]) != 0) {
        sent_count = HOLD_HEIGHT;
        response_count = HOLD_HEIGHT;
      }
      break;
    default:
      break;
  }
  if (result == GPS_MATCH_ANSWERED) {
    return GPS_MATCH_ANSWERED;
  }
  return (uint8_t)nmea_field_equal(&sent[sent_start], sent_count,
                                   &response[response_start],
                                   response_count) == 1
             ? GPS_MATCH_ANSWERED
             : result;
}
