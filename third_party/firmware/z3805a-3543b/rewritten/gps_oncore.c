/* The GPS task's side of the Oncore's binary messages: framing them as
 * they arrive on DUART channel A, accounting for each request's outcome,
 * the argument encoders a command is built with, and the decoders that
 * unpack a response into gps (docs/firmware/gps.md; Motorola's
 * VPCommands.pdf for the messages).  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/gps/oncore`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* A response's index in oncore_messages by its ID, and its length;
 * 0, counted, for an ID not there (0x4b7ac). */
uint32_t gps_response_lookup(char first, char second, int16_t* length,
                             gps_request_code_word* index) {
  static const int16_t COUNT =
      sizeof oncore_messages / sizeof oncore_messages[0];

  for (int16_t i = 0; i < COUNT; i++) {
    /* An empty entry's id is null, so that this reads ROM 0 and 1, as
     * the image does (docs/firmware/quirks.md). */
    if (first == oncore_messages[i].id[0] &&
        second == oncore_messages[i].id[1]) {
      *index = i;
      *length = oncore_messages[i].length;
      return 1;
    }
  }
  gps_counts.unknown_response++;
  return 0;
}

/* Stores a byte that is the one expected and moves to the next state,
 * else starts again (0x4b800). */
void gps_receive_expect(uint8_t c, uint8_t expected) {
  if (c == expected) {
    gps_oncore_store(c);
    gps_receive_state++;
  } else {
    gps_oncore_restart(c);
  }
}

/* 1 once more time has passed since sent_time than the message's
 * timeout (0x4b9a6). */
uint32_t gps_response_timed_out(gps_request_code_word message,
                                uint32_t* sent_time) {
  int32_t elapsed;

  return uptime_elapsed(sent_time, &elapsed) &&
         (uint32_t)elapsed > oncore_messages[message].timeout;
}

/* Counts a request's outcome in gps_counts and gives it to its sender:
 * a monitor's in its result, another's sent back to plla_exchange; 1 if
 * it got there (0x4b9e6). */
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

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x4efb4). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* Decodes @@Ai's byte into record at its channel, 1 to 6 (0x4efd6). */
uint8_t* oncore_ai_decode(uint8_t* cursor, uint8_t* record) {
  const uint8_t channel = cursor[0];

  if (channel != 0 && channel <= GPS_CHANNELS) {
    byte_copy_optional(&cursor[1], &record[channel - 1]);
  }
  return cursor + 2;
}

/* Copies @@Ca's self-test result, if there is somewhere to put it, but
 * for its two spare bits, 14 and 15 (VPCommands.pdf, 6 Channel
 * Self-Test) (0x4f002). */
uint16_t* word_copy_14_bits(uint16_t* source, uint16_t* destination) {
  static const uint16_t TEST_BITS = 0x3fff;

  if (destination != 0) {
    *destination = *source & TEST_BITS;
  }
  return source + 1;
}

/* Decodes @@Ba, the position and status, when the engine is fixing
 * positions, else marks every channel searching; while a survey runs
 * and the fix is 3D, averages the position (0x4f59a). */
uint8_t* oncore_position_message(uint8_t* cursor, uint8_t* record) {
  if (gps.fix_mode != ONCORE_POSITION_FIX) {
    channels_reset();
    return cursor;
  }
  cursor = oncore_decode_fields(cursor, record, oncore_status_field_codes);
  gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
  if (gps.position_hold == 0 &&
      (nv_settings.douglas_duration_infinite == 1 ||
       gps.average_count < (uint32_t)nv_settings.douglas_duration) &&
      (gps.status.receiver_status & ONCORE_STATUS_3D_FIX) != 0) {
    running_average_update(gps.status.latitude, &gps.average_latitude,
                           gps.average_count);
    running_average_update(gps.status.longitude, &gps.average_longitude,
                           gps.average_count);
    running_average_update(gps.status.height_gps, &gps.average_height_gps,
                           gps.average_count);
    running_average_update(gps.status.height_msl, &gps.average_height_msl,
                           gps.average_count);
    gps.average_count++;
    gps.surveying = 1;
  }
  if (gps_status_print_interval != 0 &&
      ++gps_status_print_count >= gps_status_print_interval) {
    gps_status_print_count = 0;
    gps_debug_status();
  }
  status_message_arrived = 1;
  return cursor;
}

/* Decodes @@Bb, the visible satellites, and marks them received
 * (0x4f682). */
uint8_t* oncore_visibility_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_visibility_field_codes);
  gps.visibility_received = 1;
  return cursor;
}

/* Decodes @@Ce, the pseudorange corrections, and keeps the mean and
 * standard deviation, in meters, of those of the satellites used for
 * position (0x4f6d8). */
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

/* Decodes @@Bk, the position, status and data extension (0x4f842). */
uint8_t* oncore_extension_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_extension_field_codes);
}

/* Decodes @@Bn, the Time RAIM setup and status, and marks it arrived
 * (0x4f85c). */
uint8_t* oncore_traim_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_raim_field_codes);
  traim_message_arrived = 1;
  return cursor;
}

/* Decodes @@Cj, the receiver ID, and marks it received (0x4f87e). */
uint8_t* oncore_receiver_id_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_receiver_id_field_codes);
  gps.receiver_id_received = 1;
  return cursor;
}

/* Runs a decoder of oncore_messages.  The table holds the image's own
 * routines, which return the cursor in D0 alone, where gcc takes a
 * pointer result from A0; so it is called as returning a long. */
