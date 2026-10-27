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
