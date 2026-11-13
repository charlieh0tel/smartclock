/* From the 58503A 3704's rewritten/gps_oncore.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"), and, after
 * them, the response's lookup, count and decode, the corrections', Time RAIM's,
 * receiver ID's, @@Sz's and status messages' decoders, the send filters, the
 * echoes, the timeout and the answer's match, each written as this image's
 * where its code differs
 * ("The export and the checks").  Its comments' addresses are the
 * Z3817A's, but where they name another image.  Each keeps its generated
 * prototype. */

/* The entry of oncore_messages for a message, by its index (0x57014). */
oncore_message_entry* oncore_message_entry_at(int16_t index) {
  return &oncore_messages[index];
}

/* Decodes @@Bb, the visible satellites, and marks them received; a count
 * past the list's entries is clamped to them (0x56a1e), as the Z3805A's
 * is not. */
uint8_t* oncore_visibility_message(uint8_t* cursor, uint8_t* record) {
  static const uint8_t ENTRIES =
      sizeof gps.visibility.satellites / sizeof gps.visibility.satellites[0];

  cursor = oncore_decode_fields(cursor, record, oncore_visibility_field_codes);
  gps.visibility_received = 1;
  if (gps.visibility.count > ENTRIES) {
    gps.visibility.count = ENTRIES;
  }
  return cursor;
}

/* Decodes @@Bk, the position, status and data extension (0x56bd0).  Its
 * program, 26 84 84 0e 80, stops after matrix element 7: @@Bk's last
 * three elements, clock bias and oscillator offset are never stored;
 * only @@Ek's program, 40 80, stores them (docs/firmware/gps.md, "The
 * extension messages"). */
uint8_t* oncore_extension_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_extension_field_codes);
}

/* Decodes @@Ek, the eight-channel extension (0x56bea). */
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

/* Unpacks a message into its record by a field program, oncore_field_code
 * bytes (docs/firmware/gps.md, "Decoding"); the cursor past what it read. */
uint8_t* oncore_decode_fields(uint8_t* source, uint8_t* destination,
                              uint8_t* program) {
  static const uint8_t SIGN_BIT = 0x80;
  int8_t repeats = 1;
  const uint8_t* group = program;
  const uint8_t* op = program;

  for (;;) {
    uint8_t code = *op;

    if (code == ONCORE_FIELD_END) {
      return source;
    }
    if (code == ONCORE_FIELD_GROUP_END && --repeats > 0) {
      op = group;
      continue;
    }
    if (code > ONCORE_FIELD_SKIP_DESTINATION && code < ONCORE_FIELD_END) {
      /* The count is read again for each byte, signed, as the image
       * does, so a record laid over the program ends the copy where it
       * has written over it. */
      for (uint8_t i = 1; i <= (int8_t)*op; i++) {
        *destination++ = *source++;
      }
    } else if (code == ONCORE_FIELD_SKIP_DESTINATION) {
      destination++;
    } else if (code == ONCORE_FIELD_SIGN) {
      *destination++ = (*source & SIGN_BIT) != 0 ? 0xff : 0;
    } else if (code == ONCORE_FIELD_ZERO) {
      *destination++ = 0;
    } else if (code == ONCORE_FIELD_SKIP_ZERO) {
      source++;
      *destination++ = 0;
    } else if (code == ONCORE_FIELD_SKIP_SOURCE) {
      source++;
    } else if (code >= ONCORE_FIELD_GROUP_FIRST) {
      /* The byte is the group's count negated. */
      repeats = -(int8_t)code;
      group = op + 1;
    }
    op++;
  }
}

/* Decodes @@Bc, the DOP table. */
uint8_t* oncore_dop_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_xdop_table_field_codes);
}

/* Decodes @@Bd, the almanac status. */
uint8_t* oncore_almanac_status_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_almanac_field_codes);
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
 * else starts again (0x521b0). */
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
 * the framing state (0x521f8). */
