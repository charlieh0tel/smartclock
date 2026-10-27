/* counter_error and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A time-interval reading's error bits, which counter_read reports:
 * "Gate failure", "Result uncomputable", "Measurement aborted",
 * "Interpolator calibration failure". */
typedef enum __attribute__((packed)) counter_error {
  COUNTER_GATE_FAILURE = 0x01,
  COUNTER_RESULT_UNCOMPUTABLE = 0x04,
  COUNTER_MEASUREMENT_ABORTED = 0x08,
  COUNTER_CALIBRATION_FAILURE = 0x10
} counter_error;
