/* The 58503B 1.01.04's EFC calibration read, the Z3815A 4010's in its
 * shape with the oscillator type's test, the Z3817A 3752's code
 * (relocated), where the decompiler took the block's floats' bits by
 * their values (docs/firmware/58503b-1.01.04.md, "The calibration
 * block"); and the lifetime record's update, the Z3815A's, as the
 * Z3817A's later C copies it, the same in each image (relocated), whose
 * clock event the decompiler copied from its template a byte at a time
 * into locals gcc does not keep together (docs/firmware/58503b-1.01.04.md,
 * "The function check").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

/* The calibration block in the EEPROM (save_calibration_block, 0x40aba). */
static const calibration_block* const RECORDS_CALIBRATION_BLOCK =
    (const calibration_block*)0x400080;

/* The bits of a long that efc_calibration_read takes for erased when all
 * are set: a float's sign and the top of its exponent (0x40e14). */
static const uint32_t RECORDS_ERASED_FLOAT_BITS = 0xe8000000;

/* The oscillator type byte as erased EEPROM reads. */
static const uint8_t RECORDS_ERASED_BYTE = 0xff;

/* A float the block holds, as the long the image reads. */
static uint32_t records_float_bits(const float* value) {
  return *(const uint32_t*)value;
}

/* A float from the long the block holds, bit for bit. */
static float records_float_from_bits(uint32_t bits) {
  union {
    uint32_t bits;
    float value;
  } both = {.bits = bits};

  return both.value;
}

/* Whether a float the block holds is erased. */
static bool records_erased_float(const float* value) {
  return (records_float_bits(value) & RECORDS_ERASED_FLOAT_BITS) ==
         RECORDS_ERASED_FLOAT_BITS;
}

/* Takes the EFC calibration from the block when none of its three is
 * erased, its a's long is not 0, and its oscillator type is
 * oscillator_type's or erased; else the type's defaults; true when taken
 * (0x40baa). */
