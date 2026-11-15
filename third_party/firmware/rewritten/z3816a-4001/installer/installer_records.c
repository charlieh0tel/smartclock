/* installer_records.c's functions that are this image's own: the rest are
 * common/installer/installer_records.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the Z3815A 4010's
 * rewritten/installer/installer_records.c: of its functions, those the
 * Z3816A's code carries (identical or relocated by image_diff, or changed with
 * the same instructions), with the static helpers they use, each the same here
 * but where its comment says otherwise (docs/firmware/z3816a-4001.md, "The
 * link check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Reads the SCI's settings, the installer's console's, from the
 * parameter record; else takes their defaults and returns 0.  When the
 * record is good it returns D3 as its caller left it, as the 58503A
 * 3704's, which reads DUART channel B's (installer::0x108356). */
uint8_t installer_parameter_record_read(void) {
  uint8_t result = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");

  if (installer_record_checksum(&eeprom_parameter_record,
                                sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    installer_memcpy(&installer_sci_settings, &installer_sci_settings_defaults,
                     sizeof installer_sci_settings);
    return 0;
  }
  installer_sci_settings.baud = eeprom_parameter_record.sci_baud;
  installer_sci_settings.parity = eeprom_parameter_record.sci_parity;
  installer_sci_settings.xon_xoff = eeprom_parameter_record.sci_xon_xoff;
  installer_sci_settings.echo = eeprom_parameter_record.sci_echo;
  return result;
}
