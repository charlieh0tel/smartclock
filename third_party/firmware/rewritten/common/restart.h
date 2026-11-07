/* What the boot code's and the primary's unpackers of the installer's
 * records share (boot.c and each image's own boot.c,
 * docs/firmware/restart.md): the records' marks, the unpackers'
 * results and the unpacker, which both copies call.  The exported C includes
 * the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The records' marks (restart.md): an S first, then each C record a
 * long length, a long destination, both high byte first, and that many
 * bytes, until an E. */
enum {
  BOOT_RECORDS_START = 'S',
  BOOT_RECORD_COPY = 'C',
  BOOT_RECORDS_END = 'E',
};

/* What the unpackers return. */
enum {
  BOOT_UNPACKED = 0,
  BOOT_UNPACK_NO_START = 1,
  BOOT_UNPACK_BAD_RECORD = 2,
};

/* The shortest records an unpacker looks at, S and E. */
static const int32_t BOOT_RECORDS_LEAST = 2;

/* A long, high byte first, at a cursor, which it advances past it. */
static uint32_t boot_read_long(const uint8_t** cursor) {
  uint32_t value = 0;

  for (int32_t i = 0; i < CPU_LONG_BYTES; i++) {
    value = value << CPU_BYTE_BITS | *(*cursor)++;
  }
  return value;
}

/* Unpacks records of a size from a cursor: the boot code's copies with
 * boot_memcpy, the primary's with memcpy. */
static uint32_t boot_unpack(const uint8_t* cursor, int32_t size,
                            bool with_memcpy) {
  if (size <= BOOT_RECORDS_LEAST) {
    return BOOT_UNPACKED;
  }
  if (*cursor++ != BOOT_RECORDS_START) {
    return BOOT_UNPACK_NO_START;
  }
  while (*cursor != BOOT_RECORDS_END) {
    if (*cursor++ != BOOT_RECORD_COPY) {
      return BOOT_UNPACK_BAD_RECORD;
    }
    const uint32_t length = boot_read_long(&cursor);
    uint8_t* destination = (uint8_t*)boot_read_long(&cursor);

    if (with_memcpy) {
      memcpy(destination, (uint8_t*)cursor, length);
    } else {
      boot_memcpy(destination, (uint8_t*)cursor, length);
    }
    cursor += length;
  }
  return BOOT_UNPACKED;
}
