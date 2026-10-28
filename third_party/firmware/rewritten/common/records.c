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

/* Reads the lifetime record into lifetime, or its defaults; true when its
 * checksum holds. */
uint8_t lifetime_record_read(void) {
  /* The lifetime record's default date: 1994-01-01 (year << 16 |
   * month << 8 | day); its EFC is PLL_EFC_MID_SCALE. */
  static const uint32_t LIFETIME_DEFAULT_DATE = 1994u << 16 | 1u << 8 | 1u;
  /* An EFC in the record with these bits all set, as an erased
   * record's 0xffffffff has, is taken as mid-scale. */
  static const uint32_t LIFETIME_EFC_INVALID_BITS = 0xe8000000u;

  if (record_checksum(&eeprom_lifetime_record, sizeof eeprom_lifetime_record) !=
      eeprom_lifetime_record.checksum) {
    lifetime.date = LIFETIME_DEFAULT_DATE;
    lifetime.count = 0;
    lifetime.efc = PLL_EFC_MID_SCALE;
    return 0;
  }
  lifetime.date = eeprom_lifetime_record.date;
  lifetime.count = eeprom_lifetime_record.count;

  /* The image tests the float's bits. */
  uint32_t efc_bits;
  float efc = eeprom_lifetime_record.efc;
  memcpy(&efc_bits, &efc, sizeof efc_bits);
  if ((efc_bits & LIFETIME_EFC_INVALID_BITS) == LIFETIME_EFC_INVALID_BITS) {
    lifetime.efc = PLL_EFC_MID_SCALE;
  } else {
    lifetime.efc = efc;
  }
  return 1;
}
