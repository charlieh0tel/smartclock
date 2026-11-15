/* The 58503B 1.01.04's own of the GPS task's side of the Oncore's
 * binary messages: oncore_position_message, which @@Ba's and @@Ea's
 * decoders call, the 58503A 3704's in its shape, which image_diff pairs
 * with it by its callers alone, with the Z3816A 4001's additions, whose
 * copy it is (docs/firmware/58503b-1.01.04.md, "The GPS record"), where
 * the decompiler tested the receiver status against ~ of every bit
 * where the image tests for none; and the response decoder, the Z3816A's
 * (relocated), which finds the message itself in the framing buffer it
 * is given, where the decompiler passed each decoder its arguments in
 * 2-byte slots (docs/firmware/58503b-1.01.04.md, "The function check").
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

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
 * the status' time; keeps the time status (gps_time_status); prints the status
 * every gps_status_print_interval messages; restarts the tracking log's
 * countdown; and keeps the time of this status (0x57942). */
void oncore_position_message(void) {
  /* The repeats of one status time it counts to, and the tracking log's
   * countdown, gpsm_task's passes. */
  static const uint8_t MOST_REPEATS = 46;
  static const uint8_t TRACKING_LOG_PASSES = 61;

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
    if ((gps.field_608 & GPS_FIELD_608_HEIGHT_MSL) != 0) {
      running_average_update(gps.status.height_msl, &gps.average_height_msl,
                             gps.average_count);
    } else {
      running_average_update(gps.status.height_gps, &gps.average_height_gps,
                             gps.average_count);
      gps.average_height_msl = gps.average_height_gps;
    }
    gps.average_count++;
    gps.field_5bf = 1;
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
    const uint32_t time = clock_time_bits(gps.status.time);

    if (time == gps.status_last_time) {
      if (gps.status_repeat_count < MOST_REPEATS) {
        gps.status_repeat_count++;
      }
    } else {
      gps.status_last_time = time;
      gps.status_repeat_count = 0;
    }
  }
  if (gps.utc_time == ONCORE_TIME_UTC && gps.utc_offset_received != 0) {
    gps.time_status = GPS_TIME_STATUS_UTC;
  } else if (gps.time_status != GPS_TIME_STATUS_UNTRACKED ||
             gps.utc_offset_received != 0 || usable) {
    gps.time_status = GPS_TIME_STATUS_GPS;
  } else {
    gps.time_status = GPS_TIME_STATUS_UNTRACKED;
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

/* Runs an Oncore decoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte is in D0
 * alone, where gcc takes a pointer result from A0, so it is called as
 * returning a long. */
static uint8_t* gps_oncore_decode(const oncore_decoder* decoder,
                                  uint8_t* cursor) {
  typedef uint32_t decode_in_d0(uint8_t* cursor, uint8_t* record);

  return (uint8_t*)((decode_in_d0*)decoder->decode)(cursor, decoder->record);
}

/* Runs a response's decoders over the framing buffer it is given, past
 * its "@@" and ID, with preemption off, its message found by its ID;
 * the cursor, the pointer after the buffer's bytes, gps_response_cursor
 * for gps_response_buffer, past what they read.  0, counted as an
 * unknown response, "? Resp", for an ID not in oncore_messages (0x586be),
 * where the Z3816A's counts its status monitor's failures; else 1
 * (0x5866a). */
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

/* The entry of oncore_messages for a message, by its index (0x584aa). */
oncore_message_entry* oncore_message_entry_at(int16_t index) {
  return &oncore_messages[index];
}

/* Puts @@Az's cable delay argument, in ns, at its index, the image's
 * pps_cable_delay_offset added, at the next, and encodes that as a long
 * (0x56e80).  It writes into the arguments it is given, a script step's
 * in ROM among them. */
uint8_t* oncore_cable_delay_encode(int32_t* arguments, uint8_t index,
                                   uint8_t* out) {
  long_array_set(pps_cable_delay_offset + long_array_get(arguments, index),
                 arguments, index + 1);
  return long_array_get_long(arguments, index + 1, out);
}

/* Decodes @@Ai's byte into record at its channel, 1 to gps.channel_count
 * (0x56fea). */
uint8_t* oncore_ai_decode(uint8_t* cursor, uint8_t* record) {
  const uint8_t channel = cursor[0];

  if (channel != 0 && channel <= gps.channel_count) {
    byte_copy_optional(&cursor[1], &record[channel - 1]);
  }
  return cursor + 2;
}

/* Decodes @@Az's cable delay, keeping it as given and, the image's
 * pps_cable_delay_offset taken off, in record (0x5703a). */
uint8_t* oncore_cable_delay_decode(uint8_t* cursor, int32_t* record) {
  uint32_t delay = 0;

  for (uint8_t i = 0; i < sizeof delay; i++) {
    delay = delay << CPU_BYTE_BITS | *cursor++;
  }
  gps.pps_cable_delay_given = delay;
  *record = delay - pps_cable_delay_offset;
  return cursor;
}

/* Decodes @@Ba, the six-channel position and status (0x57c5c). */
uint8_t* oncore_position_status_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_field_codes);
  oncore_position_message();
  return cursor;
}

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x57018). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* Decodes @@Ce, the pseudorange corrections, and keeps the mean and
 * standard deviation, in meters, of those of the satellites used for
 * position (0x57dc0). */
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

