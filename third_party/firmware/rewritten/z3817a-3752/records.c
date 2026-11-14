/* From the Z3815A 4010's rewritten/records.c, the Z3817A 3752's own beside
 * common/'s: of the records' routines, those the function check finds the
 * decompiler's reading wrong for: the lifetime record's update, the same in
 * both images, whose event the decompiler copied from its template a byte at a
 * time into locals gcc does not keep together; and the calibration block's EFC
 * calibration, the Z3815A's but for this image's oscillator kind, and the
 * kind's own read, whose erased byte and floats' bits the decompiler compared
 * otherwise (docs/firmware/z3817a-3752.md, "The function check"); and the
 * calibration block's and the parameter record's readers and writers,
 * the Z3815A's written as this image's, with the EFC calibration's
 * defaults, record_float_or_default and the kind's done hook, this
 * image's own.  Its comments' addresses are the Z3817A's, but where they
 * name another image.  Each keeps its generated prototype. */

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
 * (0x41684). */
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

/* The calibration block in the EEPROM, at 0x400080, which the export
 * declares no object for. */
static calibration_block* const RECORDS_CALIBRATION_BLOCK =
    (calibration_block*)0x400080;

/* An erased byte of the EEPROM. */
static const uint8_t RECORDS_ERASED_BYTE = 0xff;

/* The bits of a long that efc_calibration_read takes for erased when all
 * are set: a float's sign and the top of its exponent (0x41a18). */
static const uint32_t RECORDS_ERASED_FLOAT_BITS = 0xe8000000;

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
 * erased, its a's long is not 0, and the oscillator it was taken for is
 * this one or erased, else the defaults; true when taken (0x41834).  The
 * Z3815A 4010's but for the oscillator. */
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
  /* the kind sign extended, the block's byte not (0x41874 to 0x41882) */
  if ((int8_t)oscillator_kind != block->oscillator &&
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

/* A record's float, as its bits, or the default given while it is
 * erased, as efc_calibration_read takes one (0x418b8). */
uint32_t record_float_or_default(uint32_t bits, uint32_t fallback) {
  return (bits & RECORDS_ERASED_FLOAT_BITS) != RECORDS_ERASED_FLOAT_BITS
             ? bits
             : fallback;
}

/* Writes the time offset, the two TC coefficients, the 1PPS cable delay,
 * cut to a word, the EFC calibration, its a only while given, and the
 * oscillator's kind to the calibration block when the block differs from
 * it; 1 when the block holds them (0x4173c).  The Z3815A's writes one
 * coefficient and no kind. */
uint32_t save_calibration_block(void) {
  calibration_block block;

  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.tc_coefficient_1 = tc_coefficient_1;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.efc_dac_per_adc = scpi_value_DIAGnostic_ROSCillator_EFControl_DADC;
  if (efc_calibration_given) {
    block.efc_a = scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe;
  } else {
    block.efc_a = 0.0f;
  }
  block.efc_b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  block.oscillator = oscillator_kind;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0) {
    return 1;
  }
  return (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                               sizeof block) != 0;
}

/* Reads the time offset, the two TC coefficients, the 1PPS cable delay,
 * the oscillator's kind and the EFC calibration from the calibration
 * block, or zeros, kind 0 and the calibration's defaults; true when its
 * checksum holds (0x41910).  The second coefficient is 0 while erased,
 * and the delay is read back as an unsigned word through
 * record_word_or_default, 0 while erased.  The Z3815A's reads one
 * coefficient and no kind. */
bool calibration_block_read(void) {
  calibration_block* block = RECORDS_CALIBRATION_BLOCK;

  efc_calibration_given = 0;
  if (record_checksum(block, sizeof *block) != block->checksum) {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
    tc_coefficient_1 = 0.0f;
    pps_cable_delay_offset = 0;
    oscillator_kind_read(true);
    efc_calibration_defaults();
    return false;
  }
  scpi_value_DIAGnostic_TOFFset = block->time_offset;
  scpi_value_DIAGnostic_ROSCillator_TCOefficient = block->tc_coefficient;
  union {
    uint32_t bits;
    float value;
  } second = {.bits = record_float_or_default(
                  records_float_bits(&block->tc_coefficient_1), 0)};

  tc_coefficient_1 = second.value;
  pps_cable_delay_offset = (uint16_t)record_word_or_default(block->field_0a, 0);
  oscillator_kind_read(false);
  efc_calibration_given = efc_calibration_read();
  return true;
}

/* The parameter record's first port's eight-bit flag as
 * save_parameter_record writes it, set (0x41a90). */
static const uint8_t RECORDS_SCI_EIGHT_BITS = 1;

/* Writes the SCI's settings to the parameter record, in both of its
 * ports' places, the first's field_05 and two_stop_bits 0 and its
 * eight_bits set; whether written (0x41a5e).  The Z3815A's writes DUART
 * channel B's in the first. */
