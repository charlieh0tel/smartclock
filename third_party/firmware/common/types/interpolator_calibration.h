/* interpolator_calibration and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The time-interval counter's interpolator calibration, which
 * interpolator_calibrate steps: HDAC channels 2 and 3 trim the counter
 * until its readings at the two reference points fall in 1 to 25 and
 * 201 to 225, exactly 200 apart. */
typedef struct interpolator_calibration {
  uint8_t initialized; /* set with both DACs at 0x1f the first time */
  uint8_t calibrated;
  uint8_t hdac_2; /* HDAC channel 2's value */
  uint8_t hdac_3; /* HDAC channel 3's value */
  uint8_t result_valid;
  uint8_t result_hdac_2; /* the values that gave a spread of 200 */
  uint8_t result_hdac_3;
  uint8_t reading_low;  /* interpolator_measure(0), wanted 1 to 25 */
  uint8_t reading_high; /* interpolator_measure(1), wanted 201 to 225 */
} interpolator_calibration;
