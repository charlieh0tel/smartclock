/* What the EEPROM's records share (eeprom.c, log.c, nv.c, records.c, and
 * arithmetic.c's sign_extend_bits): an erased byte, the tag, a date's
 * and a time's fields, and the records' checksums. The exported C
 * includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it.
 * docs/firmware/structures.md describes the records ("EEPROM records",
 * "The NV register", "The lifetime record"). */

/* An erased EEPROM byte, which every writer fills its record with
 * before it fills the fields. */
static const uint8_t RECORDS_ERASED = 0xff;

/* The tag the NV register's and the identification record's writers
 * store, and the firmware revision the NV register's must match. */
static const char RECORDS_TAG[] = "S";

/* A date's and a time's fields, a byte each (docs/firmware/structures.md,
 * "A date and time"). */
static const uint8_t RECORDS_FIELD_BITS = 8;

static const uint32_t RECORDS_FIELD_MASK = 0xffu;

static const uint8_t RECORDS_LONG_BITS = 32;

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
