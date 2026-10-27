/* oncore_field_code and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A byte of the field programs oncore_decode_fields runs to unpack a
 * message into its record, oncore_status_field_codes to
 * oncore_receiver_id_field_codes: docs/firmware/gps.md,
 * "Decoding".  1 to 0x7f copies that many bytes, and 0xc1 to 0xff
 * starts a group repeated 0x100 less that many times, 63 to 1; the
 * others, 0x86 to 0xc0, do nothing. */
typedef enum __attribute__((packed)) oncore_field_code {
  ONCORE_FIELD_SKIP_DESTINATION = 0x00, /* leaves a record byte */
  ONCORE_FIELD_END = 0x80,
  ONCORE_FIELD_GROUP_END = 0x81,   /* back to the group's start, or on */
  ONCORE_FIELD_SKIP_SOURCE = 0x82, /* drops a message byte */
  ONCORE_FIELD_SKIP_ZERO = 0x83,   /* drops a message byte, writes zero */
  ONCORE_FIELD_ZERO = 0x84,        /* writes a zero byte */
  ONCORE_FIELD_SIGN = 0x85,        /* the next message byte's sign extension */
  ONCORE_FIELD_GROUP_FIRST = 0xc1, /* the longest group, 63 times */
} oncore_field_code;