/* Decodes @@Bk, the position, status and data extension (0x57f2a).  Its
 * program, 26 84 84 0e 80, stops after matrix element 7: @@Bk's last
 * three elements, clock bias and oscillator offset are never stored;
 * only @@Ek's program, 40 80, stores them (docs/firmware/gps.md, "The
 * extension messages"). */
uint8_t* oncore_extension_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_extension_field_codes);
}

/* Decodes @@Ek, the eight-channel extension (0x57f44). */
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

/* The second @@Fa entry's send filter: 1 for an "R" receiver
 * (0x582ac). */
uint8_t oncore_filter_signal_cn(void) {
  return (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0;
}

/* @@Fa's send filter: 1 once @@Ea has come, as the 58503A's
 * (0x58306). */
uint8_t oncore_filter_eight_channels(void) {
  return (gps.field_608 & GPS_FIELD_608_EIGHT_CHANNELS) != 0;
}

/* @@Am's send filter: field_608's bit 6 (0x5842c). */
uint8_t oncore_filter_field_608_bit_6(void) {
  return (gps.field_608 & GPS_FIELD_608_BIT_6) != 0;
}

/* @@Ca's send filter: 1 until @@Ea has come, as the 58503A's
 * (0x582f6). */
uint8_t oncore_filter_six_channels(void) {
  return (gps.field_608 & GPS_FIELD_608_EIGHT_CHANNELS) == 0;
}

/* The channel count by whether @@Ea has come: 8, else 6 (0x58294). */
void gps_channel_count_update(void) {
  gps.channel_count = (gps.field_608 & GPS_FIELD_608_EIGHT_CHANNELS) != 0
                          ? GPS_CHANNELS
                          : GPS_SIX_CHANNELS;
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

/* Stores a byte that is the one expected and moves to the next state,
 * else starts again (0x5260a). */
void gps_receive_expect(uint8_t c, uint8_t expected) {
  if (c == expected) {
    gps_oncore_store(c);
    gps_receive_state++;
  } else {
    gps_oncore_restart(c);
  }
}

/* Points gps_response_cursor back at gps_response_buffer's start and
 * expects a message's first @. */
void gps_response_reset(void) {
  gps_response_cursor = gps_response_buffer;
  gps_receive_state = GPS_FRAMING_FIRST_AT;
}

/* Most queries' send filter: 1 for a "B" receiver (0x582bc). */
uint8_t oncore_filter_signal_ss(void) {
  return (gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0;
}

/* @@Bn's and @@En's send filter: oncore_filter_channels's while the
 * engine gives Time RAIM, else 0 (0x58408). */
uint8_t oncore_filter_traim(int16_t index, oncore_encoder* encoder,
                            int32_t* arguments) {
  if ((gps.field_608 & GPS_FIELD_608_TIME_RAIM) == 0) {
    return 0;
  }
  return oncore_filter_channels(index, encoder, arguments);
}

/* The second @@Ar entry's send filter: field_609's bit 7 (0x5843c). */
uint8_t oncore_filter_field_609_bit_7(void) {
  return (gps.field_609 & GPS_FIELD_609_BIT_7) != 0;
}

/* @@Gj's send filter: field_609's bit 6 (0x58450). */
uint8_t oncore_filter_field_609_bit_6(void) {
  return (gps.field_609 & GPS_FIELD_609_BIT_6) != 0;
}

/* Decodes @@Gj, the leap second pending status, as the Z3816A's
 * (0x5820a). */
uint8_t* oncore_leap_second_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_leap_second_field_codes);
}

/* Decodes @@Ap, the user datum, which answers @@Ao, as the Z3816A's
 * (0x5807e). */
uint8_t* oncore_user_datum_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_user_datum_field_codes);
}