gps_framing_state gps_receive(void) {
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
          /* gps_response_lookup takes the length as unsigned. */
          if (gps_response_lookup(
                  gps_response_cursor[-2], gps_response_cursor[-1],
                  (uint16_t*)&gps_response_length, &gps_response_index) != 0) {
            gps_response_remaining = gps_response_length - FRAMING_BYTES;
            gps_receive_state++;
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

/* A response's index in oncore_messages by its ID, and its length;
 * 0 for an ID not there, which gps_response_decode counts, where the
 * 58503A's counts it here (0x5216a). */
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

/* Counts a request's outcome in gps_counts and gives it to its sender:
 * a monitor's in its result, another's sent back to plla_exchange; 1 if
 * it got there (0x523a6). */
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
  if (request->sender == GPS_SENDER_COMMAND_MONITOR) {
    command_monitor_result = kind;
    error = 0;
  } else if (request->sender == GPS_SENDER_STATUS_MONITOR) {
    status_monitor_result = kind;
    error = 0;
  } else {
    request->result = kind;
    error = (uint8_t)psos_send_x(plla_exchange, request);
  }
  if (error == 0) {
    gps_counts.sent++;
  } else {
    gps_counts.sent_errors++;
  }
  return error == 0;
}

/* Decodes @@Ce, the pseudorange corrections, and keeps the mean and
 * standard deviation, in meters, of those of the satellites used for
 * position (0x56a66). */
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

/* Runs a decoder of oncore_messages on a response.  The table holds the
 * image's own routines, which return the cursor past what they read in
 * D0 alone, where gcc takes a pointer result from A0. */
static uint8_t* gps_oncore_decode(const oncore_decoder* decoder,
                                  uint8_t* cursor) {
  typedef uint32_t decode_in_d0(uint8_t* cursor, uint8_t* record);

  return (uint8_t*)((decode_in_d0*)decoder->decode)(cursor, decoder->record);
}

/* Decodes a framed response: looks its ID up after its "@@", counting
 * one oncore_messages lacks and giving 0; else runs the message's
 * decoders past its ID with preemption off, marks it heard
 * (gps_echo_mark), gives @@Ba's time to status_snapshot_update, and
 * gives 1 (0x571a0).  The 58503A's takes the index its caller looked
 * up, and the lookup counts the unknown response.  The image tests a
 * decoder before its count, reading the long after the third's place. */
uint32_t gps_response_decode(uint8_t* response) {
  static const uint8_t DECODERS = sizeof oncore_messages[0].decoders /
                                  sizeof oncore_messages[0].decoders[0];
  uint8_t* cursor = response + libc_length("@@");
  gps_request_code_word message;
  int16_t length;
  const oncore_message_entry* entry;

  if (gps_response_lookup(cursor[0], cursor[1], &length, &message) == 0) {
    gps_counts.unknown_response++;
    return 0;
  }
  entry = oncore_message_entry_at(message);
  psos_mode(PSOS_MODE_NO_PREEMPTION, PSOS_MODE_NO_PREEMPTION, NULL);
  cursor += libc_length(entry->id);
  for (uint8_t i = 0; i < DECODERS && entry->decoders[i].decode != NULL; i++) {
    cursor = gps_oncore_decode(&entry->decoders[i], cursor);
  }
  gps_echo_mark(message);
  psos_mode(0, PSOS_MODE_NO_PREEMPTION, NULL);
  if (message == GPS_REQUEST_POSITION_STATUS) {
    status_snapshot_update(clock_time_bits(gps.status.time));
  }
  return 1;
}

/* Learns the engine's abilities from its receiver ID (0x56e2a): its
 * channels; then, the ID come, for a model number starting with an R
 * Time RAIM and GPS_FIELD_592_MODEL_R whatever its options, else bit 6,
 * Time RAIM for an I in its options and software 8.4, its version and
 * revision, as the 58503A's; without the ID none of them.  Each time
 * it sets GPS_FIELD_592_PPS_CONTINUOUS.  A line not found gives strchr
 * and atoi a null text. */
void gps_receiver_abilities_update(void) {
  static const int32_t SOFTWARE_8_4 = 84;
  static const int32_t VERSION_SCALE = 10;
  static const char MODEL_R = 'R';

  gps_channel_count_update();
  if (gps.receiver_id_received == 0) {
    gps.field_592 &= ~(GPS_FIELD_592_MODEL_R | GPS_FIELD_592_BIT_6 |
                       GPS_FIELD_592_TIME_RAIM | GPS_FIELD_592_SOFTWARE_8_4);
  } else {
    int16_t version =
        atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_VERSION)) *
        VERSION_SCALE;
    int16_t revision =
        atoi(oncore_receiver_id_value(ONCORE_ID_SOFTWARE_REVISION));

    if (gps.receiver_id.model_number[0] == MODEL_R) {
      gps.field_592 = (gps.field_592 &
                       ~(GPS_FIELD_592_BIT_6 | GPS_FIELD_592_SOFTWARE_8_4)) |
                      GPS_FIELD_592_MODEL_R | GPS_FIELD_592_TIME_RAIM;
    } else {
      bool option_i =
          strchr(oncore_receiver_id_value(ONCORE_ID_OPTIONS), 'I') != 0;

      gps.field_592 =
          (gps.field_592 & ~GPS_FIELD_592_MODEL_R) | GPS_FIELD_592_BIT_6;
      gps.field_592 = (gps.field_592 & ~GPS_FIELD_592_TIME_RAIM) |
                      (option_i ? GPS_FIELD_592_TIME_RAIM : 0);
      gps.field_592 = (gps.field_592 & ~GPS_FIELD_592_SOFTWARE_8_4) |
                      ((int16_t)(version + revision) == SOFTWARE_8_4
                           ? GPS_FIELD_592_SOFTWARE_8_4
                           : 0);
    }
  }
  gps.field_592 |= GPS_FIELD_592_PPS_CONTINUOUS;
}

