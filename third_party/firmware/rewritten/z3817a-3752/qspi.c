/* qspi.c's functions that are this image's own: the rest are
 * common/qspi.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  The queue's set-up, read beside the Z3815A 4010's; the
 * polled transfer, which masks interrupts in the status register as the
 * decompiler cannot, the other images' but for this image's readback
 * minor; and the FPGA's writes, the other images' on this image's minors
 * (docs/firmware/z3817a-3752.md, "The function check").  Its comments'
 * addresses are the Z3817A's.  Each keeps its generated prototype. */

/* The queue's longest transfer: its 16 entries of transmit and command
 * RAM (MC68331UM 6.3.4). */
static const uint16_t QSPI_QUEUE_ENTRIES = 16;

/* Sets the queue's command entry: sent with SPCR0's bits to a chip
 * select pattern on PCS3-PCS0, held selected after it for all but the
 * last (MC68331UM D.4.16). */
static void qspi_command_set(uint8_t entry, uint8_t chip_select, bool last) {
  qsm.cr[entry] = QSPI_CR_BITSE | chip_select | (last ? 0 : QSPI_CR_CONT);
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

/* Sets the QSM's queue up for a transfer to or from a device, by its
 * minor (qspi_device), SPCR1's start left to the caller; 1 for a minor it
 * does not set up or an empty FPGA block (0x2f6d8).  Each word is 16 bits
 * but the ADC's first, the FPGA byte's and the readback's; a write to the
 * EFC DAC or of
 * QSPI_TWO_WORDS also marks the transfer running for qspi_start.  The
 * Z3815A 4010's takes other minors from 0, each with its own clock, and
 * marks other writes running. */
uint8_t qspi_queue_setup(qspi_device device, uint16_t* transmit, uint16_t count,
                         uint8_t read, uint16_t* spcr1) {
  /* The chip select patterns on PCS3-PCS0: the FPGA's, the ADC's first
   * word's, and else the minor's low nibble. */
  static const uint8_t FPGA_CHIP_SELECT = 0xc;
  static const uint8_t ADC_FIRST_CHIP_SELECT = 0xe;
  static const uint8_t MINOR_CHIP_SELECT = 0xf;
  /* The clock dividers: the ADC's, the EFC DAC's, and else the write's,
   * QSPI_TWO_WORDS' read's and the FPGA's read's. */
  static const uint8_t ADC_DIVIDER = 0x18;
  static const uint8_t EFC_DIVIDER = 4;
  static const uint8_t WRITE_DIVIDER = 2;
  static const uint8_t TWO_WORDS_READ_DIVIDER = 5;
  static const uint8_t FPGA_READ_DIVIDER = 9;
  /* The readback's fewest bits a transfer. */
  static const uint16_t READBACK_FEWEST_BITS = 8;
  static const uint8_t WORD_BITS = 16;
  static const uint8_t BYTE_BITS = 8;
  /* The clock's SPI modes: 0 idles low, 2 high, 3 also takes data on the
   * second edge (D.4.10). */
  const uint16_t spi_mode_0 = 0;
  const uint16_t spi_mode_2 = SPCR0_CPOL;
  const uint16_t spi_mode_3 = SPCR0_CPOL | SPCR0_CPHA;
  const uint8_t chip_select = device & MINOR_CHIP_SELECT;
  const uint8_t fpga_divider = read ? FPGA_READ_DIVIDER : WRITE_DIVIDER;

  switch (device) {
    case QSPI_EFC:
      qsm.tr[0] = transmit[0];
      qspi_command_set(0, chip_select, true);
      if (read) {
        qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_3, EFC_DIVIDER);
      } else {
        qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, EFC_DIVIDER);
        qspi.running = 1;
      }
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_ADC:
      qsm.tr[0] = transmit[0];
      qsm.tr[1] = transmit[1];
      /* the first entry without BITSE, 8 bits (0x2f74e) */
      qsm.cr[0] = QSPI_CR_CONT | ADC_FIRST_CHIP_SELECT;
      qspi_command_set(1, chip_select, true);
      qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_0, ADC_DIVIDER);
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_FPGA_BLOCK:
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
        qspi_command_set(entry, FPGA_CHIP_SELECT, false);
      }
      qspi_command_set(count - 1, FPGA_CHIP_SELECT, true);
      qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, fpga_divider);
      qspi_queue_end(count - 1, spcr1);
      return 0;
    case QSPI_TWO_WORDS:
      qsm.tr[0] = transmit[0];
      qsm.tr[1] = transmit[1];
      qspi_command_set(0, chip_select, false);
      qspi_command_set(1, chip_select, true);
      if (read) {
        qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, TWO_WORDS_READ_DIVIDER);
      } else {
        qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, WRITE_DIVIDER);
        qspi.running = 1;
      }
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_FPGA_TAIL:
      qsm.tr[0] = transmit[0];
      qsm.tr[1] = transmit[1];
      qspi_command_set(0, FPGA_CHIP_SELECT, false);
      qspi_command_set(1, FPGA_CHIP_SELECT, true);
      qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, fpga_divider);
      qspi_queue_end(1, spcr1);
      return 0;
    case QSPI_FPGA_WORD:
      qsm.tr[0] = transmit[0];
      qspi_command_set(0, FPGA_CHIP_SELECT, true);
      qsm.spcr0 = qspi_spcr0(WORD_BITS, spi_mode_2, fpga_divider);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_FPGA_BYTE:
      qsm.tr[0] = transmit[0];
      qspi_command_set(0, FPGA_CHIP_SELECT, true);
      qsm.spcr0 = qspi_spcr0(BYTE_BITS, spi_mode_2, fpga_divider);
      qspi_queue_end(0, spcr1);
      return 0;
    case QSPI_FPGA_READBACK:
      /* count is the bits to read back, 8 to 16, which SPCR0 reads as
       * 16 at 0. */
      if (count > QSPI_QUEUE_ENTRIES) {
        count = QSPI_QUEUE_ENTRIES;
      } else if (count < READBACK_FEWEST_BITS) {
        count = READBACK_FEWEST_BITS;
      }
      qsm.tr[0] = transmit[0];
      qspi_command_set(0, FPGA_CHIP_SELECT, true);
      qsm.spcr0 = qspi_spcr0(count, spi_mode_2, FPGA_READ_DIVIDER);
      qspi_queue_end(0, spcr1);
      return 0;
    default:
      return 1;
  }
}

