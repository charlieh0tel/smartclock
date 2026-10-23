/* The boot code's routines (reset at 0x550, its own program ahead of the
 * primary), written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `platform/boot`): its DUART self-test and its unpacker of the
 * installer's records, with the primary's copy of that unpacker.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Sends "DUART" through channel B in local loopback, each character
 * read back against the text; a wrong one sets SELFTEST_UART_CHANNEL_B_DATA,
 * none within the spins SELFTEST_UART_CHANNEL_B_TIMEOUT (0x400). */
void boot_duartb_loopback_check(void) {
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  boot_loopback_send_next = (uint8_t*)text;
  boot_loopback_expect_next = text;
  duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  duart.b.rb_tb = *boot_loopback_send_next++;
  while (*boot_loopback_expect_next != 0 && boot_selftest.uart == 0) {
    for (int32_t spins = DUART_LOOPBACK_CHECK_SPINS;
         spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--) {
    }
    if ((duart.b.sr_csr & SR_RXRDY) != 0) {
      char expected = *boot_loopback_expect_next++;

      if (duart.b.rb_tb != (uint8_t)expected) {
        boot_selftest.uart |= SELFTEST_UART_CHANNEL_B_DATA;
      }
      if (*boot_loopback_send_next != 0) {
        duart.b.rb_tb = *boot_loopback_send_next++;
      }
    } else {
      boot_selftest.uart |= SELFTEST_UART_CHANNEL_B_TIMEOUT;
    }
  }
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  (void)duart.b.mr;
  duart.b.mr = DUART_MR2_ONE_STOP_BIT;
}

/* Tests the DUART's interrupt vector register with two patterns and sets
 * its vector, then, if it held them, sets channel B up for 9600 8N1 and
 * checks it in loopback (0x4cc). */
void boot_duart_early_init(void) {
  boot_selftest.uart = 0;
  duart.ivr = DUART_IVR_PATTERN_1;
  if (duart.ivr != DUART_IVR_PATTERN_1) {
    boot_selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_PATTERN_2;
  if (duart.ivr != DUART_IVR_PATTERN_2) {
    boot_selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_VECTOR;
  if (boot_selftest.uart == 0) {
    duart.b.cr = CR_RESET_RECEIVER;
    duart.b.cr = CR_RESET_TRANSMITTER;
    duart.b.cr = CR_RESET_MR_POINTER;
    duart.b.mr = MR1_PARITY_NONE | DUART_MR1_EIGHT_BITS;
    duart.b.mr = DUART_MR2_ONE_STOP_BIT;
    duart.b.sr_csr = DUART_CSR_9600 << DUART_CSR_CLOCK_BITS | DUART_CSR_9600;
    duart.ipcr_acr = ACR_BRG_SET_2 | DUART_ACR_COUNTER_CRYSTAL_16;
    boot_duartb_loopback_check();
  }
}

/* Copies so many bytes; returns the destination (0xb9e). */
uint8_t* boot_memcpy(uint8_t* to, uint8_t* from, int32_t count) {
  uint8_t* cursor = to;

  while (count-- != 0) {
    *cursor++ = *from++;
  }
  return to;
}

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

/* Copies the installer's records, installer_records' 0xa76d bytes at
 * 0xbbc, into RAM (0xaa0). */
uint32_t boot_unpack_installer(void) {
  static const int32_t RECORDS_SIZE = 0xa76d;

  return boot_unpack(installer_records, RECORDS_SIZE, false);
}

/* The primary's copy of boot_unpack_installer, with memcpy, pointed at
 * 11 bytes at address 0, boot_vectors, whose first byte, the initial
 * stack pointer's high one, is no S: it always returns
 * BOOT_UNPACK_NO_START (0x51158). */
uint32_t primary_unpack_records(void) {
  static const int32_t RECORDS_SIZE = 0xb;

  return boot_unpack((const uint8_t*)&boot_vectors, RECORDS_SIZE, true);
}