bool save_parameter_record(void) {
  parameter_record record;

  memset(&record, RECORDS_ERASED, sizeof record);
  record.duartb_baud = sci_settings.baud;
  record.duartb_parity = sci_settings.parity;
  record.duartb_xon_xoff = sci_settings.xon_xoff;
  record.duartb_field_05 = 0;
  record.duartb_eight_bits = RECORDS_SCI_EIGHT_BITS;
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

/* Reads the SCI's settings from the parameter record's second port;
 * else takes their defaults, saves them, and returns 0.  When the record
 * is good it returns D3 as its caller left it, which it never sets, as
 * the 58503A 3704's (0x41afa).  The Z3815A's also reads DUART channel
 * B's. */
uint8_t parameter_record_read(void) {
  uint8_t result = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");

  if (record_checksum(&eeprom_parameter_record,
                      sizeof eeprom_parameter_record) !=
      eeprom_parameter_record.checksum) {
    memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
    save_parameter_record();
    return 0;
  }
  sci_settings.baud = eeprom_parameter_record.sci_baud;
  sci_settings.parity = eeprom_parameter_record.sci_parity;
  sci_settings.xon_xoff = eeprom_parameter_record.sci_xon_xoff;
  sci_settings.echo = eeprom_parameter_record.sci_echo;
  return result;
}

/* Loads the oscillator's kind from the block, or 0 for an erased byte
 * or when told to (0x418ec). */
void oscillator_kind_read(bool use_default) {
  const calibration_block* block = RECORDS_CALIBRATION_BLOCK;

  if (!use_default && block->oscillator != RECORDS_ERASED_BYTE) {
    oscillator_kind = block->oscillator;
  } else {
    oscillator_kind = 0;
  }
}

/* lifetime_update's clock event: takes the GPS date when the time is
 * valid and counts another lifetime (0x4164e). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* A record's word, or the default given while it is erased, 0xffff
 * (0x418d8). */
int16_t record_word_or_default(int16_t word, int16_t fallback) {
  static const int16_t RECORDS_ERASED_WORD = -1;

  return word != RECORDS_ERASED_WORD ? word : fallback;
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

/* The calibration block saved as a descriptor's hook (0x419ac). */
uint8_t calibration_block_save_reported(int32_t index, int32_t source) {
  return records_save_reported(source);
}

/* The done hook of :DIAG:ROSC:EFC:ASLOPe, :BSLOPe and :DADC: the
 * calibration given, then the block saved (0x419cc). */
uint8_t efc_calibration_done(int32_t index, int32_t source) {
  efc_calibration_given = 1;
  return records_save_reported(source);
}

/* Takes pll.efc into lifetime and writes the record; nothing calls it
 * (0x4163e). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* save_parameter_record, a memory error for the source when it
 * fails; nonzero once saved (0x41b74). */
uint8_t parameter_record_save_reported(request_source source) {
  if (!save_parameter_record()) {
    status_report_from(STATUS_MEMORY_ERROR, source);
    return 0;
  }
  return 1;
}

/* Sets the *IDN? model to its default (0x43cc8). */
void idn_model_default(void) {
  strncpy(idn_model, model_default_text, sizeof idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x43ce4). */
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
 * they differ from it; 1 when it holds them (0x43d88). */
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

/* :SYSTem:COMMunicate:SER1:PRESet's action for the SCI, the SCPI port:
 * its defaults, applied and saved; the index is not read (0x41b94), the
 * Z3815A's for DUART channel B. */
void duartb_settings_preset(int32_t index, int32_t source) {
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  duartb_settings_done();
  parameter_record_save_reported((request_source)source);
}

/* The EFC calibration's defaults, by the oscillator's kind: for kind 0
 * a 5e-13 and b 3.3e-24, else a -2.125e-12 and b 0; the DAC's 16 counts
 * per ADC count either way (0x417fa).  The Z3815A's picks a by the
 * switch word's bit 12, which this image does not have. */
void efc_calibration_defaults(void) {
  static const float A_KIND_0 = 5e-13f;
  static const float B_KIND_0 = 3.3e-24f;
  static const float A_OTHER = -2.125e-12f;
  static const float DAC_PER_ADC = 16.0f;

  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
  if (oscillator_kind == 0) {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = A_KIND_0;
    scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = B_KIND_0;
  } else {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = A_OTHER;
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = DAC_PER_ADC;
}

/* :DIAGnostic:ROSCillator:TYPE's done hook: the EFC calibration taken
 * afresh from the defaults for the new kind and saved, -311, Memory
 * error, for the source if the block could not be (0x419f4).  The
 * source is the second argument, as every descriptor's hook's, and the
 * index is not read (0x41a06). */
void oscillator_kind_done(int32_t index, int32_t source) {
  efc_calibration_given = 0;
  efc_calibration_defaults();
  if (save_calibration_block() == 0) {
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
}
