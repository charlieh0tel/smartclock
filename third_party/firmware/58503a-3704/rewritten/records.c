/* The EEPROM's parameter, identification, lifetime and calibration
 * records, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `services/records`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  docs/firmware/structures.md
 * describes the records ("EEPROM records", "The lifetime record"). */

/* The serial ports' settings by default, 9600 baud, no parity, eight bits,
 * echo: the image's sci_settings_defaults and duartb_settings_defaults
 * (0x30a2c, 0x30a34). */
static const sci_settings_record records_sci_defaults = {
    .baud = SERIAL_BAUD_9600,
    .parity = SERIAL_PARITY_NONE,
    .echo = 1,
};

static const duartb_settings_record records_duartb_defaults = {
    .baud = SERIAL_BAUD_9600,
    .parity = SERIAL_PARITY_NONE,
    .eight_bits = 1,
    .echo = 1,
};

/* The sum and its complement of a record's bytes after its first two
 * (0x3071c). */
uint16_t record_checksum(void* record, uint16_t length) {
  return records_checksum(record, length);
}

/* Reads the lifetime record into lifetime, or its defaults; true when its
 * checksum holds (0x30934). */
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

/* Writes lifetime to the lifetime record when it differs; 1 when the
 * record holds it (0x309aa). */
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

/* Counts another lifetime and takes the EFC in normal PLL, then writes
 * the record. */
static void records_lifetime_count(void) {
  lifetime.count++;
  if (pll.stage == PLL_STAGE_NORMAL_PLL) {
    lifetime.efc = pll.efc;
  }
  lifetime_record_write();
}

/* lifetime_update's clock event: takes the GPS date when the time is
 * valid and counts another lifetime (0x30a36). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x30a6c). */
void lifetime_update(int32_t date) {
  /* The clock event's period, three hours as a packed time (hour << 16). */
  static const uint32_t LIFETIME_EVENT_PERIOD = 3u << 16;
  klok_entry event = {.event_class = KLOK_EVENT_LIFETIME};

  if (lifetime.date != (uint32_t)date) {
    lifetime.date = date;
    records_lifetime_count();
  }
  klok_entry* previous = klok_find(KLOK_EVENT_LIFETIME);
  if (previous != 0 && klok_cancel(previous) != 0) {
    task_dmessage("Error in deleting old lifetime event\r\n");
  }

  /* klok_entry's time and date, and its period's, are each a
   * packed_datetime, which smartclock.h does not say. */
  get_gps_datetime(&event.date, (time_of_day*)&event.time);
  event.period_time = LIFETIME_EVENT_PERIOD;
  datetime_add((packed_datetime*)&event.period_time,
               (packed_datetime*)&event.time);
  event.function = lifetime_event_run;
  lifetime_event = clk_do(&event);
}

/* Sets the *IDN? model to its default (0x32e4c). */
void idn_model_default(void) { strncpy(idn_model, "58503A", sizeof idn_model); }

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x32e68). */
uint8_t identification_record_read(void) {
  if (record_checksum(&eeprom_identification_record,
                      sizeof eeprom_identification_record) !=
      eeprom_identification_record.checksum) {
    idn_model_default();
    strncpy(idn_serial, "0", sizeof idn_serial);
    idn_suffix = RECORDS_DEFAULT_SUFFIX;
    return 0;
  }
  if (strncmp(eeprom_identification_record.tag, (char*)RECORDS_TAG,
              sizeof eeprom_identification_record.tag) == 0) {
    strncpy(idn_model, eeprom_identification_record.model, sizeof idn_model);
  } else {
    idn_model_default();
  }
  strncpy(idn_serial, eeprom_identification_record.serial, sizeof idn_serial);
  idn_suffix = eeprom_identification_record.suffix;
  return 1;
}

/* Writes the model, serial and suffix to the identification record when
 * they differ from it; 1 when it holds them (0x32f0c). */
uint32_t identification_record_write(void) {
  if (strncmp(eeprom_identification_record.model, idn_model,
              sizeof idn_model) == 0 &&
      strncmp(eeprom_identification_record.serial, idn_serial,
              sizeof idn_serial) == 0 &&
      strncmp(eeprom_identification_record.tag, (char*)RECORDS_TAG,
              sizeof eeprom_identification_record.tag) == 0 &&
      eeprom_identification_record.suffix == idn_suffix) {
    return 1;
  }

  identification_record record;
  memset(&record, RECORDS_ERASED, sizeof record);
  strncpy(record.model, idn_model, sizeof record.model);
  strncpy(record.serial, idn_serial, sizeof record.serial);
  record.suffix = idn_suffix;
  strncpy(record.tag, (char*)RECORDS_TAG, sizeof record.tag);
  record.checksum = record_checksum(&record, sizeof record);
  return eeprom_write((char*)&eeprom_identification_record, (char*)&record,
                      sizeof record);
}
