/* fpga_chip and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The chip of a configuration fpga_verify_chip reads back: the first
 * through port F bit 6, the second through bit 7, its bytes 0xf0d into
 * the configuration. */
typedef enum __attribute__((packed)) fpga_chip {
  FPGA_CHIP_FIRST = 1,
  FPGA_CHIP_SECOND = 2,
} fpga_chip;

/* A configuration's bitstream in ROM, from start to end. */
typedef struct fpga_configuration_entry {
  uint8_t* start;
  uint8_t* end;
} fpga_configuration_entry;

/* The FPGA's configurations, at fpga_configurations: the highest index the
 * loader and the checks take (cmp.b and bls at fpga_program+0x18,
 * fpga_verify+0x4 and fpga_load_test+0x4), 1, though only entry 0 is a
 * configuration and entry 1 would be read from the second bitstream's first
 * bytes. */
typedef struct fpga_configuration_table {
  uint8_t last; /* +0x00 */
  uint8_t field_01;
  fpga_configuration_entry entries[1]; /* +0x02 */
} fpga_configuration_table;
