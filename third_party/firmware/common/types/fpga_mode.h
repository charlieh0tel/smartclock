/* fpga_mode and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The FPGA's mode, fpga_bit_table entry 0, register 0's low two bits,
 * as fpga_set_field is given it: counter_arm's 0 and
 * interpolator_prepare's 3 are its only values
 * (docs/firmware/counter.md, "What is never written"). */
typedef enum __attribute__((packed)) fpga_mode {
  FPGA_MODE_COUNT = 0,      /* the count, started by the GPS 1PPS */
  FPGA_MODE_REFERENCES = 3, /* the interpolator's references */
} fpga_mode;

/* A register the FPGA reads back through fpga_read and fpga_read_byte,
 * by the number fpga_read puts in fpga_bit_table entry 4's address
 * bits.  The count is 24 bits, three bytes, each two registers'
 * nibbles (counter_read_raw counter_read_raw). */
typedef enum __attribute__((packed)) fpga_register {
  /* the counter's ready bits and the seconds (counter_wait_ready,
     fpga_read_seconds) */
  FPGA_REGISTER_STATUS = 0x00,
  FPGA_REGISTER_INTERPOLATOR = 0x10, /* interpolator_read */
  FPGA_REGISTER_COUNT_LOW = 0x12,
  FPGA_REGISTER_COUNT_MIDDLE = 0x14,
  FPGA_REGISTER_COUNT_HIGH = 0x16,
} fpga_register;

/* The reference interpolator_measure measures, fpga_bit_table entry 3. */
typedef enum __attribute__((packed)) interpolator_reference {
  INTERPOLATOR_REFERENCE_LOW = 0,
  INTERPOLATOR_REFERENCE_HIGH = 1,
} interpolator_reference;