/* @@Bj's decoder (oncore_messages' entry 0x26): copies the leap second
 * pending status into its record, gps.leap_second_reported, if there is
 * one, and the record into its copy at gps.leap_second_pending, so far
 * past it, which leap_event_create tests; the cursor past it (0x56f28).
 * The other images' entry is byte_copy_optional; the Z3816A's is this
 * routine, unnamed. */
uint8_t* oncore_leap_pending_decode(uint8_t* cursor, uint8_t* record) {
  static const size_t PENDING_COPY = offsetof(gps_state, leap_second_pending) -
                                     offsetof(gps_state, leap_second_reported);

  if (record != NULL) {
    record[0] = cursor[0];
    record[PENDING_COPY] = record[0];
  }
  return cursor + 1;
}

/* gps.field_608's bit 0, which the enum leaves unnamed (gps_field_608_bit):
 * the receiver ID's commit sets it for an "R" engine and for a "B" engine
 * of software version 10.0 or later (0x5814e), and @@Aq's encoder sends
 * the tropospheric model while it is set. */
static const uint8_t GPS_FIELD_608_BIT_0 = 0x01;

/* The receiver ID's commit's settings for an "R" receiver (0x581b6):
 * eight channels, whose count gps_channel_count_update takes; no MSL
 * height; bit 0 and Time RAIM; field_609's bit 7 clear; and the position
 * fix mode (0x56f46).  The Z3816A's routine, unnamed. */
void oncore_receiver_r_setup(void) {
  gps.field_608 |= GPS_FIELD_608_EIGHT_CHANNELS;
  gps_channel_count_update();
  gps.field_608 &= ~GPS_FIELD_608_HEIGHT_MSL;
  gps.field_608 |= GPS_FIELD_608_BIT_0;
  gps.field_608 |= GPS_FIELD_608_TIME_RAIM;
  gps.field_609 &= ~GPS_FIELD_609_BIT_7;
  gps.utc_offset_received = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
}

/* The receiver ID's commit's settings for a "B" receiver (0x5814a): no
 * MSL height; bit 6; field_609's bits 7 and 6 clear; the UTC offset
 * marked received, the position fix mode and the antenna normal
 * (0x56f80).  The Z3816A's routine, unnamed, changed. */
void oncore_receiver_b_setup(void) {
  gps.field_608 &= ~GPS_FIELD_608_HEIGHT_MSL;
  gps.field_608 |= GPS_FIELD_608_BIT_6;
  gps.field_609 &= ~GPS_FIELD_609_BIT_7;
  gps.field_609 &= ~GPS_FIELD_609_BIT_6;
  gps.utc_offset_received = 1;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
}

/* The receiver ID's commit's settings for a receiver neither "B" nor
 * "R", or an ID it cannot read (0x581f8): the bits the other two set
 * cleared, field_609's bits 7 and 6 too, the position fix mode and the
 * antenna normal (0x56fb0). */
void oncore_receiver_other_setup(void) {
  gps.field_608 &= ~GPS_FIELD_608_HEIGHT_MSL;
  gps.field_608 &= ~GPS_FIELD_608_BIT_6;
  gps.field_608 &= ~GPS_FIELD_608_BIT_0;
  gps.field_608 &= ~GPS_FIELD_608_TIME_RAIM;
  gps.field_609 &= ~GPS_FIELD_609_BIT_7;
  gps.field_609 &= ~GPS_FIELD_609_BIT_6;
  gps.utc_offset_received = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
}

