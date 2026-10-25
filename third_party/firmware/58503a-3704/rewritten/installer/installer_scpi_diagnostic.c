/* The installer's handlers of its SCPI :DIAGnostic tree, :DOWNload,
 * :ERASe and :TEST? (`applications/scpi/handlers/diagnostic`;
 * scpi_diagnostic.c has the primary's), and the flash word programmer
 * :DOWNload calls.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.
 *
 * The flash is two pairs of AM29F010s, word-interleaved, one at 0x0 and
 * one at 0x40000 (docs/firmware/restart.md, "The installer"): a word
 * written at a pair's byte address twice a part's address reaches both
 * parts of the pair at once, so that each command word holds the
 * command in both bytes.  The installer writes from 0x10000 to 0x7ffff
 * and never below, where it and the boot code live. */

/* The writable range, its end not in it. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_FLASH_START = 0x10000;

static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_FLASH_END = 0x80000;

/* The second pair's base; the first's is 0. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_PAIR_2 = 0x40000;

/* A pair's two unlock addresses, the parts' 0x5555 and 0x2aaa. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST = 0x5555 * 2;

static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND = 0x2aaa * 2;

/* The command words, each a part's command in both bytes: the unlock
 * cycles' data, then the commands the third cycle gives. */
static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST_DATA = 0xaaaa;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND_DATA = 0x5555;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_ERASE_SETUP = 0x8080;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_SECTOR_ERASE = 0x3030;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_PROGRAM = 0xa0a0;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_READ_RESET = 0xf0f0;

/* The tries each operation makes before it reports failure. */
static const int16_t INSTALLER_SCPI_DIAGNOSTIC_TRIES = 5;

/* A word of the flash at an address. */
static volatile uint16_t* installer_scpi_diagnostic_word(uint32_t address) {
  return (volatile uint16_t*)address;
}

/* The two unlock cycles to a pair. */
static void installer_scpi_diagnostic_unlock(uint32_t pair) {
  *installer_scpi_diagnostic_word(pair +
                                  INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST) =
      INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST_DATA;
  *installer_scpi_diagnostic_word(pair +
                                  INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND) =
      INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND_DATA;
}

/* A command to a pair: the unlock cycles, then the command at the first
 * unlock address. */
static void installer_scpi_diagnostic_command(uint32_t pair, uint16_t command) {
  installer_scpi_diagnostic_unlock(pair);
  *installer_scpi_diagnostic_word(
      pair + INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST) = command;
}

/* Programs a flash word, the word's low half, from 0x10000 to 0x7ffff,
 * polling its data each try up to 100 times for the word or both parts'
 * DQ5, the timeout, and trying up to 5 times; then puts the pair back
 * to reading.  STATUS_SCPI_DATA_OUT_OF_RANGE outside the range,
 * STATUS_SCPI_EXECUTION_ERROR once the fifth try is made, even if it
 * succeeds (docs/firmware/quirks.md) (installer::0x107cec). */
int32_t installer_flash_program_word(uint16_t* address, uint32_t word) {
  static const int16_t POLLS = 100;
  /* DQ5 of each part: the part ran out of time. */
  static const uint16_t TIMED_OUT = 0x2020;
  const uint16_t value = word;
  const uint32_t at = (uint32_t)address;
  volatile uint16_t* target = address;

  if (at < INSTALLER_SCPI_DIAGNOSTIC_FLASH_START ||
      at >= INSTALLER_SCPI_DIAGNOSTIC_FLASH_END) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  const uint32_t pair = at >= INSTALLER_SCPI_DIAGNOSTIC_PAIR_2
                            ? INSTALLER_SCPI_DIAGNOSTIC_PAIR_2
                            : 0;
  int16_t tries = 0;
  uint16_t read;

  do {
    installer_scpi_diagnostic_command(pair, INSTALLER_SCPI_DIAGNOSTIC_PROGRAM);
    *target = value;
    for (int16_t polls = 0; polls < POLLS; polls++) {
      read = *target;
      if (read == value || (read & TIMED_OUT) == TIMED_OUT) {
        break;
      }
    }
    tries++;
  } while (read != value && tries < INSTALLER_SCPI_DIAGNOSTIC_TRIES);
  installer_scpi_diagnostic_command(pair, INSTALLER_SCPI_DIAGNOSTIC_READ_RESET);
  if (tries == INSTALLER_SCPI_DIAGNOSTIC_TRIES) {
    return STATUS_SCPI_EXECUTION_ERROR;
  }
  return 0;
}

/* :DOWNload's record text's room in the image's frame, then its bytes':
 * 256 words, each a pair of hex digits' value; arrays' sizes, so
 * #define. */
#define INSTALLER_SCPI_DIAGNOSTIC_TEXT_SIZE 0x200

#define INSTALLER_SCPI_DIAGNOSTIC_BYTES_SIZE 0x100

/* A byte's two hex digits and a NUL, as strtol reads them. */
#define INSTALLER_SCPI_DIAGNOSTIC_PAIR_SIZE 3

