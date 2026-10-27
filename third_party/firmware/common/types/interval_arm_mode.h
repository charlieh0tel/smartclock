/* interval_arm_mode and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* How interval_rearm, and interval_next_reading after its
 * reading, arm the next: calibrating the interpolator first, plainly,
 * or syncing the counter to the 1PPS first (interval_next_reading+0xb2,
 * interval_next_reading+0xc6). */
typedef enum __attribute__((packed)) interval_arm_mode {
  INTERVAL_ARM_CALIBRATE = 0,
  INTERVAL_ARM_PLAIN = 1,
  INTERVAL_ARM_SYNC = 2,
} interval_arm_mode;
