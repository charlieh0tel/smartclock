/* holdover_steering_state and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The holdover stage's steering, at holdover_steering: each 10-second block's
 * mean time interval, and the accumulators holdover_ti_add keeps of the
 * readings for a least-squares slope, which holdover_correction_valid
 * turns into an EFC correction. */
typedef struct holdover_steering_state {
  float ti_mean;    /* +0x00: the block's valid readings' sum over 10, scaled
                       up at its end if some failed */
  uint8_t readings; /* +0x04: the block's valid readings */
  uint8_t seconds;  /* +0x05: the block's seconds, to 10 */
  uint8_t field_06; /* +0x06: zeroed with the block; nothing reads it */
  uint8_t field_07;
  float aging_limit; /* +0x08: 2e-9, or 2e-9 a day as a rate a second once the
                        EFC history is full; nothing reads it */
  float ti_sum;      /* +0x0c */
  float ti_weighted_sum; /* +0x10: each reading times its index */
  float ti_largest;      /* +0x14: the largest reading's magnitude,
                            holdover_ti_max's */
  int32_t ti_count;      /* +0x18: at 10 or more the correction may be used */
} holdover_steering_state;
