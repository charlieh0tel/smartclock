/* The installer's copies of the DUART's shared routine (`devices/duart`;
 * duart.c has the primary's), which keep the installer's driver records and
 * call its routines. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Sets the GP port's bits 2 to 4 to a selection, 0 to 6, and returns
 * it shifted there (installer::0x102ff4). */
int32_t installer_gp_port_select(char selection) {
  return duart_gp_port_select(selection);
}

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bits does (installer::0x1039e0). */
void installer_duart_output_bits(board_led reset, board_led set) {
  /* The output port's bits 2 to 7, by duart_shadow.output_reset. */
  static const uint8_t OP_BITS = 0xfc;
  const uint16_t sr = installer_set_interrupt_mask(CPU_IPL_6_UNSHIFTED);

  duart.stop_ropbc = (reset | installer_duart_shadow.output_reset) & OP_BITS;
  installer_duart_shadow.output_reset =
      (reset & OP_BITS | installer_duart_shadow.output_reset) &
      ~(set & OP_BITS);
  duart.start_sopbc = ~installer_duart_shadow.output_reset & OP_BITS;
  installer_set_interrupt_mask(sr);
}
