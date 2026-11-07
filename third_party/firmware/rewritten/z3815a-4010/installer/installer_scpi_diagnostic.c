/* installer_flash_program_word, installer_scpi_set_DIAGnostic_DOWNload,
 * installer_flash_erase_block, installer_scpi_set_DIAGnostic_ERASe,
 * installer_scpi_query_DIAGnostic_ERASe: this image's own, split from
 * common/installer/installer_scpi_diagnostic.c where it differs between the
 * images (rewrite.md, "Shared C", rule 6); and
 * installer_scpi_query_DIAGnostic_TEST, the same C in each image, which
 * names boot_selftest_results, a record each image's own, since this
 * image lays it out otherwise (rewrite.md, "Shared C").  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype.
 *
 * The flash is one part, 16 bits wide, from 0x0, which takes a command in a
 * word's low byte, Intel's command set as this code gives it, the part not
 * identified (docs/firmware/z3815a-4010.md): 0x50 clears its status
 * register, 0x40 and a word programs the word, 0x20 and 0xd0 erase a block,
 * and 0xff puts it back to reading; from a program or erase on, a read gives
 * the status register, bit 7 set once done.  The installer writes from
 * 0x20000, the first of three 128 KB blocks, to 0x7ffff, and never below,
 * where the boot code lives, with the output latch's bit 14 set. */

/* The writable range, its end not in it, and its blocks. */
static const uint32_t INSTALLER_FLASH_START = 0x20000;

static const uint32_t INSTALLER_FLASH_END = 0x80000;

static const uint32_t INSTALLER_FLASH_BLOCK_SIZE = 0x20000;

/* The commands, each in a word's low byte. */
static const uint16_t INSTALLER_FLASH_CLEAR_STATUS = 0x50;

static const uint16_t INSTALLER_FLASH_PROGRAM = 0x40;

static const uint16_t INSTALLER_FLASH_ERASE = 0x20;

static const uint16_t INSTALLER_FLASH_ERASE_CONFIRM = 0xd0;

static const uint16_t INSTALLER_FLASH_READ_ARRAY = 0xff;

/* The status register's bits: done; and the errors a program, and an
 * erase, checks. */
static const uint16_t INSTALLER_FLASH_READY = 0x80;

static const uint16_t INSTALLER_FLASH_PROGRAM_ERRORS = 0x18;

static const uint16_t INSTALLER_FLASH_ERASE_ERRORS = 0x38;

/* The output latch's bit the installer sets for each program and erase
 * and clears after, through installer_latch_bits_set, which sets the bits
 * given or clears them, and its set and clear. */
static const uint16_t INSTALLER_FLASH_LATCH_BIT = 0x4000;

static const char INSTALLER_FLASH_LATCH_SET = 1;

static const char INSTALLER_FLASH_LATCH_CLEAR = 0;

/* Programs a flash word from 0x20000 to 0x7ffff, with the latch's bit
 * set: clears the status, then the program command and the word, polling
 * up to 100 times for done, and trying up to 5 times while the status
 * shows an error; then puts the part back to reading.
 * STATUS_SCPI_DATA_OUT_OF_RANGE outside the range,
 * STATUS_SCPI_EXECUTION_ERROR while the last try's status shows an
 * error. */
int32_t installer_flash_program_word(uint16_t* address, uint16_t word) {
  static const int16_t POLLS = 100;
  const uint32_t at = (uint32_t)address;
  volatile uint16_t* target = address;
  int16_t tries = 0;
  bool programmed;

  if (at < INSTALLER_FLASH_START || at >= INSTALLER_FLASH_END) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  installer_latch_bits_set(INSTALLER_FLASH_LATCH_BIT,
                           INSTALLER_FLASH_LATCH_SET);
  do {
    *target = INSTALLER_FLASH_CLEAR_STATUS;
    *target = INSTALLER_FLASH_PROGRAM;
    *target = word;
    for (int16_t polls = 0;
         (*target & INSTALLER_FLASH_READY) == 0 && polls < POLLS; polls++) {
    }
    programmed = (*target & INSTALLER_FLASH_PROGRAM_ERRORS) == 0;
    tries++;
  } while (!programmed && tries < INSTALLER_SCPI_DIAGNOSTIC_TRIES);
  *target = INSTALLER_FLASH_READ_ARRAY;
  installer_latch_bits_set(INSTALLER_FLASH_LATCH_BIT,
                           INSTALLER_FLASH_LATCH_CLEAR);
  if (!programmed) {
    return STATUS_SCPI_EXECUTION_ERROR;
  }
  return 0;
}