/* Transfers to and from a QSPI device by polling, in supervisor mode
 * with interrupts masked, copying the receive RAM into the buffers'
 * receive, a word for the FPGA's readback and count words for any
 * other; finishes a transfer still pending first (0x2fc56). */
void qspi_transfer(qspi_device device, qspi_buffers* buffers, uint16_t count) {
  volatile uint16_t* from = qsm.rr;
  uint16_t* to = buffers->receive;
  uint16_t sr = cpu_enter_supervisor();
  uint16_t spcr1;

  cpu_interrupts_raise(CPU_IPL_7);
  qspi.field_00 = 1;
  while ((qsm.spcr1 & SPCR1_SPE) != 0) {
  }
  if ((qsm.spsr & SPSR_SPIF) != 0) {
    qspi_isr();
  }
  if (qspi_queue_setup(device, buffers->transmit, count, 1, &spcr1) == 0) {
    uint16_t* end = to + (device == QSPI_FPGA_READBACK ? 1 : count);

    qsm.spcr1 = spcr1;
    while ((qsm.spsr & SPSR_SPIF) == 0) {
    }
    while (to < end) {
      *to++ = *from++;
    }
    (void)qsm.spsr;
    qsm.spsr = 0;
  }
  cpu_sr_write(sr);
}

/* The FPGA's QSPI writes, the other images' on this image's minors
 * (qspi_device): QSPI_FPGA_BLOCK, QSPI_FPGA_TAIL, QSPI_FPGA_WORD and
 * QSPI_FPGA_BYTE, none at the other images' numbers. */

/* Writes count words to the FPGA's block through qspi_write; nothing
 * calls it (0x2e69a). */
void qspi_fpga_block_write(uint16_t* words, uint16_t count) {
  qspi_write(QSPI_FPGA_BLOCK, words, count);
}

/* Writes two words, its argument's, to the FPGA's tail through
 * qspi_write, the argument's own slot the buffer; nothing calls it
 * (0x2e6b6). */
void qspi_fpga_tail_write(uint32_t words) {
  qspi_write(QSPI_FPGA_TAIL, &words, FPGA_TAIL_WORDS);
}

/* Writes a word to the FPGA through qspi_write, the argument's own slot
 * the buffer; nothing calls it (0x2e6ce). */
void qspi_fpga_word_write(uint16_t word) {
  qspi_write(QSPI_FPGA_WORD, &word, 1);
}

/* Makes one transfer to the FPGA's byte device through qspi_write, the
 * argument's own slot the buffer; nothing calls it (0x2e6e6). */
void qspi_fpga_byte_write(uint16_t value) {
  qspi_write(QSPI_FPGA_BYTE, &value, 1);
}
