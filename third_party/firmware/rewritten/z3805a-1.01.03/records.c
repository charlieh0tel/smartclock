/* From the 58503B 1.01.04's records.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those common/ does not hold, each identical
 * or relocated in image_diff's matches (docs/firmware/z3805a-1.01.03.md,
 * "The export and the checks"); their comments' addresses are the Z3805A
 * 1.01.03's.  The 58503B's note follows. */

/* The 58503B 1.01.04's EFC calibration read, the Z3815A 4010's in its
 * shape with the oscillator type's test, the Z3817A 3752's code
 * (relocated), written by hand in place of the decompiler's, which took
 * the block's floats' bits by their values
 * (docs/firmware/58503b-1.01.04.md, "The calibration block";
 * docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype. */

/* The calibration block in the EEPROM (save_calibration_block, 0x326aa). */
static const calibration_block* const RECORDS_CALIBRATION_BLOCK =
    (const calibration_block*)0x400080;

/* The bits of a long that efc_calibration_read takes for erased when all
 * are set: a float's sign and the top of its exponent (0x32a04). */
static const uint32_t RECORDS_ERASED_FLOAT_BITS = 0xe8000000;

/* The oscillator type byte as erased EEPROM reads. */
static const uint8_t RECORDS_ERASED_BYTE = 0xff;

/* A float the block holds, as the long the image reads. */
static uint32_t records_float_bits(const float* value) {
  return *(const uint32_t*)value;
}

/* Whether a float the block holds is erased. */
static bool records_erased_float(const float* value) {
  return (records_float_bits(value) & RECORDS_ERASED_FLOAT_BITS) ==
         RECORDS_ERASED_FLOAT_BITS;
}

/* Takes the EFC calibration from the block when none of its three is
 * erased, its a's long is not 0, and its oscillator type is
 * oscillator_type's or erased; else the type's defaults; true when taken
 * (0x3279a). */
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

/* The lifetime record's update, the Z3815A 4010's, whose instructions
 * this image's are but for their addresses (0x32592 to 0x3268e), where
 * the decompiler's lifetime_update built its clock event in pieces. */

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
 * valid and counts another lifetime (0x325a2). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x325d8). */
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

/* The parameter record's routines, this image's own, read from its code
 * (0x32a4a to 0x32c3e): the Z3815A 4010's save and presets, where the
 * decompiler's save filled the record through one local's address, and
 * the 58503A 3704's read, which takes the SCI's defaults whatever the
 * record holds. */

/* Writes both ports' settings to the parameter record in the EEPROM,
 * its unused bytes erased and its checksum made: DUART channel B's
 * settings' +2 to +8 and the SCI's +2 to +5; nonzero once written
 * (0x32a4a). */
bool save_parameter_record(void) {
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
  return (uint8_t)eeprom_write((char*)&eeprom_parameter_record, (char*)&record,
                               sizeof record) != 0;
}

/* Reads DUART channel B's settings from the parameter record and takes
 * the SCI's defaults, returning 1; when its checksum fails, takes both
 * ports' defaults, saves them, and returns 0 (0x32af0). */