/* What @@Bn's and @@En's decoders do after their fields, the same in
 * both: the alarm limit made 10 ns, Time RAIM known and
 * GPS_FIELD_592_PPS_CONTINUOUS by the 1PPS mode, and the time it came
 * (0x56c2c to 0x56c6e), where the 58503A's set traim_message_arrived
 * alone. */
static void gps_oncore_raim_received(oncore_raim_fields* raim) {
  static const int32_t ALARM_SCALE = 10;

  raim->alarm_limit *= ALARM_SCALE;
  gps.field_592 =
      (gps.field_592 & ~GPS_FIELD_592_PPS_CONTINUOUS) |
      GPS_FIELD_592_TIME_RAIM |
      (gps.raim.pps_mode == ONCORE_PPS_ON ? GPS_FIELD_592_PPS_CONTINUOUS : 0);
  gps.raim_time = get_uptime();
  gps.raim_received = 1;
}

/* Decodes @@Bn, the Time RAIM setup and status (0x56c04). */
uint8_t* oncore_traim_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_raim_field_codes);
  gps_oncore_raim_received((oncore_raim_fields*)record);
  return cursor;
}

/* Decodes @@En, the eight-channel Time RAIM status (0x56c76). */
uint8_t* oncore_traim_8_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_raim_8_field_codes);
  gps_oncore_raim_received((oncore_raim_fields*)record);
  return cursor;
}

/* Decodes @@Cj, the receiver ID, marks it received and learns the
 * engine's abilities from it (0x56cf6). */
uint8_t* oncore_receiver_id_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_receiver_id_field_codes);
  gps.receiver_id_received = 1;
  gps_receiver_abilities_update();
  return cursor;
}

/* @@Sz's decoder, the power-on failure message, which has no data:
 * marks it through its record pointer, gps.power_on_failure, and gives
 * the cursor as it came (0x56ce8). */
uint8_t* oncore_power_on_failure_message(uint8_t* cursor, uint8_t* record) {
  *record = 1;
  return cursor;
}

/* @@Fa's send filter (oncore_messages entry 0x35): while gps.field_592's
 * eight channels are set, its bit 7, the 58503A's bit 5 (0x56f18). */
uint8_t oncore_filter_eight_channels(void) {
  return (gps.field_592 & GPS_FIELD_592_EIGHT_CHANNELS) != 0;
}

/* gps.echo_ages' value for a message not heard since gps_echoes_reset. */
static const uint8_t GPS_ECHO_AGE_UNKNOWN = 0xff;

/* Marks a message heard: for its entry in gps_echo_entries, which
 * GPS_REQUEST_NONE ends, sets its bit of gps.echoes and clears its age;
 * a message with none is not marked (0x56d3c). */
void gps_echo_mark(gps_request_code_word message) {
  static const uint8_t AGES = sizeof gps.echo_ages / sizeof gps.echo_ages[0];
  const gps_echo_entry* entry = gps_echo_entries;

  for (; entry->message != GPS_REQUEST_NONE; entry++) {
    if (entry->message == message) {
      gps.echoes |= 1 << entry->echo;
      if ((int8_t)entry->echo < AGES) {
        gps.echo_ages[entry->echo] = 0;
      }
      return;
    }
  }
}

/* Clears gps.echoes but for the position hold, which it marks heard, and
 * makes every age unknown (0x56d7e). */