/* @@Ea's receiver status byte's antenna bits (oncore_antenna_sense_set). */
static const uint8_t GPS_ONCORE_ANTENNA_OVER_CURRENT = 0x40;
static const uint8_t GPS_ONCORE_ANTENNA_UNDER_CURRENT = 0x80;

/* The A frames' and @@Ca's words are big-endian bytes, read one at a
 * time. */
static uint16_t gps_oncore_word(const uint8_t* cursor) {
  return (uint16_t)(cursor[0] << CPU_BYTE_BITS | cursor[1]);
}

/* Takes the antenna's current from @@Ea's receiver status byte into
 * gps.antenna_sense, bit 6 over, else bit 7 under, else normal; the bit
 * it found, or 0 (0x5706c). */
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

/* An engine's @@Ca or @@Fa self-test word as the generic bits self_test
 * keeps: an "R" engine's (GPS_FIELD_608_SIGNAL_CN) masked, its high byte's
 * antenna bits taken into gps.antenna_sense; a "B" engine's
 * (GPS_FIELD_608_SIGNAL_SS), a six-channel one's channel bits moved up to
 * an eight-channel one's, folded into bits 9 and 10 and its bits 13 and 15
 * moved down to 11 and 13; another's as it is; each but the "R" engine's
 * with the antenna sense normal (0x5709c). */
uint16_t oncore_selftest_generic(uint16_t response) {
  /* The bits of an "R" engine's word kept. */
  static const uint16_t SIGNAL_CN_KEPT = 0x37ff;
  /* A six-channel "B" engine's bits under its channel bits, which move up
   * by CHANNEL_SHIFT. */
  static const uint16_t SIX_CHANNEL_LOW = 0x003f;
  static const uint8_t CHANNEL_SHIFT = 2;
  /* An eight-channel "B" engine's word: the bits kept, those moved down by
   * MOVED_SHIFT, and the two pairs each folded into one bit. */
  static const uint16_t SIGNAL_SS_KEPT = 0x41ff;
  static const uint16_t SIGNAL_SS_MOVED = 0xa000;
  static const uint8_t MOVED_SHIFT = 2;
  static const uint16_t FOLDED_HIGH = 0x1800;
  static const uint16_t FOLDED_HIGH_BIT = 0x0400;
  static const uint16_t FOLDED_LOW = 0x0600;
  static const uint16_t FOLDED_LOW_BIT = 0x0200;

  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0) {
    oncore_antenna_sense_set((uint8_t)(response >> CPU_BYTE_BITS));
    return response & SIGNAL_CN_KEPT;
  }
  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
    if ((gps.field_608 & GPS_FIELD_608_EIGHT_CHANNELS) == 0) {
      response = (uint16_t)((response & ~SIX_CHANNEL_LOW) << CHANNEL_SHIFT |
                            (response & SIX_CHANNEL_LOW));
    }
    response = ((response & FOLDED_HIGH) != 0 ? FOLDED_HIGH_BIT : 0) |
               ((response & FOLDED_LOW) != 0 ? FOLDED_LOW_BIT : 0) |
               (response & SIGNAL_SS_MOVED) >> MOVED_SHIFT |
               (response & SIGNAL_SS_KEPT);
  }
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
  return response;
}

/* Decodes @@Ca or @@Fa, the self-test response: its word into record,
 * gps.self_test_response, and gps.self_test the failures of
 * gps.self_test_mask it has, which the mask then narrows to, printed with
 * the engine's kind, "UT" for an "R" engine, "VP" for a "B", else "??";
 * nothing with no record (0x57140). */
uint8_t* oncore_selftest_message(uint8_t* cursor, uint16_t* record) {
  const uint16_t response = gps_oncore_word(cursor);

  if (record != NULL) {
    char message[80];
    const char* kind;
    uint16_t generic;

    *record = response;
    generic = oncore_selftest_generic(response);
    gps.self_test = generic & gps.self_test_mask;
    if ((gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0) {
      kind = "UT";
    } else if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
      kind = "VP";
    } else {
      kind = "??";
    }
    debug_sprintf(message,
                  "%s test: resp %04X -> generic %04X & %04X = %04X\r\n", kind,
                  (uint32_t)response, (uint32_t)generic,
                  (uint32_t)gps.self_test_mask, (uint32_t)gps.self_test);
    task_dmessage(message);
    gps.self_test_mask = gps.self_test;
  }
  return cursor + sizeof response;
}

