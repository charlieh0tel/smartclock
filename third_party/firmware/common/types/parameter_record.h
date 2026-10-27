/* parameter_record and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The parameter record at 0x400000: save_parameter_record's copy of
 * the two ports' settings. */
typedef struct parameter_record {
  uint16_t checksum;            /* +0x00 */
  uint8_t duartb_baud;          /* +0x02: duartb_settings' +2 to +8, in order */
  uint8_t duartb_parity;        /* +0x03 */
  uint8_t duartb_xon_xoff;      /* +0x04 */
  uint8_t duartb_field_05;      /* +0x05 */
  uint8_t duartb_eight_bits;    /* +0x06 */
  uint8_t duartb_two_stop_bits; /* +0x07 */
  uint8_t duartb_echo;          /* +0x08 */
  uint8_t sci_baud;             /* +0x09: sci_settings' +2 to +5, in order */
  uint8_t sci_parity;           /* +0x0a */
  uint8_t sci_xon_xoff;         /* +0x0b */
  uint8_t sci_echo;             /* +0x0c */
  uint8_t unused[3];            /* +0x0d: left erased */
} parameter_record;
