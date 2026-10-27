/* The EEPROM's parameter, identification, lifetime and calibration
 * records, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `services/records`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  docs/firmware/structures.md
 * describes the records ("EEPROM records", "The lifetime record"). */

/* The sum and its complement of a record's bytes after its first two. */
uint16_t record_checksum(void* record, uint16_t length) {
  return records_checksum(record, length);
}

/* Writes lifetime to the lifetime record when it differs; 1 when the
 * record holds it. */
uint32_t lifetime_record_write(void) {
  lifetime_record record;

  memset(&record, RECORDS_ERASED, sizeof record);
  record.count = lifetime.count;
  record.date = lifetime.date;
  record.efc = lifetime.efc;
  record.checksum = record_checksum(&record, sizeof record);
  if (memcmp(&record, &eeprom_lifetime_record, sizeof record) == 0) {
    return 1;
  }
  return eeprom_write((char*)&eeprom_lifetime_record, (char*)&record,
                      sizeof record);
}
