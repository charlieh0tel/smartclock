/* What the time-interval counter's files share (ti.c and each image's
 * own ti.c): SCPI's not-a-number, the FPGA's status bits, the hex DAC's
 * slots and range, the calibration's tries, the interpolator's scale, the
 * preset bit's value.  The configuration's register loads are each image's
 * own ti.c's, since the Z3815A 4010 lays the configuration out
 * otherwise.  The exported C
 * includes the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* SCPI's not-a-number, a reading's value on an error. */
static const double TI_NOT_A_NUMBER = 9.91e37;

/* The FPGA's status register, 0, the bits counter_wait_ready waits for:
 * counter_read waits for the count, then the interpolation, and
 * interpolator_measure for the interpolation alone. */
enum {
  TI_STATUS_INTERPOLATED = 0x01,
  TI_STATUS_COUNTED = 0x02,
};

/* The hex DAC's slots, hdac_set's second, that trim the interpolator's
 * low and high references. */
enum {
  TI_HDAC_SLOT_LOW = 2,
  TI_HDAC_SLOT_HIGH = 3,
};

/* Both DACs' start, the middle of their 6-bit range, and its top. */
static const uint8_t TI_HDAC_START = 0x1f;

static const uint8_t TI_HDAC_MAXIMUM = 0x3f;

static const int32_t TI_SPREAD = 200;

/* The tries counter_calibrate and interpolator_selftest_calibrate give
 * interpolator_calibrate. */
static const int32_t TI_CALIBRATE_TRIES = 20;

/* The nonzero the image sets the preset bit with, through fpga_set_bit
 * and counter_configuration_init's fields. */
static const uint8_t TI_PRESET_ON = 8;