/* Decodes @@Bo, the UTC offset: its byte into record, and
 * gps.utc_offset_received while it is not 0 (0x571f4). */
uint8_t* oncore_utc_offset_message(uint8_t* cursor, uint8_t* record) {
  uint8_t* next = byte_copy_optional(cursor, record);

  if (record != NULL) {
    gps.utc_offset_received = *record != 0;
  }
  return next;
}

/* Decodes @@Ea, the eight-channel position and status, which marks the
 * engine as one of eight channels, then takes the antenna's current from
 * its receiver status byte, where the 58503A's takes none (0x57c82). */
uint8_t* oncore_position_status_8_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_8_field_codes);
  gps.field_608 |= GPS_FIELD_608_EIGHT_CHANNELS;
  gps_channel_count_update();
  oncore_position_message();
  oncore_antenna_sense_set(gps.status.field_23);
  return cursor;
}

/* Marks each visible GPS satellite of @@Bb's record removed in its health,
 * bit 0, while gps.ignored_satellites holds it, else not, once @@Bb has
 * come; received is gps.visibility_received, the record 2 bytes on, as
 * the Z3816A's (0x57cc0). */
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
 * past the list's entries is clamped to them; with an "R" engine's
 * GPS_FIELD_608_SIGNAL_CN and GPS_FIELD_608_BIT_6, marks the ignored
 * satellites removed, as the Z3816A's does with its word's bits 10 and 14
 * (0x57d32). */
uint8_t* oncore_visibility_message(uint8_t* cursor, uint8_t* record) {
  static const uint8_t ENTRIES =
      sizeof gps.visibility.satellites / sizeof gps.visibility.satellites[0];

  cursor = oncore_decode_fields(cursor, record, oncore_visibility_field_codes);
  gps.visibility_received = 1;
  if (gps.visibility.count > ENTRIES) {
    gps.visibility.count = ENTRIES;
  }
  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0 &&
      (gps.field_608 & GPS_FIELD_608_BIT_6) != 0) {
    oncore_visibility_removed_mark(&gps.visibility_received);
  }
  return cursor;
}

/* After @@Bn or @@En is decoded into record, gps.raim: each channel's
 * @@Ea mode copied into its own, the alarm limit multiplied by
 * RAIM_LIMIT_SCALE, gps.field_608's bit 3 set while the 1PPS is on, the
 * time of the message kept and the message marked received (0x57f86 to
 * 0x57fdc). */
static void gps_oncore_raim_commit(oncore_raim_fields* record) {
  static const int32_t RAIM_LIMIT_SCALE = 10;

  for (uint8_t i = 0; i < gps.channel_count; i++) {
    record->channels[i].mode = gps.status.channels[i].mode;
  }
  record->alarm_limit *= RAIM_LIMIT_SCALE;
  gps.field_608 =
      (gps.field_608 & ~GPS_FIELD_608_BIT_3) |
      (gps.raim.pps_mode == ONCORE_PPS_ON ? GPS_FIELD_608_BIT_3 : 0);
  gps.field_5ee = get_uptime();
  gps.raim_received = 1;
}

/* Decodes @@Bn, the Time RAIM setup and status (0x57f5e). */
uint8_t* oncore_traim_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_raim_field_codes);
  gps_oncore_raim_commit((oncore_raim_fields*)record);
  return cursor;
}

/* Decodes @@En, the eight-channel Time RAIM message, by its own field
 * table, otherwise as @@Bn (0x57fee). */
uint8_t* oncore_traim_8_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_raim_8_field_codes);
  gps_oncore_raim_commit((oncore_raim_fields*)record);
  return cursor;
}

/* Decodes @@Cj, the receiver ID, and takes the engine's kind from its
 * model number, "MODEL #    " then "B" or "R": a "B" engine signals by
 * SS, and from its software version, 10.0 or later, sets
 * GPS_FIELD_608_BIT_0, Time RAIM by its options' "I", and at 8.4
 * field_609's bit 7; an "R" engine signals by C/N, and from 3.0 sets bit
 * 6 of both; another signals by neither.  Then the channel count; the
 * Z3816A's, relocated (0x58098). */
