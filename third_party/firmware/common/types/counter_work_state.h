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

/* The time-interval counter's configuration, counter_configuration, which
 * counter_arm, counter_calibrate and counter_read are given.  A mode and
 * a state go in pairs: the mode says when the work is done, the state
 * asks for it or says it is done.
 * 0x20 bytes, interpolator_calibration_state after them. */
typedef struct counter_configuration_fields {
  uint8_t fpga_registers[4]; /* counter_arm copies them into fconf_regs */
  double calibration;        /* s, which counter_read subtracts */
  counter_registers_mode registers_mode; /* when fpga_registers load */
  counter_work_state registers_state;
  counter_calibrate_mode calibrate_mode; /* when the interpolator calibrates */
  counter_work_state calibrate_state;
  counter_sync_mode sync_mode;   /* when counter_arm syncs */
  counter_work_state sync_state; /* sync_request sets both */
  counter_sync_kind sync_kind;   /* sync_request's first; into fconf_regs[3] */
  uint8_t pad_13;
  int32_t sync_count;          /* sync_request's second; counted down */
  int32_t interpolator_offset; /* reading_low, subtracted from the reading */
  int32_t interpolator_scale;  /* the spread it is divided by */
} counter_configuration_fields;