/* :DOWNload "<S-record>": programs an S1, S2 or S3 record's data, a
 * word at a time, from its address, as does an S9, S8 or S7, with a
 * 16-, 24- or 32-bit address as theirs, for whatever data its count
 * gives; S0 and S5 do nothing, and another type, a bad checksum, a count
 * that is not the record's length or an address outside the writable
 * range is out of range (097-59551-02, 5-89 to 5-91), the 58503A 3704's
 * over this image's range.  An odd last byte is programmed with 0xff
 * after it, which leaves the flash's other byte as erased. */
int32_t installer_scpi_set_DIAGnostic_DOWNload(void* parameters, void* response,
                                               scpi_call* call) {
  /* "S" and the type, before the count's two digits; the checksum's two
   * digits, after the data. */
  static const int32_t TYPE_DIGITS = 2;
  static const int32_t CHECKSUM_DIGITS = 2;
  static const int32_t BYTE_DIGITS = 2;
  static const int32_t WORD_DIGITS = 4;
  /* What follows the last byte, the value an odd last byte is
   * programmed with beside it. */
  static const uint16_t ERASED_BYTE = 0xff;
  const scpi_text_parameter* given = parameters;
  char text[INSTALLER_SCPI_DIAGNOSTIC_TEXT_SIZE];
  uint16_t bytes[INSTALLER_SCPI_DIAGNOSTIC_BYTES_SIZE];
  int32_t address_digits;

  /* Unbounded, as the image's: a text of 512 characters or more runs
   * past its room (docs/firmware/quirks.md). */
  libc_copy(text, given->text);
  if (text[0] != 'S') {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  switch (text[1]) {
    case '0':
    case '5':
      return 0;
    case '1':
    case '9':
      address_digits = 4;
      break;
    case '2':
    case '8':
      address_digits = 6;
      break;
    case '3':
    case '7':
      address_digits = 8;
      break;
    default:
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  const int32_t length = libc_length(text);
  char pair[INSTALLER_SCPI_DIAGNOSTIC_PAIR_SIZE];
  uint32_t sum = 0;
  int32_t count = 0;

  /* The count's, the address's and the data's digits, in pairs. */
  for (int32_t digit = 0; digit < length - TYPE_DIGITS - CHECKSUM_DIGITS;
       digit += BYTE_DIGITS) {
    pair[0] = text[TYPE_DIGITS + digit];
    pair[1] = text[TYPE_DIGITS + digit + 1];
    pair[2] = 0;
    const int32_t byte = installer_strtol(pair, 0, LIBC_HEXADECIMAL);

    sum += byte;
    bytes[count++] = byte;
  }
  bytes[count] = ERASED_BYTE;
  pair[0] = text[length - CHECKSUM_DIGITS];
  pair[1] = text[length - CHECKSUM_DIGITS + 1];
  pair[2] = 0;
  if ((~sum & CPU_BYTE_MASK) != installer_strtol(pair, 0, LIBC_HEXADECIMAL)) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  /* The count, of the address's, the data's and the checksum's bytes. */
  const uint32_t record_bytes = bytes[0];

  if (record_bytes * BYTE_DIGITS + TYPE_DIGITS + BYTE_DIGITS != length) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  const int32_t address_bytes = address_digits / BYTE_DIGITS;
  uint32_t address = 0;

  for (int32_t index = 0; index < address_bytes; index++) {
    address = (address << CPU_BYTE_BITS) + bytes[1 + index];
  }
  const int32_t data_digits =
      record_bytes * BYTE_DIGITS - address_digits - CHECKSUM_DIGITS;
  int32_t index = 1 + address_bytes;

  for (int32_t digit = 0; digit < data_digits; digit += WORD_DIGITS) {
    const uint32_t word = (bytes[index] << CPU_BYTE_BITS) + bytes[index + 1];

    if (address < INSTALLER_FLASH_START || address >= INSTALLER_FLASH_END) {
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
    }
    const int32_t error =
        installer_flash_program_word((uint16_t*)address, (uint16_t)word);

    if (error != 0) {
      return error;
    }
    index += 2;
    address += sizeof(uint16_t);
  }
  return 0;
}

/* Erases the flash's block at block, with the latch's bit set: clears
 * the status, then the erase command and its confirmation, polling up to
 * 12900000 times for done, and trying up to 5 times while the status
 * shows an error; then puts the part back to reading.  Whether the last
 * try's status shows none. */
bool installer_flash_erase_block(uint16_t* block) {
  static const uint32_t POLLS = 12900000;
  volatile uint16_t* target = block;
  uint32_t tries = 0;
  bool erased;

  installer_latch_bits_set(INSTALLER_FLASH_LATCH_BIT,
                           INSTALLER_FLASH_LATCH_SET);
  do {
    tries++;
    *target = INSTALLER_FLASH_CLEAR_STATUS;
    *target = INSTALLER_FLASH_ERASE;
    *target = INSTALLER_FLASH_ERASE_CONFIRM;
    for (uint32_t polls = 0;
         (*target & INSTALLER_FLASH_READY) == 0 && polls < POLLS; polls++) {
    }
    erased = (*target & INSTALLER_FLASH_ERASE_ERRORS) == 0;
  } while (!erased && tries < INSTALLER_SCPI_DIAGNOSTIC_TRIES);
  *target = INSTALLER_FLASH_READ_ARRAY;
  installer_latch_bits_set(INSTALLER_FLASH_LATCH_BIT,
                           INSTALLER_FLASH_LATCH_CLEAR);
  return erased;
}

/* :ERASe: erases the writable range's three blocks in turn, stopping at
 * the first that fails, STATUS_SCPI_EXECUTION_ERROR. */
int32_t installer_scpi_set_DIAGnostic_ERASe(void* parameters, void* response,
                                            scpi_call* call) {
  for (int32_t block = INSTALLER_FLASH_START; block < INSTALLER_FLASH_END;
       block += INSTALLER_FLASH_BLOCK_SIZE) {
    if (!installer_flash_erase_block((uint16_t*)block)) {
      return STATUS_SCPI_EXECUTION_ERROR;
    }
  }
  return 0;
}

/* :ERASe?: 1 when every word of the writable range reads 0xffff, else 0,
 * the others' over this image's range.  The image first clears its
 * parameters' argument slot, which nothing reads. */
int32_t installer_scpi_query_DIAGnostic_ERASe(void* parameters, void* response,
                                              scpi_call* call) {
  static const uint16_t ERASED = 0xffff;
  int16_t* blank = response;
  uint32_t at = INSTALLER_FLASH_START;

  while (at < INSTALLER_FLASH_END &&
         *installer_scpi_diagnostic_word(at) == ERASED) {
    at += sizeof(uint16_t);
  }
  *blank = at == INSTALLER_FLASH_END;
  return 0;
}

/* :TEST?'s response: three longs, each a 6-byte item. */
typedef struct installer_scpi_diagnostic_long {
  uint32_t value;  /* +0x00 */
  uint16_t unused; /* +0x04 */
} installer_scpi_diagnostic_long;

_Static_assert(sizeof(installer_scpi_diagnostic_long) == 6,
               "installer_scpi_diagnostic_long");

/* :TEST? n: the boot code's self-test results, boot_selftest, by the
 * choice: for the summary a bit of each test that failed, else the
 * test's own word, the RAM's with its failed data bits and address
 * lines, the other two 0. */
int32_t installer_scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                             scpi_call* call) {
  /* The summary's bits. */
  static const uint32_t CHECKSUM_FAILED = 1 << 0;
  static const uint32_t CPU_FAILED = 1 << 1;
  static const uint32_t RAM_FAILED = 1 << 2;
  static const uint32_t UART_FAILED = 1 << 3;
  const scpi_short_parameter* given = parameters;
  installer_scpi_diagnostic_long* results = response;
  uint32_t result = 0;
  uint32_t data_bits = 0;
  uint32_t address_lines = 0;

  switch (given->value) {
    case INSTALLER_TEST_SUMMARY:
      if (boot_selftest.checksum != 0) {
        result |= CHECKSUM_FAILED;
      }
      if (boot_selftest.cpu != 0) {
        result |= CPU_FAILED;
      }
      if (boot_selftest.ram != 0) {
        result |= RAM_FAILED;
      }
      if (boot_selftest.uart != 0) {
        result |= UART_FAILED;
      }
      break;
    case INSTALLER_TEST_CHECKSUM:
      result = (uint16_t)boot_selftest.checksum;
      break;
    case INSTALLER_TEST_CPU:
      result = (uint16_t)boot_selftest.cpu;
      break;
    case INSTALLER_TEST_RAM:
      result = boot_selftest.ram;
      data_bits = boot_selftest.ram_data_bits;
      address_lines = boot_selftest.ram_address_lines;
      break;
    case INSTALLER_TEST_UART:
      result = (uint16_t)boot_selftest.uart;
      break;
    default:
      return STATUS_SCPI_EXECUTION_ERROR;
  }
  results[0].value = result;
  results[1].value = data_bits;
  results[2].value = address_lines;
  return 0;
}
