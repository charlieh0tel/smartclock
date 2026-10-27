/* fpga_bit and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* An entry of fpga_bit_table: the FPGA register a bit is in and its
 * mask, which fpga_set_bit sets or clears in fconf_regs. */
typedef struct fpga_bit {
  uint8_t reg;
  uint8_t mask;
  uint8_t is_flag; /* 1: fpga_fields_to_registers sets mask for a nonzero
                      value; 0: ORs the value in */
  uint8_t field_3;
} fpga_bit;