void gps_echoes_reset(void) {
  static const uint8_t AGES = sizeof gps.echo_ages / sizeof gps.echo_ages[0];
  static const uint16_t NONE_HEARD = 0;

  gps.echoes = NONE_HEARD;
  gps_echo_mark(GPS_REQUEST_POSITION_HOLD);
  for (uint8_t i = 0; i < AGES; i++) {
    gps.echo_ages[i] = GPS_ECHO_AGE_UNKNOWN;
  }
}

/* Whether a setting message's echo has been heard since gps_echoes_reset
 * (0x56da8). */
bool gps_echo_seen(gps_echo echo) { return (gps.echoes & (1 << echo)) != 0; }

/* The engine's cable delay, @@Az's, once its echo has been heard, else
 * GPS_CABLE_DELAY_UNHEARD (0x56616); unit_and_reference_check compares
 * it with the last it saw (0x4d5d0). */
int32_t gps_cable_delay_heard(void) {
  static const int32_t GPS_CABLE_DELAY_UNHEARD = -1000000;

  return gps_echo_seen(GPS_ECHO_CABLE_DELAY) ? gps.pps_cable_delay
                                             : GPS_CABLE_DELAY_UNHEARD;
}

/* 1 once more time has passed since start than the message's timeout
 * (0x52366). */
uint32_t gps_response_timed_out(gps_request_code_word message,
                                uint32_t* start) {
  int32_t elapsed;

  return uptime_elapsed(start, 1, &elapsed) &&
         (uint32_t)elapsed > oncore_message_entry_at(message)->timeout;
}

/* gps.channel_count, 8 while the last status message was @@Ea, else 6
 * (0x56e12). */
void gps_channel_count_update(void) {
  gps.channel_count = (gps.field_592 & GPS_FIELD_592_EIGHT_CHANNELS) != 0
                          ? GPS_CHANNELS
                          : GPS_SIX_CHANNELS;
}

/* After @@Ba or @@Ea is decoded: while a survey runs and the fix is 3D,
 * averages the position, its height the GPS one or, for an engine of
 * GPS_FIELD_592_BIT_6, the MSL one; qualifies the GPS reference; while a
 * channel tracks a satellite for position, counts the status's time
 * repeating, to GPS_STATUS_MOST_REPEATS; prints the status every
 * gps_status_print_interval messages; and stamps
 * gps.status_monitor_time, which the status monitor watches (0x56858).
 * The 58503A 3704's averages the MSL height alone and marks the status
 * arrived. */
void oncore_position_message(void) {
  if (gps.position_hold == 0 &&
      (nv_settings.douglas_duration_infinite == 1 ||
       gps.average_count < (uint32_t)nv_settings.douglas_duration) &&
      (gps.status.receiver_status & ONCORE_STATUS_3D_FIX) != 0) {
    running_average_update(gps.status.latitude, &gps.average_latitude,
                           gps.average_count);
    running_average_update(gps.status.longitude, &gps.average_longitude,
                           gps.average_count);
    if ((gps.field_592 & GPS_FIELD_592_BIT_6) != 0) {
      running_average_update(gps.status.height_msl, &gps.average_height_msl,
                             gps.average_count);
    } else {
      running_average_update(gps.status.height_gps, &gps.average_height_gps,
                             gps.average_count);
    }
    gps.average_count++;
    gps.surveying = 1;
  }
  gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
  bool tracking = false;

  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    const oncore_channel_status* status = &gps.status.channels[channel - 1];

    if (((uint8_t)satellite_id_valid(SATELLITE_GPS, status->satellite) != 0 ||
         (uint8_t)satellite_id_valid(SATELLITE_PSEUDOLITE, status->satellite) !=
             0) &&
        status->mode == ONCORE_AVAILABLE_FOR_POSITION) {
      tracking = true;
      break;
    }
  }
  if (tracking) {
    const uint32_t time = clock_time_bits(gps.status.time);

    if (time == gps.status_last_time) {
      if (gps.status_repeat_count < GPS_STATUS_MOST_REPEATS) {
        gps.status_repeat_count++;
      }
    } else {
      gps.status_last_time = time;
      gps.status_repeat_count = 0;
    }
  }
  if (gps_status_print_interval != 0 &&
      ++gps_status_print_count >= gps_status_print_interval) {
    gps_status_print_count = 0;
    gps_debug_status();
  }
  gps.status_monitor_time = get_uptime();
}

