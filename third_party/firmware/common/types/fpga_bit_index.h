/* The FPGA's register bits every image has, the same in each: the head
 * of each image's fpga_bit_index, whose smartclock.h includes this
 * header inside that enum, where the image's own bits follow
 * (docs/firmware/rewrite.md, "Shared C"). */
/* clang-format off */
  FPGA_BIT_MODE = 0,   /* register 0, 0x03: an fpga_mode */
  FPGA_BIT_PRESET = 1, /* register 0, 0x40: set around arming; counter_selftest
                          reads a count of 0 under it */
  FPGA_BIT_START = 2,  /* register 1, 0x01: pulsed by interpolator_measure */
  FPGA_BIT_REFERENCE = 3, /* register 1, 0x02: an interpolator_reference */
  FPGA_BIT_ADDRESS = 4, /* register 1, 0xf8: the address bits fpga_read uses */
    /* clang-format on */
