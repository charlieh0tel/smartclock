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
