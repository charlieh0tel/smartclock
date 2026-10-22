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

/* A message's two-character ID, after its "@@". */
static const uint32_t GPS_ONCORE_ID_LENGTH = 2;

/* A string's length; the image has no strlen and counts in line. */
static uint32_t gps_oncore_length(const char* s) {
  uint32_t length = 0;

  while (s[length] != 0) {
    length++;
  }
  return length;
}

/* Points gps_response_cursor back at gps_response_buffer's start and
 * expects a message's first @ (0x4b79a). */
void gps_response_reset(void) {
  gps_response_cursor = gps_response_buffer;
  gps_receive_state = GPS_FRAMING_FIRST_AT;
}

/* Stores a received byte at gps_response_cursor. */
static void gps_oncore_store(uint8_t c) { *gps_response_cursor++ = c; }

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

/* A response's index in oncore_messages by its ID, and its length;
 * 0, counted, for an ID not there (0x4b7ac). */
uint32_t gps_response_lookup(char first, char second, int16_t* length,
                             int16_t* index) {
  static const int16_t COUNT =
      sizeof oncore_messages / sizeof oncore_messages[0];

  for (int16_t i = 0; i < COUNT; i++) {
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

/* The XOR of the bytes from start up to end (0x4f8a0). */
uint8_t oncore_checksum(uint8_t* start, uint8_t* end) {
  uint8_t sum = 0;

  for (; start < end; start++) {
    sum ^= *start;
  }
  return sum;
}

/* Reads the bytes the engine has sent, framing a message from its @@
 * into gps_response_buffer, until none is left or a message is complete;
 * the framing state (0x4b846). */
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

/* 1 once more time has passed since sent_time than the message's
 * timeout (0x4b9a6). */
uint32_t gps_response_timed_out(int16_t message, uint32_t* sent_time) {
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

/* A message as text for the command log: its "@@" and ID, each byte
 * after them in hex, and "<cr><lf>"; empty unless it ends in CR LF.  Its
 * length (0x4fdca). */
int32_t gps_message_to_text(char* message, uint8_t length, char* text) {
  const uint32_t header_length = gps_oncore_length("@@") + GPS_ONCORE_ID_LENGTH;
  const char* end = message + (length - 2);

  if (message[0] == 0 || end[0] != '\r' || end[1] != '\n') {
    text[0] = 0;
  } else {
    char header[5];
    char digits[5];
    char hex[54];

    memcpy(header, message, header_length);
    header[header_length] = 0;
    hex[0] = 0;
    for (const char* byte = message + header_length; byte < end; byte++) {
      char* shown = digits;

      sprintf(digits, "%hx", (uint32_t)(uint8_t)*byte);
      if (gps_oncore_length(digits) < 2) {
        digits[2] = 0;
        digits[1] = digits[0];
        digits[0] = '0';
      } else {
        /* Never more than two digits for a byte, so never trimmed. */
        while (gps_oncore_length(shown) >= 3 &&
               (*shown == '0' || *shown == 'f')) {
          shown++;
        }
      }
      strcat(hex, shown);
    }
    sprintf(text, "%s%s<cr><lf>", header, hex);
  }
  return gps_oncore_length(text);
}

/* A request's argument by its index (0x4ee8c). */
int32_t long_array_get(int32_t* array, uint8_t index) { return array[index]; }

/* Sets a request's argument by its index (0x4eeac). */
void long_array_set(int32_t value, int32_t* array, uint8_t index) {
  array[index] = value;
}

/* The argument encoders a message's descriptor names, which
 * gps_request_build calls with the arguments, an index and where the
 * command's bytes go, and which return where the next goes. */

/* An argument's low byte (0x4eec2). */
char* long_array_get_byte(int32_t* array, uint8_t index, char* out) {
  *out = (char)long_array_get(array, index);
  return out + 1;
}

/* An argument's low word, high byte first (0x4eee0). */
char* long_array_get_word(int32_t* array, uint8_t index, char* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (char)(value >> 8);
  out[1] = (char)value;
  return out + 2;
}

/* An argument, high byte first (0x4ef02). */
char* long_array_get_long(int32_t* array, uint8_t index, char* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (char)(value >> 24);
  out[1] = (char)(value >> 16);
  out[2] = (char)(value >> 8);
  out[3] = (char)value;
  return out + 4;
}

/* The decoders a message's descriptor names, which gps_response_decode
 * calls with the cursor in the response and their record, and which
 * return the cursor past what they read. */

/* Copies a word (0x4ef70). */
uint16_t* word_copy_advance(uint16_t* source, uint16_t* destination) {
  *destination = *source;
  return source + 1;
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

/* Unpacks a message into its record by a field program, oncore_field_code
 * bytes (docs/firmware/gps.md, "Decoding"); the cursor past what it read
 * (0x4f02a). */
uint8_t* oncore_decode_fields(uint8_t* source, uint8_t* destination,
                              uint8_t* program) {
  static const uint8_t SIGN_BIT = 0x80;
  int8_t repeats = 1;
  const char* group = program;
  const char* op = program;

  for (;;) {
    uint8_t code = (uint8_t)*op;

    if (code == ONCORE_FIELD_END) {
      return source;
    }
    if (code == ONCORE_FIELD_GROUP_END && --repeats > 0) {
      op = group;
      continue;
    }
    if (code > ONCORE_FIELD_SKIP_DESTINATION && code < ONCORE_FIELD_END) {
      /* The count is read again for each byte, as the image does, so a
       * record laid over the program ends the copy where it has written
       * over it. */
      for (uint8_t i = 1; i <= *op; i++) {
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

/* Folds a value into the average of count values before it (0x47de2). */
void running_average_update(int32_t value, double* average, uint32_t count) {
  double total = double_multiply_pointer(unsigned_to_double(count), average);

  total = double_add(integer_to_double(value), total);
  *average = double_divide_reversed(unsigned_to_double(count + 1), total);
}

/* Decodes @@Ba, the position and status, when the engine is fixing
 * positions, else marks every channel searching; while a survey runs
 * and the fix is 3D, averages the position (0x4f59a). */
uint8_t* oncore_position_message(uint8_t* cursor, uint8_t* record) {
  if (gps.fix_mode != ONCORE_POSITION_FIX) {
    channels_reset();
    return cursor;
  }
  /* oncore_decode_fields takes the program's bytes as char. */
  cursor =
      oncore_decode_fields(cursor, record, (char*)oncore_status_field_codes);
  gps_receiver_status_update(0, 0);
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
  /* oncore_decode_fields takes the program's bytes as char. */
  uint8_t* next = oncore_decode_fields(cursor, record,
                                       (char*)oncore_corrections_field_codes);

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
