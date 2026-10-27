/* gps_framing_state and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* gps_receive_state: where gps_receive is in framing an Oncore message
 * into gps_response_buffer, its jump table at gps_receive+0x4c (docs/firmware/
 * gps.md, "Framing").  A state above GPS_FRAMING_COMPLETE drops each
 * byte (gps_receive+0x3e). */
typedef enum __attribute__((packed)) gps_framing_state {
  GPS_FRAMING_FIRST_AT = 0,  /* expects the first @ */
  GPS_FRAMING_SECOND_AT = 1, /* expects the second @ */
  GPS_FRAMING_ID_FIRST = 2,  /* an uppercase letter */
  GPS_FRAMING_ID_SECOND = 3, /* a letter; the ID's length from the table */
  GPS_FRAMING_BODY = 4,      /* the rest, the checksum last */
  GPS_FRAMING_CR = 5,
  GPS_FRAMING_LF = 6,
  GPS_FRAMING_COMPLETE = 7, /* gps_receive reads no more */
} gps_framing_state;
