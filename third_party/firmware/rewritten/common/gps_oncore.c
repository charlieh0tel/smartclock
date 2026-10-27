/* The functions of gps_oncore.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_oncore.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* A message's two-character ID, after its "@@". */
static const uint32_t GPS_ONCORE_ID_LENGTH = 2;

/* Points gps_response_cursor back at gps_response_buffer's start and
 * expects a message's first @. */
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

/* The XOR of the bytes from start up to end. */
uint8_t oncore_checksum(uint8_t* start, uint8_t* end) {
  uint8_t sum = 0;

  for (; start < end; start++) {
    sum ^= *start;
  }
  return sum;
}

/* Reads the bytes the engine has sent, framing a message from its @@
 * into gps_response_buffer, until none is left or a message is complete;
 * the framing state. */
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

/* A request's argument by its index. */
int32_t long_array_get(int32_t* array, uint8_t index) { return array[index]; }

/* Sets a request's argument by its index. */
void long_array_set(int32_t value, int32_t* array, uint8_t index) {
  array[index] = value;
}

/* The argument encoders a message's descriptor names, which
 * gps_request_build calls with the arguments, an index and where the
 * command's bytes go, and which return where the next goes. */

/* An argument's low byte. */
uint8_t* long_array_get_byte(int32_t* array, uint8_t index, uint8_t* out) {
  *out = (uint8_t)long_array_get(array, index);
  return out + 1;
}

/* An argument's low word, high byte first. */
uint8_t* long_array_get_word(int32_t* array, uint8_t index, uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
  return out + 2;
}

/* An argument, high byte first. */
uint8_t* long_array_get_long(int32_t* array, uint8_t index, uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
  return out + 4;
}

/* A zero byte, then an argument, high byte first. */
uint8_t* long_array_get_long_prefixed(int32_t* array, uint8_t index,
                                      uint8_t* out) {
  *out = 0;
  return long_array_get_long(array, index, out + 1);
}

/* The decoders a message's descriptor names, which gps_response_decode
 * calls with the cursor in the response and their record, and which
 * return the cursor past what they read. */

/* Copies a byte, if there is somewhere to put it. */
uint8_t* byte_copy_optional(uint8_t* source, uint8_t* destination) {
  if (destination != NULL) {
    *destination = *source;
  }
  return source + 1;
}

/* Copies a word. */
uint16_t* word_copy_advance(uint16_t* source, uint16_t* destination) {
  *destination = *source;
  return source + 1;
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

/* Folds a value into the average of count values before it. */
void running_average_update(int32_t value, double* average, uint32_t count) {
  double total = double_multiply_pointer(unsigned_to_double(count), average);

  total = double_add(integer_to_double(value), total);
  *average = double_divide_reversed(unsigned_to_double(count + 1), total);
}

/* Decodes @@Bc, the DOP table. */
uint8_t* oncore_dop_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_xdop_table_field_codes);
}

/* Decodes @@Bd, the almanac status. */
uint8_t* oncore_almanac_status_message(uint8_t* cursor, uint8_t* record) {
  return oncore_decode_fields(cursor, record, oncore_almanac_field_codes);
}
