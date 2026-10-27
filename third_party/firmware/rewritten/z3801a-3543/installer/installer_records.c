/* installer/installer_records.c's functions that are this image's own: the rest
 * are common/installer/installer_records.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Reads DUART channel B's settings from the parameter record, or their
 * defaults; true when its checksum holds (installer::0x1081ac). */
uint8_t installer_parameter_record_read(void) {
  const bool valid =
      installer_record_checksum(&eeprom_parameter_record,
                                sizeof eeprom_parameter_record) ==
      eeprom_parameter_record.checksum;

  if (valid) {
    installer_duartb_settings.baud = eeprom_parameter_record.duartb_baud;
    installer_duartb_settings.parity = eeprom_parameter_record.duartb_parity;
    installer_duartb_settings.xon_xoff =
        eeprom_parameter_record.duartb_xon_xoff;
    installer_duartb_settings.field_05 =
        eeprom_parameter_record.duartb_field_05;
    installer_duartb_settings.eight_bits =
        eeprom_parameter_record.duartb_eight_bits;
    installer_duartb_settings.two_stop_bits =
        eeprom_parameter_record.duartb_two_stop_bits;
    installer_duartb_settings.echo = eeprom_parameter_record.duartb_echo;
  } else {
    installer_memcpy(&installer_duartb_settings,
                     &installer_duartb_settings_defaults,
                     sizeof installer_duartb_settings);
  }
  return valid;
}
