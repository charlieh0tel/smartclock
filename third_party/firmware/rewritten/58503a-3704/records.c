/* records.c's functions that are this image's own: the rest are
 * common/records.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* DUART channel B's settings by default, 9600 baud, no parity, eight
 * bits, echo: the image's duartb_settings_defaults (0x3092a). */
static const duartb_settings_record records_duartb_defaults = {
    .baud = SERIAL_BAUD_9600,
    .parity = SERIAL_PARITY_NONE,
    .eight_bits = 1,
    .echo = 1,
};

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

/* Takes pll.efc into lifetime and writes the record; nothing calls it
 * (0x30a26). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* The calibration block in the EEPROM, at 0x400080, which the export
 * declares no object for. */
static calibration_block* const RECORDS_CALIBRATION_BLOCK =
    (calibration_block*)0x400080;

/* The calibration block's field_0a when it holds no cable delay. */
static const uint16_t RECORDS_NO_CABLE_DELAY = 0xffff;

/* Writes the time offset, the TC and the 1PPS cable delay, cut to a word,
 * to the calibration block when the block differs from it; 1 when the
 * block holds them (0x30b24). */
uint32_t save_calibration_block_58503a(void) {
  calibration_block block;

  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0) {
    return 1;
  }
  return (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                               sizeof block) != 0;
}

/* Reads the time offset, the TC and the 1PPS cable delay from the
 * calibration block, or zeros; true when its checksum holds (0x30ba8).
 * The delay is read back as an unsigned word, so that a negative one
 * that save_calibration_block_58503a cut to a word comes back 65536 more
 * (docs/firmware/58503a-3704.md, "The records"). */
uint8_t calibration_block_read(void) {
  calibration_block* block = RECORDS_CALIBRATION_BLOCK;

  if (record_checksum(block, sizeof *block) != block->checksum) {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
    pps_cable_delay_offset = 0;
    return 0;
  }
  scpi_value_DIAGnostic_TOFFset = block->time_offset;
  scpi_value_DIAGnostic_ROSCillator_TCOefficient = block->tc_coefficient;
  if ((uint16_t)block->field_0a == RECORDS_NO_CABLE_DELAY) {
    pps_cable_delay_offset = 0;
  } else {
    pps_cable_delay_offset = (uint16_t)block->field_0a;
  }
  return 1;
}

/* save_calibration_block_58503a as a descriptor's hook, a memory error
 * for the source when it fails; the index is not read; nonzero once
 * saved (0x30c0a). */
uint8_t calibration_block_save_reported(int32_t index, int32_t source) {
  if (!save_calibration_block_58503a()) {
    status_report_from(STATUS_MEMORY_ERROR, (request_source)source);
    return 0;
  }
  return 1;
}

/* Writes DUART channel B's settings to the parameter record, and its
 * baud, parity, pacing and echo again in the SCI's fields, the 58503A
 * having no SCI; true when written (0x30c6c). */
bool save_parameter_record_58503a(void) {
  parameter_record record;

  memset(&record, RECORDS_ERASED, sizeof record);
  record.duartb_baud = duartb_settings.baud;
  record.duartb_parity = duartb_settings.parity;
  record.duartb_xon_xoff = duartb_settings.xon_xoff;
  record.duartb_field_05 = duartb_settings.field_05;
  record.duartb_eight_bits = duartb_settings.eight_bits;
  record.duartb_two_stop_bits = duartb_settings.two_stop_bits;
  record.duartb_echo = duartb_settings.echo;
  record.sci_baud = duartb_settings.baud;
  record.sci_parity = duartb_settings.parity;
  record.sci_xon_xoff = duartb_settings.xon_xoff;
  record.sci_echo = duartb_settings.echo;
  record.checksum = record_checksum(&record, sizeof record);
  return (uint8_t)eeprom_write((char*)&eeprom_parameter_record, (char*)&record,
                               sizeof record) != 0;
}

/* Reads DUART channel B's settings from the parameter record; else takes
 * their defaults and saves them, and returns 0.  When the record is good
 * it returns D3 as its caller left it, which it never sets
 * (docs/firmware/58503a-3704.md, "The records") (0x30d12). */
uint8_t parameter_record_read_58503a(void) {
  uint8_t result = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");

  if (record_checksum(&eeprom_parameter_record,
                      sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    memcpy(&duartb_settings, &records_duartb_defaults, sizeof duartb_settings);
    save_parameter_record_58503a();
    return 0;
  }
  duartb_settings.baud = eeprom_parameter_record.duartb_baud;
  duartb_settings.parity = eeprom_parameter_record.duartb_parity;
  duartb_settings.xon_xoff = eeprom_parameter_record.duartb_xon_xoff;
  duartb_settings.field_05 = eeprom_parameter_record.duartb_field_05;
  duartb_settings.eight_bits = eeprom_parameter_record.duartb_eight_bits;
  duartb_settings.two_stop_bits = eeprom_parameter_record.duartb_two_stop_bits;
  duartb_settings.echo = eeprom_parameter_record.duartb_echo;
  return result;
}

/* save_parameter_record_58503a, a memory error for the source when it
 * fails; nonzero once saved (0x30da4). */
uint8_t parameter_record_save_reported(request_source source) {
  if (!save_parameter_record_58503a()) {
    status_report_from(STATUS_MEMORY_ERROR, source);
    return 0;
  }
  return 1;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action for channel B: its defaults,
 * taken and saved; the index is not read (0x30dc4). */
void duartb_settings_preset(int32_t index, int32_t source) {
  memcpy(&duartb_settings, &records_duartb_defaults, sizeof duartb_settings);
  duartb_settings_done();
  parameter_record_save_reported((request_source)source);
}
