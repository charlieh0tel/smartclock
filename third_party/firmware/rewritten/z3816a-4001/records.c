/* The Z3816A 4001's EEPROM record writers, the 58503A 3704's
 * rewritten/records.c's but where this image's differ: each reports a
 * memory error for its source itself, as the Z3801A 3543's do, and the
 * parameter record keeps the SCI's settings in both ports' bytes (no
 * DUART channel B settings here; docs/firmware/z3816a-4001.md, "The host
 * port").  The decompiler's readings fill each record over separate
 * locals, which gcc lays out otherwise.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype.
 * With them, from the 58503A 3704's rewritten/records.c, of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use,
 * each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Their comments'
 * addresses are the Z3816A's. */

/* The calibration block in the EEPROM, at 0x400080, which the export
 * declares no object for. */
static calibration_block* const RECORDS_CALIBRATION_BLOCK =
    (calibration_block*)0x400080;

/* Writes the time offset, the TC and the 1PPS cable delay, cut to a word,
 * to the calibration block when the block differs from it, a memory
 * error for the source when the write fails; the index is not read; 1
 * when the block holds them (0x404be). */
uint32_t save_calibration_block(int32_t index, int32_t source) {
  calibration_block block;

  (void)index;
  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0 ||
      (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                            sizeof block) != 0) {
    return 1;
  }
  status_report_from(STATUS_MEMORY_ERROR, (request_source)source);
  return 0;
}

/* Writes the SCI's settings to the parameter record, in channel B's
 * bytes and again in the SCI's, a memory error for the source when it
 * fails; true when written (0x405fe).  Channel B's other bytes are
 * fixed: no field_05, eight bits, one stop bit (0x4062c to 0x40636). */
bool save_parameter_record(request_source source) {
  parameter_record record;

  memset(&record, RECORDS_ERASED, sizeof record);
  record.duartb_baud = sci_settings.baud;
  record.duartb_parity = sci_settings.parity;
  record.duartb_xon_xoff = sci_settings.xon_xoff;
  record.duartb_field_05 = 0;
  record.duartb_eight_bits = 1;
  record.duartb_two_stop_bits = 0;
  record.duartb_echo = sci_settings.echo;
  record.sci_baud = sci_settings.baud;
  record.sci_parity = sci_settings.parity;
  record.sci_xon_xoff = sci_settings.xon_xoff;
  record.sci_echo = sci_settings.echo;
  record.checksum = record_checksum(&record, sizeof record);
  if ((uint8_t)eeprom_write((char*)&eeprom_parameter_record, (char*)&record,
                            sizeof record) != 0) {
    return true;
  }
  status_report_from(STATUS_MEMORY_ERROR, source);
  return false;
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
 * valid and counts another lifetime (0x4036e). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x403a4). */
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

/* Sets the *IDN? model to its default (0x42732). */
void idn_model_default(void) { strncpy(idn_model, "Z3816A", sizeof idn_model); }

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x4274e). */
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
 * they differ from it; 1 when it holds them (0x427f2). */
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
 * (0x4035e). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* The calibration block's field_0a when it holds no cable delay. */
static const uint16_t RECORDS_NO_CABLE_DELAY = 0xffff;

/* Reads the time offset, the TC and the 1PPS cable delay from the
 * calibration block, or zeros; true when its checksum holds (0x4045c).
 * The delay is read back as an unsigned word, so that a negative one
 * that save_calibration_block cut to a word comes back 65536 more
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

/* Reads the SCI's settings from the parameter record, 1; else takes their
 * defaults and returns 0 (0x40594).  The Z3815A 4010's but that it has
 * no DUART channel B settings to read and saves no defaults. */
uint8_t parameter_record_read(void) {
  if (record_checksum(&eeprom_parameter_record,
                      sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
    return 0;
  }
  sci_settings.baud = eeprom_parameter_record.sci_baud;
  sci_settings.parity = eeprom_parameter_record.sci_parity;
  sci_settings.xon_xoff = eeprom_parameter_record.sci_xon_xoff;
  sci_settings.echo = eeprom_parameter_record.sci_echo;
  return 1;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action, the SCI's: its defaults,
 * applied by duartb_settings_done, which reconfigures the SCI here, and
 * saved for the source; the index is not read (0x406aa).  The Z3815A
 * 4010's but that it saves through save_parameter_record itself. */
void sci_settings_preset(int32_t index, int32_t source) {
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  duartb_settings_done();
  save_parameter_record((request_source)source);
}
