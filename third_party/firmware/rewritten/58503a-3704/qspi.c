/* qspi.c's functions that are this image's own: the rest are
 * common/qspi.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The queue's longest transfer: its 16 entries of transmit and command
 * RAM (MC68331UM 6.3.4). */
static const uint16_t QSPI_QUEUE_ENTRIES = 16;

/* Loads a transfer of words into the queue's first entries, each sent
 * to the device's chip selects and held selected across all but the
 * last (MC68331UM D.4.16); a NULL transmit sends zeros. */
static void qspi_queue_words(const uint16_t* transmit, uint8_t words,
                             uint8_t chip_select) {
  for (uint8_t entry = 0; entry < words; entry++) {
    qsm.tr[entry] = transmit == NULL ? 0 : transmit[entry];
  }
  for (uint8_t entry = 0; entry < words; entry++) {
    qsm.cr[entry] =
        QSPI_CR_BITSE | chip_select | (entry + 1 < words ? QSPI_CR_CONT : 0);
  }
}

/* SPCR0 for a master transferring words of so many bits, 16 written as
 * 0, in a clock mode (CPOL and CPHA) at a baud divider (D.4.10). */
static uint16_t qspi_spcr0(uint8_t bits, uint16_t clock_mode, uint8_t divider) {
  return SPCR0_MSTR | ((bits << SPCR0_BITS_SHIFT) & SPCR0_BITS_MASK) |
         clock_mode | divider;
}

/* Ends the queue at its last entry, with the end-of-queue interrupt,
 * and gives the caller SPCR1 to start it: enabled, with the shortest
 * delays before the clock and after the transfer (D.4.11, D.4.12). */
static void qspi_queue_end(uint16_t last, uint16_t* spcr1) {
  static const uint16_t START_DELAYS = 1 << SPCR1_DSCKL_SHIFT | 1;

  qsm.spcr2 = SPCR2_SPIFIE | last << SPCR2_ENDQP_SHIFT;
  *spcr1 = SPCR1_SPE | START_DELAYS;
}

/* Sets the QSM's queue up for a transfer to or from a device, SPCR1's
 * start left to the caller; 1 for a device it does not know or an
 * empty FPGA block (0x1f396).  A write to the hex DAC, the EFC DAC or
 * QSPI_UNUSED_TWO_WORDS also marks the transfer running for
 * qspi_start.  Devices 0 to 7 select their own number on PCS3-PCS0,
 * the FPGA's all FPGA_CHIP_SELECT (docs/firmware/structures.md, "The
 * QSPI's devices").  Device 0 is the front panel, one 8-bit word,
 * where the Z3805A's sends three of 12 bits. */
uint8_t qspi_queue_setup(qspi_device device, uint16_t* transmit, uint16_t count,
                         uint8_t read, uint16_t* spcr1) {
  /* The FPGA's chip selects, and its clock divider for writes and for
   * reads. */
  static const uint8_t FPGA_CHIP_SELECT = 6;
  static const uint8_t FPGA_WRITE_DIVIDER = 2;
  static const uint8_t FPGA_READ_DIVIDER = 9;
  /* The readback's fewest bits a transfer. */
  static const uint16_t READBACK_FEWEST_BITS = 8;
  /* The clock's SPI modes: 2 idles high, 3 also takes data on the
   * second edge (D.4.10). */
  const uint16_t spi_mode_2 = SPCR0_CPOL;
  const uint16_t spi_mode_3 = SPCR0_CPOL | SPCR0_CPHA;
  const uint8_t fpga_divider = read ? FPGA_READ_DIVIDER : FPGA_WRITE_DIVIDER;

  switch (device) {
    case QSPI_FRONT_PANEL:
      qspi_queue_words(transmit, 1, QSPI_FRONT_PANEL);
      qsm.spcr0 = qspi_spcr0(8, spi_mode_3, 2);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_ADC:
      qspi_queue_words(transmit, 1, QSPI_ADC);
      qsm.spcr0 = qspi_spcr0(16, spi_mode_3, 0x18);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_HDAC:
      qspi_queue_words(transmit, 3, QSPI_HDAC);
      qsm.spcr0 = qspi_spcr0(12, read ? spi_mode_3 : spi_mode_2, 0x46);
      if (!read) {
        qspi.running = 1;
      }
      qspi_queue_end(2, spcr1);
      return 0;
    case QSPI_UNUSED_WORD:
      /* The queue ends at entry 1, which this does not load: it sends
       * whatever the last transfer left there (0x1f472). */
      qspi_queue_words(transmit, 1, QSPI_UNUSED_WORD);
      qsm.spcr0 = qspi_spcr0(16, read ? spi_mode_2 : spi_mode_3, 8);
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_UNUSED_TWO_WORDS:
      qspi_queue_words(transmit, 2, QSPI_UNUSED_TWO_WORDS);
      qsm.spcr0 = qspi_spcr0(16, spi_mode_2, read ? 5 : 2);
      if (!read) {
        qspi.running = 1;
      }
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_FPGA_BLOCK: {
      if (count == 0) {
        return 1;
      }
      if (count > QSPI_QUEUE_ENTRIES) {
        count = QSPI_QUEUE_ENTRIES;
      }
      /* Each word's transmit and command entry in turn, the last
       * command then rewritten to release the chip select. */
      for (uint16_t entry = 0; entry < count; entry++) {
        qsm.tr[entry] = transmit[entry];
        qsm.cr[entry] = QSPI_CR_CONT | QSPI_CR_BITSE | FPGA_CHIP_SELECT;
      }
      qsm.cr[count - 1] = QSPI_CR_BITSE | FPGA_CHIP_SELECT;
      qsm.spcr0 = qspi_spcr0(16, spi_mode_2, fpga_divider);
      qspi_queue_end(count - 1, spcr1);
      return 0;
    }
    case QSPI_EFC:
      qspi_queue_words(transmit, 1, QSPI_EFC);
      qsm.spcr0 = qspi_spcr0(16, read ? spi_mode_2 : spi_mode_3, 8);
      if (!read) {
        qspi.running = 1;
      }
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_FPGA_TAIL:
      qspi_queue_words(transmit, 2, FPGA_CHIP_SELECT);
      qsm.spcr0 = qspi_spcr0(16, spi_mode_2, fpga_divider);
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_FPGA_WORD:
      qspi_queue_words(transmit, 1, FPGA_CHIP_SELECT);
      qsm.spcr0 = qspi_spcr0(16, spi_mode_2, fpga_divider);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_FPGA_BYTE:
      qspi_queue_words(transmit, 1, FPGA_CHIP_SELECT);
      qsm.spcr0 = qspi_spcr0(8, spi_mode_2, fpga_divider);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_FPGA_READBACK:
      /* count is the bits to read back, 8 to 16; more read 16, the
       * image taking them as 0, which SPCR0 reads as 16. */
      if (count > QSPI_QUEUE_ENTRIES) {
        count = QSPI_QUEUE_ENTRIES;
      } else if (count < READBACK_FEWEST_BITS) {
        count = READBACK_FEWEST_BITS;
      }
      qspi_queue_words(transmit, 1, FPGA_CHIP_SELECT);
      qsm.spcr0 = qspi_spcr0(count, spi_mode_2, FPGA_READ_DIVIDER);
      qspi_queue_end(0, spcr1);
      return 0;
    default:
      return 1;
  }
}
