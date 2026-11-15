/* The 58503B 1.01.04's NV register slots' erase and restore, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/58503b-1.01.04.md, "The NV records"),
 * each keeping its generated prototype.  The decompiler compared the
 * tag's signed char with an int of 0xf0 to 0xff, which gcc takes as
 * never equal, and read the two slots' tags as values gcc may load in
 * either order, where the image reads slot 0's first.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

/* An erased slot's tag byte: these bits set over nv_settings.gps_utc,
 * which nv_restore takes back from two erased slots that agree. */
static const uint8_t NV_ERASED_BITS = 0xf0;

/* Erases an NV register slot: its tag's first byte made
 * nv_settings.gps_utc with NV_ERASED_BITS set, unless it already is
 * (0x42a44). */
void nv_slot_erase(uint8_t slot) {
  const char erased = (char)(nv_settings.gps_utc | NV_ERASED_BITS);

  if (nv_records[slot].tag[0] != erased) {
    eeprom_write_byte(nv_records[slot].tag, erased);
  }
}

/* Loads the settings from slot 0, else slot 1, erasing slot 0; else
 * takes nv_settings_defaults, sets nv_settings_defaulted, fails the
 * self-test's EEPROM word when neither slot holds a record, keeps the GPS's UTC
 * setting, 2 or 3, that two erased slots' tags agree on, saves the
 * defaults to slot 0 when it is erased or no newer than slot 1, else to
 * slot 1, erasing the other, and reports the configuration lost
 * (0x42a8c). */
void nv_restore(void) {
  /* The low bits of an erased tag, and the two UTC settings it keeps. */
  static const uint8_t NV_ERASED_SETTING = 0x0f;
  static const uint8_t NV_UTC_KEPT_FIRST = 2;
  static const uint8_t NV_UTC_KEPT_LAST = 3;
  /* A sequence number as erased EEPROM reads. */
  static const uint32_t NV_SEQUENCE_ERASED = 0xffffffff;
  const volatile char* const tags[] = {nv_records[0].tag, nv_records[1].tag};
  const char tag_0 = *tags[0];
  const char tag_1 = *tags[1];

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
  memcpy(&nv_settings, &nv_settings_defaults, sizeof nv_settings);
  if (tag_1 == tag_0 && ((uint8_t)tag_1 & NV_ERASED_BITS) == NV_ERASED_BITS) {
    const uint8_t setting = (uint8_t)tag_1 & NV_ERASED_SETTING;

    if (setting == NV_UTC_KEPT_FIRST || setting == NV_UTC_KEPT_LAST) {
      nv_settings.gps_utc = setting;
    }
  }
  /* Read as the image reads them: slot 0's, then slot 1's and slot 0's
   * again (0x42b44 to 0x42b50). */
  const volatile int32_t* const sequences[] = {&nv_records[0].sequence,
                                               &nv_records[1].sequence};
  bool keep_0 = (uint32_t)*sequences[0] == NV_SEQUENCE_ERASED;

  if (!keep_0) {
    const uint32_t sequence_1 = (uint32_t)*sequences[1];

    keep_0 = sequence_1 >= (uint32_t)*sequences[0] &&
             sequence_1 != NV_SEQUENCE_ERASED;
  }
  if (keep_0) {
    nv_slot = 0;
    nv_slot_erase(1);
  } else {
    nv_slot = 1;
    nv_slot_erase(0);
  }
  nv_save(REQUEST_SOURCE_STARTUP);
  status_report_from(STATUS_CONFIGURATION_LOST, REQUEST_SOURCE_STARTUP);
}

/* Whether either NV register slot holds the tag and a good checksum,
 * whatever its revision (0x42e8a). */
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

/* The sum and its complement of an NV register record's bytes from its
 * revision on (0x42b8a). */
uint16_t nv_checksum(nv_record* record) {
  return records_sum_and_complement(records_byte_sum(
      (const uint8_t*)record->revision,
      sizeof *record - __builtin_offsetof(nv_record, revision)));
}

/* Nonzero when two NV register records, taken as longs, differ other than
 * in their sequence numbers (0x42bb8). */
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

/* 0 when slot nv_slot holds the tag, a good checksum and this firmware's
 * revision, else 1 (0x42e1c). */
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

/* Clears the NV settings, makes the battery-backed RAM's checksum good
 * with its restore flag clear, so that system_init does not restore it,
 * and restarts through trap #12 (docs/firmware/restart.md); the Z3817A
 * 3752's over this image's preserved_region (0x4ae10). */
void nv_clear_and_reset(void) {
  clear_nv();
  enter_supervisor_mode();
  preserved_region.restore = PRESERVED_REGION_DISCARD;
  preserved_region.checksum =
      record_checksum(&preserved_region, sizeof preserved_region);
  __m68k_trap(12);
}

/* The NV register's settings, packed into longs most significant bit
 * first; a value that would cross a long's end starts the next. */
typedef struct records_bit_stream {
  uint32_t* next; /* the long to store or load next */
  uint32_t bits;  /* the long being filled or emptied */
  uint8_t used;   /* its bits filled or emptied */
} records_bit_stream;

/* A long as erased EEPROM reads, which fills the record past the
 * settings. */
static const uint32_t RECORDS_ERASED_LONG = 0xffffffffu;

/* Stores the long being filled, its bits moved to the top. */
static void records_pack_flush(records_bit_stream* stream) {
  if (stream->used > 0 && stream->used < CPU_LONG_BITS) {
    stream->bits <<= CPU_LONG_BITS - stream->used;
  }
  *stream->next++ = stream->bits;
  stream->bits = 0;
  stream->used = 0;
}

/* The low n bits, n 0 to 32: the image's low_bit_masks (0x41570). */
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
 * the image's own nv_numeric_params and nv_setting_params, 23 and 19,
 * its loops' bounds, read in place. */
static const uint32_t RECORDS_NV_NUMERIC_PARAMS =
    (uint32_t)(sizeof nv_numeric_params / sizeof nv_numeric_params[0]);

static const uint32_t RECORDS_NV_SETTING_PARAMS =
    (uint32_t)(sizeof nv_setting_params / sizeof nv_setting_params[0]);

/* Packs the settings into an NV register record and writes it to slot
 * nv_slot when it differs; a memory error for the source when the write
 * fails (0x42be4).  The Z3817A 3752's, relocated, this image's tag and
 * revision. */
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
      log_event(LOG_EEPROM_SAVE_FAILED);
    }
    status_report_from(STATUS_MEMORY_ERROR, source);
  }
  return saved;
}

/* Loads the settings from slot nv_slot when its record is good; 1 when it
 * is (0x42ee8).  The Z3817A 3752's, relocated. */
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
