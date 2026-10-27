/* The NV register, the settings it keeps and its two slots, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `services/nv`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

static const uint32_t RECORDS_ERASED_LONG = 0xffffffffu;

/* The NV register's settings, packed into longs most significant bit
 * first; a value that would cross a long's end starts the next. */
typedef struct records_bit_stream {
  uint32_t* next; /* the long to store or load next */
  uint32_t bits;  /* the long being filled or emptied */
  uint8_t used;   /* its bits filled or emptied */
} records_bit_stream;

/* Stores the long being filled, its bits moved to the top. */
static void records_pack_flush(records_bit_stream* stream) {
  if (stream->used > 0 && stream->used < CPU_LONG_BITS) {
    stream->bits <<= CPU_LONG_BITS - stream->used;
  }
  *stream->next++ = stream->bits;
  stream->bits = 0;
  stream->used = 0;
}

/* The low n bits, n 0 to 32: the image's low_bit_masks (0x314b2). */
static const uint32_t records_low_bit_masks[] = {
    0x0,        0x1,        0x3,        0x7,       0xf,       0x1f,
    0x3f,       0x7f,       0xff,       0x1ff,     0x3ff,     0x7ff,
    0xfff,      0x1fff,     0x3fff,     0x7fff,    0xffff,    0x1ffff,
    0x3ffff,    0x7ffff,    0xfffff,    0x1fffff,  0x3fffff,  0x7fffff,
    0xffffff,   0x1ffffff,  0x3ffffff,  0x7ffffff, 0xfffffff, 0x1fffffff,
    0x3fffffff, 0x7fffffff, 0xffffffff,
};

/* Packs a value's low bits. */
static void records_pack(records_bit_stream* stream, uint32_t value,
                         uint8_t width) {
  if ((uint8_t)(width + stream->used) > CPU_LONG_BITS) {
    records_pack_flush(stream);
  }
  stream->bits = (width >= CPU_LONG_BITS ? 0 : stream->bits << width) |
                 (value & records_low_bit_masks[width]);
  stream->used += width;
}

/* Unpacks a value of so many bits.  For 32 the image takes the long as it
 * is; rotating it a whole turn is the same. */
static uint32_t records_unpack(records_bit_stream* stream, uint8_t width) {
  if ((uint8_t)(width + stream->used) > CPU_LONG_BITS) {
    stream->used = 0;
    stream->bits = *stream->next++;
  }
  stream->bits = rotate_left(stream->bits, width);
  stream->used += width;
  return stream->bits & records_low_bit_masks[width];
}

/* A value's bits in the NV register, from a descriptor's widths. */
static uint8_t records_nv_width(const int8_t* widths, int32_t index) {
  return (uint8_t)abs(widths[index]);
}

/* The descriptors whose values the NV register keeps, in its order, are
 * the image's own nv_numeric_params and nv_setting_params (0x32d62,
 * 0x32dbe), read in place: six of them have no name yet (0x33dd2,
 * 0x33d94, 0x34082, 0x340c0, 0x33a42, 0x33f6c). */
static const uint32_t RECORDS_NV_NUMERIC_PARAMS =
    (uint32_t)(sizeof nv_numeric_params / sizeof nv_numeric_params[0]);

static const uint32_t RECORDS_NV_SETTING_PARAMS =
    (uint32_t)(sizeof nv_setting_params / sizeof nv_setting_params[0]);

/* Erases an NV register slot's tag, if it is not erased (0x32758). */
void nv_slot_erase(uint8_t slot) {
  if ((uint8_t)nv_records[slot].tag[0] != RECORDS_ERASED) {
    eeprom_write_byte(nv_records[slot].tag, (char)RECORDS_ERASED);
  }
}

/* Clears the NV settings, makes the battery-backed RAM's checksum good
 * with its restore flag clear, so that system_init does not restore it,
 * and restarts through trap #12 (docs/firmware/restart.md) (0x45e66). */
void nv_clear_and_reset(void) {
  clear_nv();
  enter_supervisor_mode();
  preserved_region.restore = PRESERVED_REGION_DISCARD;
  preserved_region.checksum =
      record_checksum(&preserved_region, sizeof preserved_region);
  __m68k_trap(12);
}

/* Whether either NV register slot holds the tag and a good checksum,
 * whatever its revision (0x32b62). */
