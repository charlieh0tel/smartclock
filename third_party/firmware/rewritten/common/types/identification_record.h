/* identification_record and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The identification record at 0x400010: identification_record_write's
 * model, serial and suffix, and "S". */
typedef struct identification_record {
  uint16_t checksum; /* +0x00 */
  char model[18];    /* +0x02: idn_model */
  char serial[18];   /* +0x14: idn_serial */
  char suffix;       /* +0x26: idn_suffix */
  char tag[3];       /* +0x27: "S" */
  uint8_t unused[6]; /* +0x2a: left erased */
} identification_record;

/* The lifetime record at 0x400060, lifetime's date, count and EFC in
 * that order. */
typedef struct lifetime_record {
  uint16_t checksum;  /* +0x00 */
  uint32_t date;      /* +0x02 */
  int32_t count;      /* +0x06 */
  float efc;          /* +0x0a */
  uint8_t unused[18]; /* +0x0e: left erased */
} lifetime_record;