static uint8_t* gps_oncore_decode(const oncore_decoder* decoder,
                                  uint8_t* cursor) {
  typedef uint32_t decode_in_d0(uint8_t* cursor, uint8_t* record);

  return (uint8_t*)((decode_in_d0*)decoder->decode)(cursor, decoder->record);
}

/* Runs a response's decoders over it, past its "@@" and ID, with
 * preemption off, and gives @@Ba's time to status_snapshot_update; the
 * cursor past what they read (0x4faa2). */
uint8_t* gps_response_decode(gps_request_code_word message, uint8_t* response) {
  static const uint8_t DECODERS = sizeof oncore_messages[0].decoders /
                                  sizeof oncore_messages[0].decoders[0];
  const oncore_message_entry* entry = &oncore_messages[message];
  uint8_t* cursor;

  psos_mode(PSOS_MODE_NO_PREEMPTION, PSOS_MODE_NO_PREEMPTION, NULL);
  cursor = response + libc_length("@@") + libc_length(entry->id);
  for (uint8_t i = 0; i < DECODERS && entry->decoders[i].decode != NULL; i++) {
    cursor = gps_oncore_decode(&entry->decoders[i], cursor);
  }
  psos_mode(0, PSOS_MODE_NO_PREEMPTION, NULL);
  if (message == GPS_REQUEST_POSITION_STATUS) {
    status_snapshot_update(clock_time_bits(gps.status.time));
  }
  return cursor;
}

/* Whether a response answers the command sent, by the message's match
 * kind; for those that compare, whether its bytes after the ID are the
 * command's (0x4fb86). */
gps_match_result gps_response_match(gps_request_code_word message,
                                    uint8_t* sent, uint8_t length,
                                    gps_argument_mode mode,
                                    gps_request_code_word index,
                                    uint8_t* response,
                                    int16_t response_length) {
  /* @@Ab's offset: sign, hours, minutes. */
  static const uint8_t OFFSET_LENGTH = 3;
  static const uint8_t OFFSET_NEGATIVE = 0xff;
  /* @@Af's height, then its type; the response's GPS height, then its
   * MSL one. */
  static const uint8_t HEIGHT_LENGTH = 4;
  /* @@As's latitude, longitude, height and type, and the heights it
   * takes, in cm (VPCommands.pdf, Position Hold Position). */
  static const uint8_t HOLD_HEIGHT = 8;
  static const uint8_t HOLD_TYPE = 12;
  static const int32_t HOLD_HEIGHT_LOWEST = -100000;
  static const int32_t HOLD_HEIGHT_HIGHEST = 1800000;
  static const int32_t HOLD_HEIGHT_TOLERANCE = 2;
  const oncore_message_entry* entry = &oncore_messages[message];
  const oncore_match_kind kind = entry->match_kind;
  uint8_t offset[3];
  uint8_t sent_start;
  uint8_t sent_count;
  uint8_t response_start;
  int16_t response_count;

  switch (kind) {
    case ONCORE_MATCH_ANY:
      return GPS_MATCH_ANSWERED;
    case ONCORE_MATCH_SAME:
      return index == message ? GPS_MATCH_ANSWERED : GPS_MATCH_OTHER;
    case ONCORE_MATCH_USER_DATUM:
      return index == GPS_REQUEST_USER_DATUM ? GPS_MATCH_ANSWERED
                                             : GPS_MATCH_OTHER;
    case ONCORE_MATCH_CORRECTIONS:
      return index == GPS_REQUEST_CORRECTION_INPUT ? GPS_MATCH_ANSWERED
                                                   : GPS_MATCH_OTHER;
    case ONCORE_MATCH_ECHO:
    case ONCORE_MATCH_GMT_OFFSET:
    case ONCORE_MATCH_HEIGHT:
    case ONCORE_MATCH_HOLD_POSITION:
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
  response_start = libc_length(oncore_messages[index].id) + libc_length("@@");
  response_count = response_length - response_start;
  if (kind == ONCORE_MATCH_GMT_OFFSET) {
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
  } else if (kind == ONCORE_MATCH_HEIGHT) {
    if (sent[sent_start + HEIGHT_LENGTH] == ONCORE_HEIGHT_MSL) {
      response += HEIGHT_LENGTH;
    }
    sent_count = HEIGHT_LENGTH;
    response_count = HEIGHT_LENGTH;
  } else if (kind == ONCORE_MATCH_HOLD_POSITION &&
             sent[sent_start + HOLD_TYPE] ==
                 response[response_start + HOLD_TYPE]) {
    int32_t sent_height;
    int32_t response_height;

    /* bytes_to_long takes the long as unsigned. */
    bytes_to_long(&sent[sent_start + HOLD_HEIGHT], (uint32_t*)&sent_height);
    bytes_to_long(&response[response_start + HOLD_HEIGHT],
                  (uint32_t*)&response_height);
    if (sent_height >= HOLD_HEIGHT_LOWEST &&
        sent_height <= HOLD_HEIGHT_HIGHEST &&
        labs(sent_height - response_height) <= HOLD_HEIGHT_TOLERANCE) {
      sent_count = HOLD_HEIGHT;
      response_count = HOLD_HEIGHT;
    }
  }
  return bytes_equal_n(&sent[sent_start], sent_count, &response[response_start],
                       response_count) == 1
             ? GPS_MATCH_ANSWERED
             : GPS_MATCH_DIFFERS;
}
