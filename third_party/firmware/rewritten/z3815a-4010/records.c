/* records.c's functions that are this image's own: the rest are
 * common/records.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  The lifetime record's update, the 58503A 3704's; the
 * calibration block, the 58503A 3704's with this image's EFC calibration
 * after its fields: :DIAG:ROSC:EFC:DADC, the EFC's DAC counts per ADC
 * count, and the OCXO calibration's a and b, :ASLOPe and :BSLOPe
 * (docs/firmware/z3815a-4010.md).  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

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
 * valid and counts another lifetime (0x44e9c). */
void lifetime_event_run(void) {
  if (time_valid()) {
    lifetime.date = get_gps_date();
  }
  records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x44ed2). */
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

/* The calibration block's field_0a when it holds no cable delay. */
static const int16_t RECORDS_NO_CABLE_DELAY = -1;

/* The bits of a long that efc_calibration_read takes for erased when all
 * are set: a float's sign and the top of its exponent (0x45076). */
static const uint32_t RECORDS_ERASED_FLOAT_BITS = 0xe8000000;

/* The EFC calibration's defaults (0x45038). */
static const float RECORDS_EFC_A_BIT_12 = 1.2e-12f;

static const float RECORDS_EFC_A = -2.125e-12f;

static const float RECORDS_EFC_DAC_PER_ADC = 16.0f;

/* A float the block holds, as the long the image reads. */
static uint32_t records_float_bits(const float* value) {
  return *(const uint32_t*)value;
}

/* Whether a float the block holds is erased. */
static bool records_erased_float(const float* value) {
  return (records_float_bits(value) & RECORDS_ERASED_FLOAT_BITS) ==
         RECORDS_ERASED_FLOAT_BITS;
}

/* Writes the time offset, the TC, the 1PPS cable delay, cut to a word,
 * and the EFC calibration, its a only while given, to the calibration
 * block when the block differs from it; 1 when the block holds them
 * (0x44f8a). */
uint32_t save_calibration_block(void) {
  calibration_block block;

  memset(&block, RECORDS_ERASED, sizeof block);
  block.time_offset = scpi_value_DIAGnostic_TOFFset;
  block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
  block.field_0a = (int16_t)pps_cable_delay_offset;
  block.efc_dac_per_adc = scpi_value_DIAGnostic_ROSCillator_EFControl_DADC;
  if (efc_calibration_given) {
    block.efc_a = scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe;
  } else {
    block.efc_a = 0.0f;
  }
  block.efc_b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  block.checksum = record_checksum(&block, sizeof block);
  if (memcmp(&block, RECORDS_CALIBRATION_BLOCK, sizeof block) == 0) {
    return 1;
  }
  return (uint8_t)eeprom_write((char*)RECORDS_CALIBRATION_BLOCK, (char*)&block,
                               sizeof block) != 0;
}

/* The EFC calibration's defaults: a by the switch word's bit 12, the
 * DAC's 16 counts per ADC count and b 0 (0x45038). */