uint8_t nv_records_valid(void) {
  static const uint8_t SLOTS = sizeof nv_records / sizeof nv_records[0];

  for (uint8_t slot = 0; slot < SLOTS; slot++) {
    nv_record* record = &nv_records[slot];

    if (strncmp(record->tag, (char*)RECORDS_TAG, sizeof record->tag) == 0 &&
        nv_checksum(record) == record->checksum) {
      return 1;
    }
  }
  return 0;
}

/* Loads the settings from slot 0, else slot 1; else takes their defaults,
 * sets nv_settings_defaulted, fails the EEPROM's self-test when neither
 * slot holds a record, saves the defaults to slot 0 when it is erased or
 * no newer than slot 1, else to slot 1, erasing the other, and reports
 * the configuration lost (0x32796). */
void nv_restore(void) {
  /* nv_settings by default: the image's nv_settings_defaults (0x3085a),
   * its pads 0xff. */
  static const nv_settings_fields NV_DEFAULTS = {
      .elevation_mask = 10,
      .douglas_duration = 7200,
      .questionable_hysteresis = 60,
      .holdover_status_threshold = 86400,
      .limit_threshold = 1e-3f,
      .holdover_limit_threshold = 1e-6f,
      .prediction_duration = 86400,
      .leap_date = 1994 << 16 | 1 << 8 | 1,
      .leap_minute_length = 60,
      .raim_state = {1, 0},
      .pad_4d = 0xff,
      .raim_threshold = {10, 20},
      .holdover_measurement_threshold = 25200,
      .status_enable = {0x88, 0, 0x24, 0x3, 0x7, 0x8, 0x1fdf},
      .status_positive_transition = {0x7f, 0x2, 0x7, 0xf, 0x13df},
      .serial_restricted = 1,
      .pad_a1 = 0xff,
      .pulse_period = 1,
      .pulse_start_date = {1994, 1, 1},
      .antenna_current_alarm = 1,
      .douglas_state_powerup = 1,
      .tcode_format = TIMECODE_T2,
      .gps_utc = 1,
  };

  selftest.eeprom = 0;
  nv_settings_defaulted = 0;
  nv_slot = 0;
  if (nv_load_slot()) {
    return;
  }
  nv_slot = 1;
  if (nv_load_slot()) {
    nv_slot_erase(0);
    return;
  }

  nv_settings_defaulted = 1;
  if (!nv_records_valid()) {
    selftest.eeprom = SELFTEST_FAILED;
  }
  memcpy(&nv_settings, &NV_DEFAULTS, sizeof nv_settings);
  uint32_t sequence_0 = (uint32_t)nv_records[0].sequence;
  uint32_t sequence_1 = (uint32_t)nv_records[1].sequence;
  if (sequence_0 == RECORDS_ERASED_LONG ||
      (sequence_0 <= sequence_1 && sequence_1 != RECORDS_ERASED_LONG)) {
    nv_slot = 0;
  } else {
    nv_slot = 1;
  }
  nv_slot_erase(1 - nv_slot);
  nv_save(REQUEST_SOURCE_STARTUP);
  status_report_from(STATUS_CONFIGURATION_LOST, REQUEST_SOURCE_STARTUP);
}

/* The sum and its complement of an NV register record's bytes from its
 * revision on (0x32862). */
uint16_t nv_checksum(nv_record* record) {
  return records_sum_and_complement(records_byte_sum(
      (const uint8_t*)record->revision,
      sizeof *record - __builtin_offsetof(nv_record, revision)));
}

/* Nonzero when two NV register records, taken as longs, differ other than
 * in their sequence numbers (0x32890). */
uint32_t nv_record_differs(int32_t* first, int32_t* second) {
  /* nv_record's sequence number, as nv_record_differs takes it, a
   * long. */
  static const int32_t NV_SEQUENCE_LONG = 1;
  static const int32_t NV_LONGS =
      (int32_t)(sizeof(nv_record) / sizeof(int32_t));

  if (first[0] != second[0]) {
    return 1;
  }
  for (int32_t i = NV_SEQUENCE_LONG + 1; i < NV_LONGS; i++) {
    if (first[i] != second[i]) {
      return 1;
    }
  }
  return 0;
}

/* Packs the settings into an NV register record and writes it to slot
 * nv_slot when it differs; a memory error for the source when the write
 * fails (0x328bc). */
