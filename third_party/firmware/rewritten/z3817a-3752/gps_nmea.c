/* From the Z3815A 4010's rewritten/gps_nmea.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* An angle's hemisphere, 'N' or 'S' for a latitude, 'E' or 'W' for a
 * longitude, by its sign (0x4e216). */
uint32_t nmea_hemisphere(int32_t mas, char longitude) {
  if (longitude == 0) {
    return mas < 0 ? 'S' : 'N';
  }
  return mas < 0 ? 'W' : 'E';
}

/* A field's character: a letter, digit, punctuation or space, but none
 * of NMEA's reserved "$*,!\^~" (0x5622e). */
uint32_t nmea_field_char(uint8_t c) {
  static const uint8_t PRINTING = LIBC_CTYPE_UPPER | LIBC_CTYPE_LOWER |
                                  LIBC_CTYPE_DIGIT | LIBC_CTYPE_PUNCTUATION |
                                  LIBC_CTYPE_BLANK;

  if ((ctype_table[c + 1] & PRINTING) == 0) {
    return 0;
  }
  for (const char* reserved = "$*,!\\^~"; *reserved != c; reserved++) {
    if (*reserved == '\0') {
      return 1;
    }
  }
  return 0;
}

/* The length of the field at text: its field characters to the first
 * that is not one (0x56260). */
int32_t nmea_field_length(uint8_t* text) {
  uint8_t* end = text;

  while (*end != '\0' && nmea_field_char(*end) != 0) {
    end++;
  }
  return end - text;
}

/* 1 if two fields are the same and not empty, each of the length
 * given, or of nmea_field_length's for a negative one (0x56284). */
uint32_t nmea_field_equal(uint8_t* a, uint16_t a_length, uint8_t* b,
                          uint16_t b_length) {
  int16_t left = a_length;

  if (left < 0) {
    left = (uint8_t)nmea_field_length(a);
  }
  if ((int16_t)b_length < 0) {
    b_length = (uint8_t)nmea_field_length(b);
  }
  if (left != (int16_t)b_length || left == 0) {
    return 0;
  }
  while (*a == *b) {
    if (--left <= 0) {
      return 1;
    }
    a++;
    b++;
  }
  return 0;
}

/* Puts @@Az's cable delay argument, in ns, at its index, the image's
 * pps_cable_delay_offset added, at the next, and encodes that as a long
 * (0x56034).  It writes into the arguments it is given. */
uint8_t* oncore_cable_delay_encode(int32_t* arguments, uint8_t index,
                                   uint8_t* out) {
  long_array_set(pps_cable_delay_offset + long_array_get(arguments, index),
                 arguments, index + 1);
  return long_array_get_long(arguments, index + 1, out);
}

/* Decodes @@Ai's byte into record at its channel, 1 to gps.channel_count
 * (0x560dc). */
uint8_t* oncore_ai_decode(uint8_t* cursor, uint8_t* record) {
  const uint8_t channel = cursor[0];

  if (channel != 0 && channel <= gps.channel_count) {
    byte_copy_optional(&cursor[1], &record[channel - 1]);
  }
  return cursor + 2;
}

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x5610a). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* Decodes @@Az's cable delay, keeping it as given and, the image's
 * pps_cable_delay_offset taken off, in record (0x5612c). */
uint8_t* oncore_cable_delay_decode(uint8_t* cursor, int32_t* record) {
  uint32_t delay = 0;

  for (uint8_t i = 0; i < sizeof delay; i++) {
    delay = delay << CPU_BYTE_BITS | *cursor++;
  }
  gps.pps_cable_delay_given = delay;
  *record = delay - pps_cable_delay_offset;
  return cursor;
}

/* Copies @@Ca's self-test result, if there is somewhere to put it,
 * keeping its two spare bits, 14 and 15, as the 58503A's does
 * (0x5615e). */
uint16_t* word_copy_14_bits(uint16_t* source, uint16_t* destination) {
  uint8_t* bytes = (uint8_t*)source;
  uint16_t word = bytes[0] << CPU_BYTE_BITS | bytes[1];

  if (destination != 0) {
    *destination = word;
  }
  return source + 1;
}

/* The second @@Ar entry's send filter (oncore_messages entry 0x38):
 * while gps.field_592's software 8.4 is set, its bit 2, where the
 * 58503A's is bit 7 (0x56fbc). */
uint8_t oncore_filter_software_8_4(void) {
  return (gps.field_592 & GPS_FIELD_592_SOFTWARE_8_4) != 0;
}

/* Whether an @@As position hold height sent, four bytes high first, is
 * one the Oncore takes, -100000 to 1800000 cm, and within 2 cm of the
 * one it answered: 1 if so, else 0 (0x562ea).  gps_response_match calls
 * it for the heights' and the position hold's echoes (0x5740c, 0x57436;
 * gps_oncore.c), where the 58503A 3704's makes the test in line. */
uint32_t oncore_hold_heights_match(uint8_t* sent, uint8_t* response) {
  /* The heights the Oncore takes, in cm (VPCommands.pdf, Position Hold
   * Position), and how far the answer may be from the height sent. */
  static const int32_t HOLD_HEIGHT_LOWEST = -100000;
  static const int32_t HOLD_HEIGHT_HIGHEST = 1800000;
  static const int32_t HOLD_HEIGHT_TOLERANCE = 2;
  int32_t sent_height;
  int32_t response_height;

  /* bytes_to_long takes the long as unsigned. */
  bytes_to_long(sent, (uint32_t*)&sent_height);
  bytes_to_long(response, (uint32_t*)&response_height);
  return sent_height >= HOLD_HEIGHT_LOWEST &&
         sent_height <= HOLD_HEIGHT_HIGHEST &&
         labs(sent_height - response_height) <= HOLD_HEIGHT_TOLERANCE;
}

/* Points gps_response_cursor back at gps_response_buffer's start and
 * has the framer wait for an @ (0x52158). */
void gps_response_reset(void) {
  gps_response_cursor = gps_response_buffer;
  gps_receive_state = GPS_FRAMING_FIRST_AT;
}

/* The text after a receiver ID line's label, for the line of a field
 * whose text starts with its label; NULL for none (0x56dc6). */
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
