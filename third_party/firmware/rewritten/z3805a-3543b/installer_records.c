/* The installer's copies of the EEPROM records' readers (`services/records`;
 * records.c has the primary's), older than the primary's: the parameter record
 * gives only channel B's settings, and the identification record's tag is not
 * checked. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The sum and its complement of a record's bytes after its first two
 * (installer::0x108166). */
uint16_t installer_record_checksum(void* record, uint16_t length) {
  return records_checksum(record, length);
}

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

/* Sets the *IDN? model to the installer's default, 58000A
 * (installer::0x10822e). */
void installer_idn_model_default(void) {
  installer_strncpy(installer_idn_model, "58000A", sizeof installer_idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (installer::0x10824a). */
uint8_t installer_identification_record_read(void) {
  /* The identification record's default suffix. */
  static const char DEFAULT_SUFFIX = 'A';

  if (installer_record_checksum(&eeprom_identification_record,
                                sizeof eeprom_identification_record) !=
      eeprom_identification_record.checksum) {
    installer_idn_model_default();
    installer_strncpy(installer_idn_serial, "0", sizeof installer_idn_serial);
    installer_idn_suffix = DEFAULT_SUFFIX;
    return 0;
  }
  installer_strncpy(installer_idn_model, eeprom_identification_record.model,
                    sizeof installer_idn_model);
  installer_strncpy(installer_idn_serial, eeprom_identification_record.serial,
                    sizeof installer_idn_serial);
  installer_idn_suffix = eeprom_identification_record.suffix;
  return 1;
}
