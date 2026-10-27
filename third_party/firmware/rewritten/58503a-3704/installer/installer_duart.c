/* The installer's copies of the DUART's shared routines (`devices/duart`;
 * duart.c has the primary's), which keep the installer's driver records
 * and call its routines.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bits does (installer::0x1039b4). */
void installer_duart_output_bits(board_led reset, board_led set) {
  const uint16_t sr = installer_enter_supervisor_mode();

  duart.stop_ropbc =
      (reset | installer_duart_shadow.output_reset) & DUART_OP_BITS;
  installer_duart_shadow.output_reset =
      (reset & DUART_OP_BITS | installer_duart_shadow.output_reset) &
      ~(set & DUART_OP_BITS);
  duart.start_sopbc = ~installer_duart_shadow.output_reset & DUART_OP_BITS;
  installer_set_interrupt_mask(sr);
}

/* installer_duart_output_bits; the installer's root task resets bit 2
 * with it (installer::0x100fbe, called at installer::0x100d90). */
void installer_output_bits_set(board_led reset, board_led set) {
  installer_duart_output_bits(reset, set);
}