char nv_save(request_source source) {
  static const uint32_t NV_PACKED_LONGS =
      (uint32_t)(sizeof nv_records[0].packed / sizeof nv_records[0].packed[0]);
  nv_record record;
  records_bit_stream stream = {.next = record.packed};

  strncpy(record.tag, (char*)RECORDS_TAG, sizeof record.tag);
  record.sequence = nv_records[nv_slot].sequence + 1;
  strncpy(record.revision, (char*)RECORDS_FIRMWARE_REVISION,
          sizeof record.revision);
  for (uint32_t i = 0; i < RECORDS_NV_NUMERIC_PARAMS; i++) {
    scpi_numeric_param* param = nv_numeric_params[i];
    uint32_t* values = param->value;
    for (int32_t v = 0; v <= (int8_t)param->last_index; v++) {
      records_pack(&stream, values[v], records_nv_width(param->nv_widths, v));
    }
  }
  for (uint32_t i = 0; i < RECORDS_NV_SETTING_PARAMS; i++) {
    scpi_setting_param* param = nv_setting_params[i];
    for (int32_t v = 0; v <= (int8_t)param->last_index; v++) {
      records_pack(&stream, (uint32_t)(int8_t)param->value[v],
                   records_nv_width(param->nv_widths, v));
    }
  }
  if (stream.used != 0) {
    records_pack_flush(&stream);
  }

  uint32_t* end = record.packed + NV_PACKED_LONGS;
  if (stream.next >= end && kernel_running) {
    task_debug_message("Warning: Entire nv register is being used\r\n");
  }
  while (stream.next < end) {
    *stream.next++ = RECORDS_ERASED_LONG;
  }
  record.checksum = nv_checksum(&record);

  /* nv_record_differs takes the records as longs. */
  char saved = 1;
  if (nv_record_differs((int32_t*)&nv_records[nv_slot], (int32_t*)&record)) {
    saved = eeprom_write(nv_records[nv_slot].tag, record.tag, sizeof record);
  }
  if (!saved) {
    if (kernel_running) {
      log_event_58503a(LOG_EEPROM_SAVE_FAILED);
    }
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
  return saved;
}

/* 0 when slot nv_slot holds the tag, a good checksum and this firmware's
 * revision, else 1 (0x32af4). */
uint8_t nv_slot_valid(void) {
  nv_record* record = &nv_records[nv_slot];

  if (strncmp(record->tag, (char*)RECORDS_TAG, sizeof record->tag) == 0 &&
      nv_checksum(record) == record->checksum &&
      strncmp(record->revision, (char*)RECORDS_FIRMWARE_REVISION,
              sizeof record->revision) == 0) {
    return 0;
  }
  return 1;
}

/* Loads the settings from slot nv_slot when its record is good; 1 when it
 * is (0x32bc0). */
uint32_t nv_load_slot(void) {
  if (nv_slot_valid() != 0) {
    return 0;
  }

  nv_record record;
  memcpy(&record, &nv_records[nv_slot], sizeof record);
  records_bit_stream stream = {.next = record.packed + 1,
                               .bits = record.packed[0]};

  for (uint32_t i = 0; i < RECORDS_NV_NUMERIC_PARAMS; i++) {
    scpi_numeric_param* param = nv_numeric_params[i];
    uint32_t* values = param->value;
    for (int32_t v = 0; v <= (int8_t)param->last_index; v++) {
      uint8_t width = records_nv_width(param->nv_widths, v);
      uint32_t value = records_unpack(&stream, width);
      values[v] =
          param->nv_widths[v] < 0 ? sign_extend_bits(value, width) : value;
    }
  }
  nv_numeric_bytes_loaded =
      (int16_t)((stream.next - record.packed) * sizeof *record.packed);
  for (uint32_t i = 0; i < RECORDS_NV_SETTING_PARAMS; i++) {
    scpi_setting_param* param = nv_setting_params[i];
    for (int32_t v = 0; v <= (int8_t)param->last_index; v++) {
      param->value[v] = (uint8_t)records_unpack(
          &stream, records_nv_width(param->nv_widths, v));
    }
  }
  nv_bytes_loaded =
      (int16_t)((stream.next - record.packed) * sizeof *record.packed);
  return 1;
}
