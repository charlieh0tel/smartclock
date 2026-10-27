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

/* Decodes @@As's height and its type, the position hold position's
 * last fields (0x5001c). */
uint8_t* oncore_as_height_decode(uint8_t* cursor, uint8_t* record) {
  /* bytes_to_long takes the long as unsigned, byte_copy_optional the
   * type as a byte. */
  cursor = bytes_to_long(cursor, (uint32_t*)&gps.hold_position.height);
  return byte_copy_optional(cursor, (uint8_t*)&gps.hold_height_type);
}

/* Decodes @@Bb, the visible satellites, and marks them received
 * (0x50716). */
uint8_t* oncore_visibility_message(uint8_t* cursor, uint8_t* record) {
  cursor = oncore_decode_fields(cursor, record, oncore_visibility_field_codes);
  gps.visibility_received = 1;
  return cursor;
}

/* Decodes @@Cj, the receiver ID, and marks it received (0x50968). */
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

/* Reads the bytes the engine has sent, framing a message from its @@
 * into gps_response_buffer, until none is left or a message is complete;
 * the framing state (0x4c124). */
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