/* Decodes @@Ba, the six-channel position and status, which marks the
 * engine as one of six channels (0x5698e). */
uint8_t* oncore_position_status_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_field_codes);
  gps.field_592 &= ~GPS_FIELD_592_EIGHT_CHANNELS;
  gps_channel_count_update();
  oncore_position_message();
  return cursor;
}

/* Decodes @@Ea, the eight-channel position and status, which marks the
 * engine as one of eight channels (0x569c2). */
uint8_t* oncore_position_status_8_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_status_8_field_codes);
  gps.field_592 |= GPS_FIELD_592_EIGHT_CHANNELS;
  gps_channel_count_update();
  oncore_position_message();
  return cursor;
}

/* The six-channel messages' send filter, @@Ca's: 1 while the last status
 * message was @@Ba (0x56f08). */
uint8_t oncore_filter_six_channels(void) {
  return (gps.field_592 & GPS_FIELD_592_EIGHT_CHANNELS) == 0;
}

/* Whether a response answers the message sent, by the message's match
 * kind (0x57280, its jump table at 0x572c2); for those that compare,
 * whether its bytes after the ID are the command's.  The second @@Ar is
 * answered as the entry 0x38 it is, and the second @@Ea by the first.
 * The 58503A 3704's but that a height, @@Af's and @@Au's, within 2 cm of
 * the one sent while that is in range answers (oncore_hold_heights_match,
 * 0x573e2). */
gps_match_result gps_response_match(gps_request_code_word message,
                                    uint8_t* sent, uint8_t length,
                                    gps_argument_mode mode,
                                    gps_request_code_word index,
                                    uint8_t* response,
                                    int16_t response_length) {
  /* @@Ab's offset: sign, hours, minutes. */
  static const uint8_t OFFSET_LENGTH = 3;
  static const uint8_t OFFSET_NEGATIVE = 0xff;
  /* @@Af's and @@Au's height, then its type; @@Af's response's GPS
   * height, then its MSL one. */
  static const uint8_t HEIGHT_LENGTH = 4;
  /* @@As's latitude, longitude, height and type (VPCommands.pdf,
   * Position Hold Position). */
  static const uint8_t HOLD_HEIGHT = 8;
  static const uint8_t HOLD_TYPE = 12;
  const oncore_message_entry* entry = oncore_message_entry_at(message);
  const oncore_match_kind kind = entry->match_kind;
  uint8_t offset[3];
  uint8_t sent_start;
  uint8_t sent_count;
  uint8_t response_start;
  int16_t response_count;
  gps_match_result result = GPS_MATCH_DIFFERS;

  switch (kind) {
    case ONCORE_MATCH_ANY:
      return GPS_MATCH_ANSWERED;
    case ONCORE_MATCH_PASS:
      return GPS_MATCH_IGNORED;
    case ONCORE_MATCH_SAME:
      return index == message ? GPS_MATCH_ANSWERED : GPS_MATCH_OTHER;
    case ONCORE_MATCH_USER_DATUM:
      return index == GPS_REQUEST_USER_DATUM ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_CORRECTIONS:
      return index == GPS_REQUEST_CORRECTION_INPUT ? GPS_MATCH_ANSWERED
                                                   : GPS_MATCH_OTHER;
    case ONCORE_MATCH_EA:
      return index == GPS_REQUEST_POSITION_STATUS_8 ? GPS_MATCH_ANSWERED
                                                    : GPS_MATCH_OTHER;
    case ONCORE_MATCH_SECOND_AR:
      index = GPS_REQUEST_FIX_ALGORITHM_SECOND;
      break;
    case ONCORE_MATCH_ECHO:
    case ONCORE_MATCH_GMT_OFFSET:
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_HOLD_POSITION:
    case ONCORE_MATCH_ALTITUDE_HOLD_HEIGHT:
    case ONCORE_MATCH_FIX_MODE:
      break;
    default:
      return GPS_MATCH_OTHER;
  }
  if (index != message) {
    return GPS_MATCH_OTHER;
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
    case ONCORE_MATCH_ALTITUDE_HOLD_HEIGHT:
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
    return result;
  }
  return (uint8_t)nmea_field_equal(&sent[sent_start], sent_count,
                                   &response[response_start],
                                   response_count) == 1
             ? GPS_MATCH_ANSWERED
             : result;
}
