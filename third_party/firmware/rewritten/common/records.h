/* What the EEPROM's records share (eeprom.c, log.c, nv.c, records.c, and
 * each image's own, and arithmetic.c's sign_extend_bits): an erased
 * byte, a date's and a time's fields, the records' checksums, the
 * EEPROM's page and tries, the writing flag's store, and the logs'
 * sizes; the image's own records.h has its tag and firmware revision.  The
 * exported C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it.
 * docs/firmware/structures.md describes the records ("EEPROM records", "The NV
 * register", "The lifetime record"). */

/* An erased EEPROM byte, which every writer fills its record with
 * before it fills the fields. */
static const uint8_t RECORDS_ERASED = 0xff;

/* log.c's results that the :LOG setters tell apart: the log full
 * (log_append), a page write failed, the low byte of
 * LOG_PAGE_WRITE_FAILED, which log_erase has already reported, and a
 * count not the log's (log_erase_checked). */
static const int16_t RECORDS_LOG_FULL = 0xfc;

static const int16_t RECORDS_LOG_WRITE_FAILED = 0xfe;

static const int16_t RECORDS_LOG_COUNT_DIFFERS = 0xff;

/* The identification record's default suffix. */
static const char RECORDS_DEFAULT_SUFFIX = 'A';

/* A date's and a time's fields, a byte each (docs/firmware/structures.md,
 * "A date and time"). */
static const uint8_t RECORDS_FIELD_BITS = 8;

static const uint32_t RECORDS_FIELD_MASK = 0xffu;

/* The sum of so many bytes, as every record checksum takes it. */
static uint8_t records_byte_sum(const uint8_t* bytes, uint32_t count) {
  uint8_t sum = 0;

  for (uint32_t i = 0; i < count; i++) {
    sum += bytes[i];
  }
  return sum;
}

/* A record's checksum word: the sum in its high byte, its complement in
 * the low. */
static uint16_t records_sum_and_complement(uint8_t sum) {
  return (uint16_t)(sum << RECORDS_FIELD_BITS | (uint8_t)~sum);
}

/* sign_extend_bits' and its installer copy's body: a value of so many
 * bits sign-extended to a long. */
static uint32_t records_sign_extend_bits(uint32_t value, uint8_t width) {
  if (width == 8) {
    return (uint32_t)(int8_t)value;
  }
  if (width == 16) {
    return (uint32_t)(int16_t)value;
  }
  if (width == 0 || width >= CPU_LONG_BITS) {
    return value;
  }
  if (value & (uint32_t)1 << (width - 1)) {
    value |= ~(uint32_t)0 << width;
  }
  return value;
}

/* record_checksum's and its installer copy's body: the sum and
 * its complement of a record's bytes after its first two. */
static uint16_t records_checksum(void* record, uint16_t length) {
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

/* The EEPROM writes a page of 64 bytes at once (eeprom_write_page). */
static const uint32_t EEPROM_PAGE_SIZE = 64u;

/* Tries at a write, and polls of the last byte written for it to read
 * back, on each try. */
static const int32_t EEPROM_TRIES = 5;

static const int16_t EEPROM_POLLS = 5000;

/* Sets or clears eeprom_writing.  eeprom_lock reads it at a raised
 * interrupt mask, so the store must happen where the image makes it,
 * though the flag is cleared again before anything else here reads it. */
static void records_set_writing(bool writing) {
  *(volatile uint8_t*)&eeprom_writing = writing;
}

/* The reset-cause log's records. */
static const int32_t RECORDS_RESET_RECORDS =
    (int32_t)(sizeof eeprom_reset_records / sizeof eeprom_reset_records[0]);

static const int32_t RECORDS_LOG_PAGE_ENTRIES =
    (int32_t)(sizeof log_write_page.entries / sizeof log_write_page.entries[0]);