uint8_t* oncore_receiver_id_message(uint8_t* cursor, uint8_t* record) {
  /* The model number's label and the kind's letter after it. */
  static const char MODEL_LABEL[] = "MODEL #    ";
  static const uint8_t MODEL_KIND = sizeof MODEL_LABEL - 1;
  /* The software version as tens of its version and its revision. */
  static const int16_t VERSION_SCALE = 10;
  static const uint16_t SIGNAL_SS_BIT_0_VERSION = 100;
  static const uint16_t SIGNAL_SS_609_BIT_7_VERSION = 84;
  static const uint16_t SIGNAL_CN_BIT_6_VERSION = 30;
  const char* model = gps.receiver_id.model_number;
  uint16_t version;
  char kind;

  cursor = oncore_decode_fields(cursor, record, oncore_receiver_id_field_codes);
  gps.receiver_id_received = 1;
  version =
      (int16_t)atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_VERSION)) *
          VERSION_SCALE +
      (int16_t)atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_REVISION));
  kind = model[MODEL_KIND];
  if ((uint32_t)libc_length(model) < (uint32_t)MODEL_KIND + 1 ||
      strncmp(model, MODEL_LABEL, MODEL_KIND) != 0 ||
      (kind != 'B' && kind != 'R')) {
    gps.field_608 &= ~(GPS_FIELD_608_SIGNAL_CN | GPS_FIELD_608_SIGNAL_SS);
    oncore_receiver_other_setup();
  } else if (kind == 'B') {
    gps.field_608 |= GPS_FIELD_608_SIGNAL_SS;
    gps.field_608 &= ~GPS_FIELD_608_SIGNAL_CN;
    oncore_receiver_b_setup();
    gps.field_608 =
        (gps.field_608 & ~GPS_FIELD_608_BIT_0) |
        (version >= SIGNAL_SS_BIT_0_VERSION ? GPS_FIELD_608_BIT_0 : 0);
    gps.field_608 =
        (gps.field_608 & ~GPS_FIELD_608_TIME_RAIM) |
        (strchr(oncore_receiver_id_value(ONCORE_ID_OPTIONS), 'I') != NULL
             ? GPS_FIELD_608_TIME_RAIM
             : 0);
    gps.field_609 =
        (gps.field_609 & ~GPS_FIELD_609_BIT_7) |
        (version == SIGNAL_SS_609_BIT_7_VERSION ? GPS_FIELD_609_BIT_7 : 0);
  } else {
    gps.field_608 |= GPS_FIELD_608_SIGNAL_CN;
    gps.field_608 &= ~GPS_FIELD_608_SIGNAL_SS;
    oncore_receiver_r_setup();
    gps.field_608 =
        (gps.field_608 & ~GPS_FIELD_608_BIT_6) |
        (version >= SIGNAL_CN_BIT_6_VERSION ? GPS_FIELD_608_BIT_6 : 0);
    gps.field_609 =
        (gps.field_609 & ~GPS_FIELD_609_BIT_6) |
        (version >= SIGNAL_CN_BIT_6_VERSION ? GPS_FIELD_609_BIT_6 : 0);
  }
  gps_channel_count_update();
  return cursor;
}

/* Decodes @@Sz, the power-on failure message, which has no data: marks it
 * received in record, gps.sz_received, which nothing reads (0x58224). */
uint8_t* oncore_power_on_failure_message(uint8_t* cursor, uint8_t* record) {
  *record = 1;
  return cursor;
}

/* @@Bo's send filter: 1 unless the engine is a "B" one alone, SS without
 * C/N (0x582cc). */
