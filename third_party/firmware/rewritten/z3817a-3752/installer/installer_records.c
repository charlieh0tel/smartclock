/* From the Z3815A 4010's rewritten/installer/installer_records.c, the Z3817A
 * 3752's own beside common/'s: of its functions, those the Z3817A's code
 * carries identical or relocated (docs/firmware/z3817a-3752.md, "The
 * function check").  Its comments' addresses are the Z3817A's, but where
 * they name another image.  Each keeps its generated prototype. */

/* Reads the SCI's settings, the installer's console's, from the
 * parameter record; else takes their defaults and returns 0.  When the
 * record is good it returns D3 as its caller left it, as the 58503A
 * 3704's, which reads DUART channel B's (installer::0x108372). */
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
