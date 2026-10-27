/* installer/installer_records.c's functions that are this image's own:
 * the rest are common/installer/installer_records.c's, the same in every
 * image (docs/firmware/rewrite.md, "Shared C").  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Reads DUART channel B's settings from the parameter record; else takes
 * their defaults and returns 0.  When the record is good it returns D3
 * as its caller left it, as parameter_record_read_58503a does
 * (installer::0x108280). */
uint8_t installer_parameter_record_read(void) {
  uint8_t result = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");

  if (installer_record_checksum(&eeprom_parameter_record,
                                sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    installer_memcpy(&installer_duartb_settings,
                     &installer_duartb_settings_defaults,
                     sizeof installer_duartb_settings);
    return 0;
  }
  installer_duartb_settings.baud = eeprom_parameter_record.duartb_baud;
  installer_duartb_settings.parity = eeprom_parameter_record.duartb_parity;
  installer_duartb_settings.xon_xoff = eeprom_parameter_record.duartb_xon_xoff;
  installer_duartb_settings.field_05 = eeprom_parameter_record.duartb_field_05;
  installer_duartb_settings.eight_bits =
      eeprom_parameter_record.duartb_eight_bits;
  installer_duartb_settings.two_stop_bits =
      eeprom_parameter_record.duartb_two_stop_bits;
  installer_duartb_settings.echo = eeprom_parameter_record.duartb_echo;
  return result;
}
