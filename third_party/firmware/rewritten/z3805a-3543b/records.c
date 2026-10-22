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
 * (0x3082e). */
uint16_t record_checksum(void* record, uint16_t length) {
  /* What a record's checksum word does not cover: the word itself,
   * its first two bytes. */
  static const uint16_t CHECKSUM_SIZE = 2;

  if (length < CHECKSUM_SIZE) {
    return 0;
  }
  return records_sum_and_complement(
      records_byte_sum((const uint8_t*)record + CHECKSUM_SIZE,
                       (uint16_t)(length - CHECKSUM_SIZE)));
}

/* Reads the lifetime record into lifetime, or its defaults; true when its
 * checksum holds (0x30a3e). */
uint8_t lifetime_record_read(void) {
  /* The lifetime record's defaults: 1994-01-01 (year << 16 | month << 8
   * | day) and the EFC at mid-scale. */
  static const uint32_t LIFETIME_DEFAULT_DATE = 1994u << 16 | 1u << 8 | 1u;
  static const float EFC_MID_SCALE = 524288.0f;
  /* An EFC in the record with these bits all set, as an erased
   * record's 0xffffffff has, is taken as mid-scale. */
  static const uint32_t LIFETIME_EFC_INVALID_BITS = 0xe8000000u;

  if (record_checksum(&eeprom_lifetime_record, sizeof eeprom_lifetime_record) !=
      eeprom_lifetime_record.checksum) {
    lifetime.date = LIFETIME_DEFAULT_DATE;
    lifetime.count = 0;
    lifetime.efc = EFC_MID_SCALE;
    return 0;
  }
  lifetime.date = eeprom_lifetime_record.date;
  lifetime.count = eeprom_lifetime_record.count;

  /* The image tests the float's bits. */
  uint32_t efc_bits;
  float efc = eeprom_lifetime_record.efc;
  memcpy(&efc_bits, &efc, sizeof efc_bits);
  if ((efc_bits & LIFETIME_EFC_INVALID_BITS) == LIFETIME_EFC_INVALID_BITS) {
    lifetime.efc = EFC_MID_SCALE;
  } else {
    lifetime.efc = efc;
  }
  return 1;
}

/* Writes lifetime to the lifetime record when it differs; 1 when the
 * record holds it (0x30ab4). */
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
 * valid and counts another lifetime (0x30b30). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x30b66). */
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

/* Reads the time offset and TC from the calibration block, or zeros;
 * true when its checksum holds (0x30c1e). */
bool calibration_block_read(void) {
  bool valid = record_checksum(&eeprom_calibration_block,
                               sizeof eeprom_calibration_block) ==
               eeprom_calibration_block.checksum;

  if (valid) {
    scpi_value_DIAGnostic_TOFFset = eeprom_calibration_block.time_offset;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient =
        eeprom_calibration_block.tc_coefficient;
  } else {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
  }
  return valid;
}

/* Writes the time offset and TC to the calibration block when they
 * differ from it, a memory error for the source when it fails (0x30c60). */
uint32_t save_calibration_block(uint32_t source) {
  if (eeprom_calibration_block.time_offset == scpi_value_DIAGnostic_TOFFset &&
      eeprom_calibration_block.tc_coefficient ==
          scpi_value_DIAGnostic_ROSCillator_TCOefficient) {
    return 1;
  }

  calibration_block block;
  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.checksum = record_checksum(&block, sizeof block);
  if (!eeprom_write((char*)&eeprom_calibration_block, (char*)&block,
                    sizeof block)) {
    status_report_from(STATUS_MEMORY_ERROR, (uint8_t)source);
    return 0;
  }
  return 1;
}

/* Reads DUART channel B's settings from the parameter record, or their
 * defaults; always the SCI's defaults; true when its checksum holds
 * (0x30d2e). */
uint8_t parameter_record_read(void) {
  bool valid = record_checksum(&eeprom_parameter_record,
                               sizeof eeprom_parameter_record) ==
               eeprom_parameter_record.checksum;

  if (valid) {
    duartb_settings.baud = eeprom_parameter_record.duartb_baud;
    duartb_settings.parity = eeprom_parameter_record.duartb_parity;
    duartb_settings.xon_xoff = eeprom_parameter_record.duartb_xon_xoff;
    duartb_settings.field_05 = eeprom_parameter_record.duartb_field_05;
    duartb_settings.eight_bits = eeprom_parameter_record.duartb_eight_bits;
    duartb_settings.two_stop_bits =
        eeprom_parameter_record.duartb_two_stop_bits;
    duartb_settings.echo = eeprom_parameter_record.duartb_echo;
  } else {
    memcpy(&duartb_settings, &records_duartb_defaults, sizeof duartb_settings);
  }
  memcpy(&sci_settings, &records_sci_defaults, sizeof sci_settings);
  return valid;
}

/* Writes both serial ports' settings to the parameter record, a memory
 * error for the source when it fails (0x30dea). */
bool save_parameter_record(uint8_t source) {
  parameter_record record;

  memset(&record, RECORDS_ERASED, sizeof record);
  record.duartb_baud = duartb_settings.baud;
  record.duartb_parity = duartb_settings.parity;
  record.duartb_xon_xoff = duartb_settings.xon_xoff;
  record.duartb_field_05 = duartb_settings.field_05;
  record.duartb_eight_bits = duartb_settings.eight_bits;
  record.duartb_two_stop_bits = duartb_settings.two_stop_bits;
  record.duartb_echo = duartb_settings.echo;
  record.sci_baud = sci_settings.baud;
  record.sci_parity = sci_settings.parity;
  record.sci_xon_xoff = sci_settings.xon_xoff;
  record.sci_echo = sci_settings.echo;
  record.checksum = record_checksum(&record, sizeof record);

  bool written = eeprom_write((char*)&eeprom_parameter_record, (char*)&record,
                              sizeof record) != 0;
  if (!written) {
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
  return written;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action for channel B: its defaults,
 * taken and saved (0x30ea0). */
void duartb_settings_preset(int32_t index, int32_t source) {
  (void)index;
  memcpy(&duartb_settings, &records_duartb_defaults, sizeof duartb_settings);
  duartb_settings_done();
  save_parameter_record(source);
}

/* The same for the SCI (0x30ed4). */
void sci_settings_preset(int32_t index, int32_t source) {
  (void)index;
  memcpy(&sci_settings, &records_sci_defaults, sizeof sci_settings);
  sci_settings_done();
  save_parameter_record(source);
}

/* Sets the *IDN? model to its default (0x32e40). */
void idn_model_default(void) { strncpy(idn_model, "Z3805A", sizeof idn_model); }

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x32e5c). */
uint8_t identification_record_read(void) {
  /* The identification record's default suffix. */
  static const char DEFAULT_SUFFIX = 'A';

  if (record_checksum(&eeprom_identification_record,
                      sizeof eeprom_identification_record) !=
      eeprom_identification_record.checksum) {
    idn_model_default();
    strncpy(idn_serial, "0", sizeof idn_serial);
    idn_suffix = DEFAULT_SUFFIX;
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
 * they differ from it; 1 when it holds them (0x32f00). */
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