/* :DOWNload "<S-record>": programs an S1, S2 or S3 record's data, a
 * word at a time, from its address, as does an S9, S8 or S7, with a
 * 16-, 24- or 32-bit address as theirs, for whatever data its count
 * gives; S0 and S5 do nothing, and another type, a bad checksum, a count
 * that is not the record's length or an address outside the writable
 * range is out of range (097-59551-02, 5-89 to 5-91;
 * installer::0x107d8c).  An odd last byte is programmed with 0xff
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

    if (address < INSTALLER_SCPI_DIAGNOSTIC_FLASH_START ||
        address >= INSTALLER_SCPI_DIAGNOSTIC_FLASH_END) {
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

/* :ERASe: erases the writable range, the first pair's sectors 2 to 7
 * and all eight of the second's, 32 KB each across a pair, with
 * interrupts masked while it commands them; then polls, up to a million
 * times, for both pairs' first words, 0x10000 and 0x40000, to show DQ7,
 * done, or DQ5, the timeout, in both parts, and tries again up to 5
 * times.  STATUS_SCPI_EXECUTION_ERROR once the fifth try is made, even
 * if it succeeds (docs/firmware/quirks.md) (installer::0x107f42). */
int32_t installer_scpi_set_DIAGnostic_ERASe(void* parameters, void* response,
                                            scpi_call* call) {
  static const uint32_t SECTOR_SIZE = 0x8000;
  static const int32_t FIRST_SECTOR = 2;
  static const int32_t SECTORS = 8;
  static const int32_t POLLS = 1000000;
  /* DQ7 or DQ5 of the high part, then of the low. */
  static const uint16_t HIGH_DONE = 0xa000;
  static const uint16_t LOW_DONE = 0x00a0;
  volatile uint16_t* first =
      installer_scpi_diagnostic_word(INSTALLER_SCPI_DIAGNOSTIC_FLASH_START);
  volatile uint16_t* second =
      installer_scpi_diagnostic_word(INSTALLER_SCPI_DIAGNOSTIC_PAIR_2);
  bool done = false;
  int32_t tries = 0;

  do {
    tries++;
    const uint16_t sr = installer_enter_supervisor_mode();

    installer_scpi_diagnostic_command(0, INSTALLER_SCPI_DIAGNOSTIC_ERASE_SETUP);
    installer_scpi_diagnostic_unlock(0);
    for (int32_t sector = FIRST_SECTOR; sector < SECTORS; sector++) {
      *installer_scpi_diagnostic_word(INSTALLER_SCPI_DIAGNOSTIC_FLASH_START +
                                      (sector - FIRST_SECTOR) * SECTOR_SIZE) =
          INSTALLER_SCPI_DIAGNOSTIC_SECTOR_ERASE;
    }
    installer_scpi_diagnostic_command(INSTALLER_SCPI_DIAGNOSTIC_PAIR_2,
                                      INSTALLER_SCPI_DIAGNOSTIC_ERASE_SETUP);
    installer_scpi_diagnostic_unlock(INSTALLER_SCPI_DIAGNOSTIC_PAIR_2);
    for (int32_t sector = 0; sector < SECTORS; sector++) {
      *installer_scpi_diagnostic_word(INSTALLER_SCPI_DIAGNOSTIC_PAIR_2 +
                                      sector * SECTOR_SIZE) =
          INSTALLER_SCPI_DIAGNOSTIC_SECTOR_ERASE;
    }
    installer_set_interrupt_mask(sr);
    for (int32_t polls = POLLS; !done && polls-- > 0;) {
      if ((*first & HIGH_DONE) != 0 && (*first & LOW_DONE) != 0 &&
          (*second & HIGH_DONE) != 0 && (*second & LOW_DONE) != 0) {
        done = true;
      }
    }
  } while (!done && tries < INSTALLER_SCPI_DIAGNOSTIC_TRIES);
  if (tries == INSTALLER_SCPI_DIAGNOSTIC_TRIES) {
    return STATUS_SCPI_EXECUTION_ERROR;
  }
  return 0;
}

/* :ERASe?: 1 when every word of the writable range reads 0xffff, else 0
 * (installer::0x108030).  The image first clears its parameters'
 * argument slot (installer::0x108030), which nothing reads. */
int32_t installer_scpi_query_DIAGnostic_ERASe(void* parameters, void* response,
                                              scpi_call* call) {
  static const uint16_t ERASED = 0xffff;
  int16_t* blank = response;
  uint32_t at = INSTALLER_SCPI_DIAGNOSTIC_FLASH_START;

  while (at < INSTALLER_SCPI_DIAGNOSTIC_FLASH_END &&
         *installer_scpi_diagnostic_word(at) == ERASED) {
    at += sizeof(uint16_t);
  }
  *blank = at == INSTALLER_SCPI_DIAGNOSTIC_FLASH_END;
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
 * lines, the other two 0 (installer::0x10815c). */
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