uint8_t parameter_record_read(void) {
  if (record_checksum(&eeprom_parameter_record,
                      sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    memcpy(&duartb_settings, &duartb_settings_defaults, sizeof duartb_settings);
    memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
    save_parameter_record();
    return 0;
  }
  duartb_settings.baud = eeprom_parameter_record.duartb_baud;
  duartb_settings.parity = eeprom_parameter_record.duartb_parity;
  duartb_settings.xon_xoff = eeprom_parameter_record.duartb_xon_xoff;
  duartb_settings.field_05 = eeprom_parameter_record.duartb_field_05;
  duartb_settings.eight_bits = eeprom_parameter_record.duartb_eight_bits;
  duartb_settings.two_stop_bits = eeprom_parameter_record.duartb_two_stop_bits;
  duartb_settings.echo = eeprom_parameter_record.duartb_echo;
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  return 1;
}

/* save_parameter_record, a memory error for the source when it
 * fails; nonzero once saved (0x32bb8). */
uint8_t parameter_record_save_reported(request_source source) {
  if (!save_parameter_record()) {
    status_report_from(STATUS_MEMORY_ERROR, source);
    return 0;
  }
  return 1;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action for channel B, the SCPI
 * port's: its defaults, applied and saved; the index is not read
 * (0x32bd8). */
void duartb_settings_preset(int32_t index, int32_t source) {
  memcpy(&duartb_settings, &duartb_settings_defaults, sizeof duartb_settings);
  duartb_settings_done();
  parameter_record_save_reported((request_source)source);
}

/* The same for the SCI, its settings applied by sci_settings_done
 * (0x32c0c). */
void sci_settings_preset(int32_t index, int32_t source) {
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  sci_settings_done();
  parameter_record_save_reported((request_source)source);
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

/* Takes pll.efc into lifetime and writes the record; nothing calls it
 * (0x32592). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* save_calibration_block as a descriptor's hook, a memory error
 * for the source when it fails; the index is not read; nonzero once
 * saved (0x32980). */
uint8_t calibration_block_save_reported(int32_t index, int32_t source) {
  if (!save_calibration_block()) {
    status_report_from(STATUS_MEMORY_ERROR, (request_source)source);
    return 0;
  }
  return 1;
}

/* A record's word, or the default given while it is erased, 0xffff
 * (0x3283e). */
int16_t record_word_or_default(int16_t word, int16_t fallback) {
  static const int16_t RECORDS_ERASED_WORD = -1;

  return word != RECORDS_ERASED_WORD ? word : fallback;
}

/* The done hook of :DIAG:ROSC:EFC:ASLOPe, :BSLOPe and :DADC: the
 * calibration given, then the block saved (0x329a0). */
uint8_t efc_calibration_done(int32_t index, int32_t source) {
  efc_calibration_given = 1;
  return records_save_reported(source);
}

/* A record's long, or the default given while it is erased, its bits
 * RECORDS_ERASED_FLOAT_BITS all set, as a float's (0x3281e). */
uint32_t record_long_or_default(uint32_t value, uint32_t fallback) {
  return (value & RECORDS_ERASED_FLOAT_BITS) != RECORDS_ERASED_FLOAT_BITS
             ? value
             : fallback;
}

/* Sets the *IDN? model to its default (0x34db6). */
void idn_model_default(void) {
  strncpy(idn_model, RECORDS_MODEL, sizeof idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x34dd2). */
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
 * they differ from it; 1 when it holds them (0x34e76). */
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

/* The highest oscillator type (0x32852). */
uint8_t oscillator_type_most(void) { return 1; }

/* oscillator_type from the calibration block, 0 for a bad block or an
 * erased byte, and 0 past oscillator_type_most (0x32856). */
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

/* The oscillator type whose entry of efc_calibration_types serves:
 * oscillator_type, or the first past oscillator_type_most. */
static int32_t records_calibration_type(void) {
  return (int8_t)oscillator_type > (int32_t)oscillator_type_most()
             ? 0
             : (int8_t)oscillator_type;
}

/* The EFC calibration's defaults: :ASLOPe by the oscillator type, :BSLOPe 0
 * and the DAC's 16 counts per ADC count (0x3274e). */
void efc_calibration_defaults(void) {
  static const float RECORDS_EFC_DAC_PER_ADC = 16.0f;

  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe =
      efc_calibration_types[records_calibration_type()].a_default;
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = RECORDS_EFC_DAC_PER_ADC;
}

/* The bounds :ASLOPe and :BSLOPe are held to: a's by the oscillator type,
 * b's both 0 (0x3288e). */
void efc_calibration_limits(void) {
  const efc_calibration_type* type =
      &efc_calibration_types[records_calibration_type()];

  efc_a_minimum = type->a_minimum;
  efc_a_maximum = type->a_maximum;
  efc_b_minimum = 0.0f;
  efc_b_maximum = 0.0f;
}

/* Writes the time offset, both TCs, the 1PPS cable delay, cut to a word,
 * the EFC calibration, its a only while given, and the oscillator type
 * to the calibration block when the block differs from them; 1 when the
 * block holds them (0x32690). */
uint32_t save_calibration_block(void) {
  calibration_block block;

  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.tc_coefficient_second = tc_coefficient_second;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.efc_dac_per_adc = scpi_value_DIAGnostic_ROSCillator_EFControl_DADC;
  if (efc_calibration_given) {
    block.efc_a = scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe;
  } else {
    block.efc_a = 0.0f;
  }
  block.efc_b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  block.oscillator = oscillator_type;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0) {
    return 1;
  }
  return (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                               sizeof block) != 0;
}

/* Reads the time offset, both TCs, the second 0 while erased, the 1PPS
 * cable delay, the oscillator type and the EFC calibration from the
 * calibration block, or zeros, the type as a bad block gives it and the
 * calibration's defaults; then the calibration's limits; true when the
 * block's checksum holds (0x328e0).  The delay is read back as an
 * unsigned word through record_word_or_default, 0 while erased. */
bool calibration_block_read(void) {
  calibration_block* block = RECORDS_CALIBRATION_BLOCK;
  bool held;

  efc_calibration_given = 0;
  if (record_checksum(block, sizeof *block) == block->checksum) {
    scpi_value_DIAGnostic_TOFFset = block->time_offset;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = block->tc_coefficient;
    *(uint32_t*)&tc_coefficient_second = record_long_or_default(
        records_float_bits(&block->tc_coefficient_second), 0);
    pps_cable_delay_offset =
        (uint16_t)record_word_or_default(block->field_0a, 0);
    oscillator_type_read(0);
    efc_calibration_given = efc_calibration_read();
    held = true;
  } else {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
    tc_coefficient_second = 0.0f;
    pps_cable_delay_offset = 0;
    oscillator_type_read(1);
    efc_calibration_defaults();
    held = false;
  }
  efc_calibration_limits();
  return held;
}

/* :DIAGnostic:ROSCillator:TYPE's done hook: the EFC calibration taken
 * afresh from the defaults for the new kind and saved, -311, Memory
 * error, for the source if the block could not be (0x329c8).  The
 * source is the second argument, as every descriptor's hook's, and the
 * index is not read (0x329da). */
void oscillator_kind_done(int32_t index, int32_t source) {
  efc_calibration_given = 0;
  efc_calibration_defaults();
  if (save_calibration_block() == 0) {
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
}
