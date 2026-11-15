/* The Oncore messages' decoding by field programs (docs/firmware/gps.md,
 * "Decoding"), the same in each image that includes this file
 * (docs/firmware/rewrite.md, "Shared C", "A file for some images"): the
 * decoder and two messages it decodes whole.  An image that decodes its
 * receiver's messages otherwise leaves the file out.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

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