bool efc_calibration_read(void) {
  const calibration_block* block = RECORDS_CALIBRATION_BLOCK;
  bool valid = !records_erased_float(&block->efc_dac_per_adc);

  if (records_erased_float(&block->efc_a) ||
      records_float_bits(&block->efc_a) == 0) {
    valid = false;
  }
  if (records_erased_float(&block->efc_b)) {
    valid = false;
  }
  if ((int32_t)(int8_t)oscillator_type != block->oscillator &&
      block->oscillator != RECORDS_ERASED_BYTE) {
    valid = false;
  }
  if (!valid) {
    efc_calibration_defaults();
    return false;
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = block->efc_dac_per_adc;
  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = block->efc_a;
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = block->efc_b;
  return true;
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

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x409e8). */
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

/* lifetime_update's clock event: takes the GPS date when the time is
 * valid and counts another lifetime (0x409b2). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* Sets the *IDN? model to its default (0x43178). */
void idn_model_default(void) {
  strncpy(idn_model, RECORDS_MODEL, sizeof idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x43194). */
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
 * they differ from it; 1 when it holds them (0x43238). */
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
 * (0x409a2). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* save_calibration_block as a descriptor's hook, a memory error
 * for the source when it fails; the index is not read; nonzero once
 * saved (0x40d90). */
uint8_t calibration_block_save_reported(int32_t index, int32_t source) {
  if (!save_calibration_block()) {
    status_report_from(STATUS_MEMORY_ERROR, (request_source)source);
    return 0;
  }
  return 1;
}

/* A record's word, or the default given while it is erased, 0xffff
 * (0x40c4e). */
int16_t record_word_or_default(int16_t word, int16_t fallback) {
  static const int16_t RECORDS_ERASED_WORD = -1;

  return word != RECORDS_ERASED_WORD ? word : fallback;
}

/* save_parameter_record, a memory error for the source when it
 * fails; nonzero once saved (0x40f72). */
uint8_t parameter_record_save_reported(request_source source) {
  if (!save_parameter_record()) {
    status_report_from(STATUS_MEMORY_ERROR, source);
    return 0;
  }
  return 1;
}

/* save_calibration_block, a memory error for the source when it
 * fails; the index is not read; nonzero once saved. */
static uint8_t records_save_reported(int32_t source) {
  if (!save_calibration_block()) {
    status_report_from(STATUS_MEMORY_ERROR, (uint8_t)source);
    return 0;
  }
  return 1;
}

/* The done hook of :DIAG:ROSC:EFC:ASLOPe, :BSLOPe and :DADC: the
 * calibration given, then the block saved (0x40db0). */
uint8_t efc_calibration_done(int32_t index, int32_t source) {
  efc_calibration_given = 1;
  return records_save_reported(source);
}

/* The written functions below are this image's own: the oscillator type
 * and what hangs on it, and the records' readers and writers, which keep
 * the SCI's settings where the 58503A 3704's keep DUART channel B's. */

/* :DIAGnostic:ROSCillator:EFControl:DADC's default, the DAC's counts per
 * ADC count (0x40b9c). */
static const float RECORDS_EFC_DAC_PER_ADC = 16.0f;

/* The greatest oscillator type, past which oscillator_type reads as 0
 * (0x40c62). */
uint8_t oscillator_type_most(void) { return 1; }

/* oscillator_type's entry of efc_oscillators, the first past
 * oscillator_type_most; a negative type indexes before the table, as the
 * image does. */
static const efc_oscillator_entry* records_oscillator_entry(void) {
  return &efc_oscillators[(int8_t)oscillator_type > oscillator_type_most()
                              ? 0
                              : (int8_t)oscillator_type];
}

/* The EFC calibration's defaults by the oscillator type: :ASLOPe its
 * entry's, :BSLOPe 0 and :DADC 16 (0x40b5e).  The Z3815A 4010's takes a
 * by the switch word's bit 12. */
void efc_calibration_defaults(void) {
  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe =
      records_oscillator_entry()->efc_a;
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = RECORDS_EFC_DAC_PER_ADC;
}

/* :ASLOPe's limits by the oscillator type, its entry's, and :BSLOPe's 0
 * (0x40c9e). */
void efc_calibration_limits(void) {
  const efc_oscillator_entry* entry = records_oscillator_entry();

  efc_a_minimum = entry->efc_a_minimum;
  efc_a_maximum = entry->efc_a_maximum;
  efc_b_minimum = 0.0f;
  efc_b_maximum = 0.0f;
}

/* A record's long, or the default given while it is erased, its bits
 * RECORDS_ERASED_FLOAT_BITS all set, as a float's (0x40c2e). */
uint32_t record_long_or_default(uint32_t value, uint32_t fallback) {
  return (value & RECORDS_ERASED_FLOAT_BITS) != RECORDS_ERASED_FLOAT_BITS
             ? value
             : fallback;
}

/* oscillator_type from the calibration block, 0 for a bad block or an
 * erased byte, and 0 past oscillator_type_most (0x40c66). */
void oscillator_type_read(uint8_t block_bad) {
  if (block_bad == 0 &&
      RECORDS_CALIBRATION_BLOCK->oscillator != RECORDS_ERASED_BYTE) {
    oscillator_type = RECORDS_CALIBRATION_BLOCK->oscillator;
  } else {
    oscillator_type = 0;
  }
  if ((int8_t)oscillator_type > oscillator_type_most()) {
    oscillator_type = 0;
  }
}

/* Reads the time offset, the two TCs, the second 0 while erased, the
 * 1PPS cable delay, 0 while erased, the oscillator type and the EFC
 * calibration from the calibration block, or zeros, the type 0 and the
 * calibration's defaults; then the EFC calibration's limits by the type;
 * true when its checksum holds (0x40cf0).  The Z3815A 4010's in its shape,
 * with the second TC, the type and the limits. */
bool calibration_block_read(void) {
  const calibration_block* block = RECORDS_CALIBRATION_BLOCK;
  bool valid;

  efc_calibration_given = 0;
  if (record_checksum(block, sizeof *block) != block->checksum) {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
    tc_coefficient_second = 0.0f;
    pps_cable_delay_offset = 0;
    oscillator_type_read(1);
    efc_calibration_defaults();
    valid = false;
  } else {
    scpi_value_DIAGnostic_TOFFset = block->time_offset;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = block->tc_coefficient;
    tc_coefficient_second = records_float_from_bits(record_long_or_default(
        records_float_bits(&block->tc_coefficient_second), 0));
    pps_cable_delay_offset =
        (uint16_t)record_word_or_default(block->field_0a, 0);
    oscillator_type_read(0);
    efc_calibration_given = efc_calibration_read();
    valid = true;
  }
  efc_calibration_limits();
  return valid;
}

/* Writes the time offset, the two TCs, the 1PPS cable delay cut to a
 * word, the EFC calibration, its a 0 unless given, and the oscillator
 * type to the calibration block when the block differs from it; 1 when
 * the block holds them (0x40aa0).  The Z3815A 4010's with the second TC
 * and the type. */
uint32_t save_calibration_block(void) {
  calibration_block block;

  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.tc_coefficient_second = tc_coefficient_second;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.efc_dac_per_adc = scpi_value_DIAGnostic_ROSCillator_EFControl_DADC;
  block.efc_a = efc_calibration_given != 0
                    ? scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe
                    : 0.0f;
  block.efc_b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  block.oscillator = oscillator_type;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0 ||
      (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                            sizeof block) != 0) {
    return 1;
  }
  return 0;
}

/* Writes the SCI's settings to the parameter record, its baud, parity,
 * pacing and echo in DUART channel B's fields too, and the rest of
 * those as no HP pacing, eight data bits and one stop bit; true when
 * written (0x40e5a).  The 58503A's writes channel B's in both. */
bool save_parameter_record(void) {
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
  return (uint8_t)eeprom_write((char*)&eeprom_parameter_record, (char*)&record,
                               sizeof record) != 0;
}

/* Reads the SCI's settings from the parameter record's SCI fields; else
 * takes their defaults and saves them; true when its checksum holds
 * (0x40ef6).  The 58503A's reads DUART channel B's and returns D3 as its
 * caller left it for a good record. */
bool parameter_record_read(void) {
  if (record_checksum(&eeprom_parameter_record,
                      sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
    save_parameter_record();
    return false;
  }
  sci_settings.baud = eeprom_parameter_record.sci_baud;
  sci_settings.parity = eeprom_parameter_record.sci_parity;
  sci_settings.xon_xoff = eeprom_parameter_record.sci_xon_xoff;
  sci_settings.echo = eeprom_parameter_record.sci_echo;
  return true;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action, for the SCI here: its
 * defaults, taken and saved; the index is not read (0x40f92). */
void duartb_settings_preset(int32_t index, int32_t source) {
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  duartb_settings_done();
  parameter_record_save_reported((request_source)source);
}

/* :DIAGnostic:ROSCillator:TYPE's done hook: the EFC calibration taken
 * afresh from the defaults for the new kind and saved, -311, Memory
 * error, for the source if the block could not be (0x40dd8).  The
 * source is the second argument, as every descriptor's hook's, and the
 * index is not read (0x40dea). */
void oscillator_kind_done(int32_t index, int32_t source) {
  efc_calibration_given = 0;
  efc_calibration_defaults();
  if (save_calibration_block() == 0) {
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
}