uint8_t oncore_filter_not_signal_ss_alone(void) {
  return (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0 ||
         (gps.field_608 & GPS_FIELD_608_SIGNAL_SS) == 0;
}

/* The failures self_test can hold, its low 14 bits. */
static const uint16_t GPS_ONCORE_SELFTEST_FAILURES = 0x3fff;

/* The second @@Ca entry's send filter: oncore_filter_six_channels's
 * while gps.self_test holds a failure, so that the test is sent again
 * (0x5839c). */
uint8_t oncore_filter_six_channels_retest(int16_t index,
                                          oncore_encoder* encoder,
                                          int32_t* arguments) {
  return oncore_filter_six_channels() != 0 &&
         (gps.self_test & GPS_ONCORE_SELFTEST_FAILURES) != 0;
}

/* The second @@Fa entry's: oncore_filter_eight_channels's while
 * gps.self_test holds a failure (0x583d2). */
uint8_t oncore_filter_eight_channels_retest(int16_t index,
                                            oncore_encoder* encoder,
                                            int32_t* arguments) {
  return oncore_filter_eight_channels() != 0 &&
         (gps.self_test & GPS_ONCORE_SELFTEST_FAILURES) != 0;
}

/* @@Aq's encoder: the argument's low byte, the atmospheric correction
 * mode, its bit 1, the tropospheric model, cleared unless
 * GPS_FIELD_608_BIT_0 is set (0x56e4e). */
uint8_t* oncore_atmosphere_encode(int32_t* arguments, uint8_t index,
                                  uint8_t* out) {
  static const uint32_t TROPOSPHERE = 0x02;
  uint32_t mode = (uint32_t)long_array_get(arguments, index);

  if ((gps.field_608 & GPS_FIELD_608_BIT_0) == 0) {
    mode &= ~TROPOSPHERE;
  }
  *out++ = (uint8_t)mode;
  return out;
}

/* An argument's low three bytes, high byte first (0x56d26). */
uint8_t* long_array_get_three_bytes(int32_t* array, uint8_t index,
                                    uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 16);
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)value;
  return out + 3;
}

/* The first argument's high word, high byte first, whatever the index:
 * the image passes long_array_get none (0x56d88). */
uint8_t* long_array_get_first_high_word(int32_t* array, uint8_t index,
                                        uint8_t* out) {
  uint16_t word = (uint16_t)((uint32_t)long_array_get(array, 0) >> 16);

  out[0] = (uint8_t)(word >> CPU_BYTE_BITS);
  out[1] = (uint8_t)word;
  return out + 2;
}

/* As many bytes as the first argument's low byte gives, at most
 * MOST_BYTES, from the arguments after it, each long high byte first;
 * the index unread (0x56db4). */
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

/* The text after a receiver ID line's label, for the line of a field
 * whose text starts with its label; NULL for none, as the 58503A's
 * (0x58248). */
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

/* A response's index in oncore_messages by its ID, and its length; 0
 * for an ID not there, which gps_receive counts, where the 58503A's
 * counts it here (0x525c4). */
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

/* Reads the bytes the engine has sent, framing a message from its @@
 * into gps_response_buffer, until none is left or a message is complete;
 * the framing state.  An ID gps_response_lookup does not find is counted
 * an unknown response (0x5273a); the 58503A's, relocated, on the ACIA
 * (0x52652). */