void efc_calibration_defaults(void) {
  if ((switch_word & SWITCH_WORD_BIT_12) != 0) {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = RECORDS_EFC_A_BIT_12;
  } else {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = RECORDS_EFC_A;
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = RECORDS_EFC_DAC_PER_ADC;
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
}

/* Takes the EFC calibration from the block when none of its three is
 * erased and its a's long is not 0, else the defaults; true when taken
 * (0x4506c). */
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
  if (!valid) {
    efc_calibration_defaults();
    return false;
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = block->efc_dac_per_adc;
  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = block->efc_a;
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = block->efc_b;
  return true;
}

/* A record's word, or the default given while it is erased, 0xffff
 * (0x450d2). */
int16_t record_word_or_default(int16_t word, int16_t fallback) {
  static const int16_t RECORDS_ERASED_WORD = -1;

  return word != RECORDS_ERASED_WORD ? word : fallback;
}

/* Reads the time offset, the TC, the 1PPS cable delay and the EFC
 * calibration from the calibration block, or zeros and the calibration's
 * defaults; true when its checksum holds (0x450e6).  The delay is read
 * back as an unsigned word, as the 58503A 3704's, through
 * record_word_or_default, 0 while erased. */
bool calibration_block_read(void) {
  calibration_block* block = RECORDS_CALIBRATION_BLOCK;

  efc_calibration_given = 0;
  if (record_checksum(block, sizeof *block) != block->checksum) {
    scpi_value_DIAGnostic_TOFFset = 0.0f;
    scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
    pps_cable_delay_offset = 0;
    efc_calibration_defaults();
    return false;
  }
  scpi_value_DIAGnostic_TOFFset = block->time_offset;
  scpi_value_DIAGnostic_ROSCillator_TCOefficient = block->tc_coefficient;
  pps_cable_delay_offset = (uint16_t)record_word_or_default(block->field_0a, 0);
  efc_calibration_given = efc_calibration_read();
  return true;
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

/* The calibration block saved as a descriptor's hook (0x4515e). */
uint8_t calibration_block_save_reported(int32_t index, int32_t source) {
  return records_save_reported(source);
}

/* The done hook of :DIAG:ROSC:EFC:ASLOPe, :BSLOPe and :DADC: the
 * calibration given, then the block saved (0x4517e). */
uint8_t efc_calibration_done(int32_t index, int32_t source) {
  efc_calibration_given = 1;
  return records_save_reported(source);
}

/* records.c's functions that are this image's own: the rest are
 * common/records.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* DUART channel B's settings by default, 9600 baud, no parity, eight
 * bits, echo: the image's duartb_settings_defaults (0x44d90). */
static const duartb_settings_record records_duartb_defaults = {
    .baud = SERIAL_BAUD_9600,
    .parity = SERIAL_PARITY_NONE,
    .eight_bits = 1,
    .echo = 1,
};

/* Sets the *IDN? model to its default (0x475b6). */
void idn_model_default(void) { strncpy(idn_model, "Z3815A", sizeof idn_model); }

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x475d2). */
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
 * they differ from it; 1 when it holds them (0x47676). */
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
 * (0x44e8c). */
void lifetime_efc_save(void) {
  lifetime.efc = pll.efc;
  lifetime_record_write();
}

/* Writes both ports' settings to the parameter record in the EEPROM,
 * its unused bytes erased and its checksum made: DUART channel B's
 * settings' +2 to +8 and the SCI's +2 to +5; nonzero once written
 * (0x451e8).  The 58503A 3704's, whose SCI fields take channel B's
 * settings again, which this image's take from sci_settings
 * (0x45236 to 0x4524e). */
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

/* Reads both ports' settings from the parameter record; else takes their
 * defaults, saves them, and returns 0.  When the record is good it
 * returns D3 as its caller left it, which it never sets, as the 58503A
 * 3704's (0x45358) (0x4528e). */
uint8_t parameter_record_read(void) {
  uint8_t result = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");

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
  sci_settings.baud = eeprom_parameter_record.sci_baud;
  sci_settings.parity = eeprom_parameter_record.sci_parity;
  sci_settings.xon_xoff = eeprom_parameter_record.sci_xon_xoff;
  sci_settings.echo = eeprom_parameter_record.sci_echo;
  return result;
}

/* save_parameter_record, a memory error for the source when it
 * fails; nonzero once saved (0x45360). */
uint8_t parameter_record_save_reported(request_source source) {
  if (!save_parameter_record()) {
    status_report_from(STATUS_MEMORY_ERROR, source);
    return 0;
  }
  return 1;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action for channel B, the SCPI
 * port's: its defaults, applied and saved; the index is not read
 * (0x45380). */
void duartb_settings_preset(int32_t index, int32_t source) {
  memcpy(&duartb_settings, &duartb_settings_defaults, sizeof duartb_settings);
  duartb_port_settings_done();
  parameter_record_save_reported((request_source)source);
}

/* The same for the SCI, the console's, its settings applied by
 * duartb_settings_done, which reconfigures the SCI here (0x453b4). */
void sci_settings_preset(int32_t index, int32_t source) {
  memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
  duartb_settings_done();
  parameter_record_save_reported((request_source)source);
}
