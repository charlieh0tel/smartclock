/* The installer's copies of the EEPROM records' readers (`services/records`;
 * records.c has the primary's), older than the primary's: the parameter record
 * gives only channel B's settings, and the identification record's tag is not
 * checked. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The sum and its complement of a record's bytes after its first two
 * (installer::0x10823a). */
uint16_t installer_record_checksum(void* record, uint16_t length) {
  return records_checksum(record, length);
}

/* Sets the *IDN? model to the installer's default, 58000A
 * (installer::0x10830e). */
void installer_idn_model_default(void) {
  installer_strncpy(installer_idn_model, "58000A", sizeof installer_idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (installer::0x10832a). */
uint8_t installer_identification_record_read(void) {
  if (installer_record_checksum(&eeprom_identification_record,
                                sizeof eeprom_identification_record) !=
      eeprom_identification_record.checksum) {
    installer_idn_model_default();
    installer_strncpy(installer_idn_serial, "0", sizeof installer_idn_serial);
    installer_idn_suffix = RECORDS_DEFAULT_SUFFIX;
    return 0;
  }
  installer_strncpy(installer_idn_model, eeprom_identification_record.model,
                    sizeof installer_idn_model);
  installer_strncpy(installer_idn_serial, eeprom_identification_record.serial,
                    sizeof installer_idn_serial);
  installer_idn_suffix = eeprom_identification_record.suffix;
  return 1;
}