gps_framing_state gps_receive(void) {
  /* A message's bytes besides those its body counts down: "@@", the ID,
   * and CR LF. */
  static const int16_t FRAMING_BYTES = 6;

  while (acia_rx_pending() && gps_receive_state != GPS_FRAMING_COMPLETE) {
    uint8_t c = acia_get_byte(0);

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

/* 1 once more time has passed since start than the message's timeout;
 * start is restarted if the clock went back (0x527c0). */
uint32_t gps_response_timed_out(gps_request_code_word message,
                                uint32_t* start) {
  int32_t elapsed;

  return uptime_elapsed(start, 1, &elapsed) &&
         (uint32_t)elapsed > oncore_message_entry_at(message)->timeout;
}

/* Whether a response answers the command sent, by the message's match
 * kind; for those that compare, whether its bytes after the ID are the
 * command's.  The Z3816A's, identical, the 58503A's with this image's
 * match kinds and verdicts: @@Ab's negative offset compared as it is
 * before it is turned, and @@Af's, @@Au's and @@As's heights taken as
 * close by oncore_hold_heights_match (0x5873a). */
gps_match_result gps_response_match(gps_request_code_word message,
                                    uint8_t* sent, uint8_t length,
                                    gps_argument_mode mode,
                                    gps_request_code_word index,
                                    uint8_t* response,
                                    int16_t response_length) {
  /* @@Ab's offset: sign, hours, minutes. */
  static const uint8_t OFFSET_LENGTH = 3;
  static const uint8_t OFFSET_NEGATIVE = 0xff;
  /* @@Af's and @@Au's height, then its type; the response's GPS height,
   * then its MSL one. */
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
  int16_t response_count;

  switch (kind) {
    case ONCORE_MATCH_ANY:
      return GPS_MATCH_ANSWERED;
    case ONCORE_MATCH_TIMEOUT:
      return GPS_MATCH_TIMEOUT;
    case ONCORE_MATCH_SAME:
    case ONCORE_MATCH_SAME_SIGNAL_CN:
      return index == message ? GPS_MATCH_ANSWERED : GPS_MATCH_OTHER;
    case ONCORE_MATCH_USER_DATUM:
      return index == GPS_REQUEST_USER_DATUM ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SELFTEST:
      return index == GPS_REQUEST_SELFTEST ? GPS_MATCH_ANSWERED
                                           : GPS_MATCH_OTHER;
    case ONCORE_MATCH_CORRECTIONS:
      return index == GPS_REQUEST_CORRECTION_INPUT ? GPS_MATCH_ANSWERED
                                                   : GPS_MATCH_OTHER;
    case ONCORE_MATCH_EA:
      return index == GPS_REQUEST_POSITION_STATUS_8 ? GPS_MATCH_ANSWERED
                                                    : GPS_MATCH_OTHER;
    case ONCORE_MATCH_FA:
      return index == GPS_REQUEST_SELFTEST_8 ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_ECHO:
    case ONCORE_MATCH_GMT_OFFSET:
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_SECOND_AR:
    case ONCORE_MATCH_HOLD_POSITION:
    case ONCORE_MATCH_HOLD_HEIGHT:
    case ONCORE_MATCH_FIX_MODE:
      break;
    default:
      return GPS_MATCH_OTHER;
  }
  if (kind == ONCORE_MATCH_SECOND_AR) {
    index = GPS_REQUEST_FIX_ALGORITHM_SECOND;
  }
  if (index != message) {
    return GPS_MATCH_OTHER;
  }
  result = GPS_MATCH_DIFFERS;
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

      if (given[0] != OFFSET_NEGATIVE || given[1] != 0) {
        break;
      }
      if (nmea_field_equal(given, sent_count, &response[response_start],
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
      break;
    }
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_HOLD_HEIGHT:
      if (kind == ONCORE_MATCH_HEIGHT &&
          sent[sent_start + HEIGHT_LENGTH] == ONCORE_HEIGHT_MSL) {
        response += HEIGHT_LENGTH;
      }
      sent_count = HEIGHT_LENGTH;
      response_count = HEIGHT_LENGTH;
      if (oncore_hold_heights_match(&sent[sent_start],
                                    &response[response_start]) != 0) {
        result = GPS_MATCH_ANSWERED;
      }
      break;
    case ONCORE_MATCH_HOLD_POSITION:
      if (sent[sent_start + HOLD_TYPE] ==
              response[response_start + HOLD_TYPE] &&
          oncore_hold_heights_match(&sent[sent_start + HOLD_HEIGHT],
                                    &response[response_start + HOLD_HEIGHT]) !=
              0) {
        sent_count = HOLD_HEIGHT;
        response_count = HOLD_HEIGHT;
      }
      break;
    default:
      break;
  }
  if (result == GPS_MATCH_ANSWERED) {
    return result;
  }
  return nmea_field_equal(&sent[sent_start], sent_count,
                          &response[response_start], response_count) == 1
             ? GPS_MATCH_ANSWERED
             : result;
}

/* Counts a request's outcome in gps_counts and gives it to its sender:
 * a monitor's in its result, the Z3816A's poll monitor's among them,
 * another's sent back to plla_exchange; 1 if it got there (0x52800). */
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
    case GPS_SENDER_POLL_MONITOR:
      poll_monitor_result = kind;
      error = 0;
      break;
    case GPS_SENDER_COMMAND_MONITOR:
      command_monitor_result = kind;
      error = 0;
      break;
    case GPS_SENDER_STATUS_MONITOR:
      status_monitor_result = kind;
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
