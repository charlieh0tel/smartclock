/* counter_work_state and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A counter_configuration_fields work's state: asked for, or done. */
typedef enum __attribute__((packed)) counter_work_state {
  COUNTER_WORK_DONE = 1,
  COUNTER_WORK_WANTED = 2,
} counter_work_state;

/* counter_configuration_fields.registers_mode: when counter_arm loads
 * fpga_registers; any other value never. */
typedef enum __attribute__((packed)) counter_registers_mode {
  COUNTER_REGISTERS_WHEN_WANTED = 1,
} counter_registers_mode;

/* counter_configuration_fields.calibrate_mode: when counter_read
 * calibrates the interpolator; any other value never. */
typedef enum __attribute__((packed)) counter_calibrate_mode {
  COUNTER_CALIBRATE_WHEN_WANTED = 1,
  COUNTER_CALIBRATE_ALWAYS = 3,
} counter_calibrate_mode;

/* counter_configuration_fields.sync_mode: when counter_arm syncs the
 * counter, as sync_request asks; any other value never. */
typedef enum __attribute__((packed)) counter_sync_mode {
  COUNTER_SYNC_WHEN_WANTED = 2,
} counter_sync_mode;
