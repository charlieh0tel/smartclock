/* From the Z3815A 4010's rewritten/duartb.c, the 58503B 1.01.04's own beside
 * common/'s: DUART channel B's settings' restrictions, which the 58503B's
 * code carries relocated, on duartb_settings, the record their code
 * addresses, though this image has no DUART and nothing else refers to it
 * (docs/firmware/58503b-1.01.04.md, "The link list").  Its comments'
 * addresses are the 58503B's.  Each keeps its generated prototype. */

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x406a8). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x406ca). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}
