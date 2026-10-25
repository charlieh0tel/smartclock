/* The QSPI's driver, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): its lock, its transfers
 * and its interrupt.  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  The lock is
 * docs/firmware/structures.md's "The QSPI's lock"; the registers are
 * mc68331.h's. */

/* The QSPI lock's ring of waiting tasks wraps at 16. */
static const uint8_t QSPI_WAITING_MASK = 0xf;

/* Hands the QSPI lock to the next task waiting, or frees it. */
static void qspi_hand_on(void) {
  if (qspi.write == qspi.read) {
    qspi.busy = 0;
    return;
  }
  if ((uint8_t)psos_signal_v(qspi.waiting[qspi.read], SIG_QSPI) != 0) {
    fatal_error("SPI isr signaling SPI owner");
  }
  qspi.read = (qspi.read + 1) & QSPI_WAITING_MASK;
}

/* Releases the QSPI lock to the next task waiting, at interrupt level
 * 4 (0x1f32a). */
void qspi_release(void) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_4);

  qspi_hand_on();
  cpu_sr_write(sr);
}

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

/* Sets a transfer up and starts it, or on a device it does not know
 * releases the lock; then waits while the interrupt has the transfer
 * running (0x1f61c). */
void qspi_start(qspi_device device, uint16_t* transmit, uint16_t count,
                char read) {
  uint16_t spcr1;

  qspi.running = 0;
  if (qspi_queue_setup(device, transmit, count, read, &spcr1) == 0) {
    qsm.spcr1 = spcr1;
  } else {
    qspi_release();
    if (read) {
      qspi.copy_pending = 0;
    }
  }
  /* qspi_interrupt clears it (0x1f664 reads it each time round). */
  volatile uint8_t* const running = &qspi.running;
  while (*running) {
  }
}

/* Takes the QSPI lock, or queues the task and waits until it is handed
 * on, at interrupt level 4 while it looks (0x1f672). */
void qspi_acquire(void) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_4);

  if (!qspi.busy) {
    qspi.busy = 1;
    cpu_sr_write(sr);
    return;
  }
  qspi.waiting[qspi.write] = psos_my_pid();
  qspi.write = (qspi.write + 1) & QSPI_WAITING_MASK;
  cpu_sr_write(sr);
  if ((uint8_t)psos_wait_v(SIG_QSPI, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_QSPI");
  }
}

/* The QSPI's end of queue: copies the receive RAM out if a read wants
 * it, hands the lock on, and clears SPIF, reading SPSR first as the
 * clear requires (MC68331UM 6.3.5.1) (0x1f770). */
void qspi_isr(void) {
  qspi.running = 0;
  if (qspi.copy_pending) {
    volatile uint16_t* from = qsm.rr;
    uint16_t* end = qspi.receive_buffer + qspi.receive_count;

    for (uint16_t* to = qspi.receive_buffer; to < end; to++) {
      *to = *from++;
    }
    qspi.copy_pending = 0;
  }
  qspi_hand_on();
  (void)qsm.spsr;
  qsm.spsr = 0;
}

/* Writes a QSPI device through the kernel, device 1 (0x1f8ca). */
void qspi_write(qspi_device device, void* buffers, uint32_t count) {
  psos_io_write(IO_DEVICE_QSPI | device, buffers, &count);
}

/* Writes count words to the FPGA's block through qspi_write; nothing
 * calls it (0x1f2c6). */
void qspi_fpga_block_write(uint16_t* words, uint16_t count) {
  qspi_write(QSPI_FPGA_BLOCK, words, count);
}

/* Writes two words, its argument's, to the FPGA's tail through
 * qspi_write, the argument's own slot the buffer; nothing calls it
 * (0x1f2e2). */
void qspi_fpga_tail_write(uint32_t words) {
  qspi_write(QSPI_FPGA_TAIL, &words, FPGA_TAIL_WORDS);
}

/* Writes a word to the FPGA through qspi_write, the argument's own slot
 * the buffer; nothing calls it (0x1f2fa). */
void qspi_fpga_word_write(uint16_t word) {
  qspi_write(QSPI_FPGA_WORD, &word, 1);
}

/* Makes one transfer to the FPGA's byte device through qspi_write, the
 * argument's own slot the buffer; nothing calls it (0x1f312). */
void qspi_fpga_byte_write(uint16_t value) {
  qspi_write(QSPI_FPGA_BYTE, &value, 1);
}

/* Reads a QSPI device through the kernel, device 1 (0x1f8ea). */
void qspi_read(qspi_device device, void* buffers, uint32_t count) {
  psos_io_read(IO_DEVICE_QSPI | device, buffers, &count);
}

/* Sends to a QSPI device by polling, in supervisor mode with interrupts
 * masked, without the receive RAM's copy; finishes a transfer still
 * pending first (0x1f90a). */
void qspi_send(qspi_device device, uint16_t* transmit, uint16_t count) {
  uint16_t sr = cpu_enter_supervisor();
  uint16_t spcr1;

  cpu_interrupts_raise(CPU_IPL_7);
  qspi.field_00 = 1;
  while ((qsm.spcr1 & SPCR1_SPE) != 0) {
  }
  if ((qsm.spsr & SPSR_SPIF) != 0) {
    qspi_isr();
  }
  if (qspi_queue_setup(device, transmit, count, 0, &spcr1) == 0) {
    qsm.spcr1 = spcr1;
    while ((qsm.spsr & SPSR_SPIF) == 0) {
    }
    (void)qsm.spsr;
    qsm.spsr = 0;
  }
  cpu_sr_write(sr);
}

/* Transfers to and from a QSPI device by polling, in supervisor mode
 * with interrupts masked, copying the receive RAM into the buffers'
 * receive, a word for the FPGA's readback and count words for any
 * other; finishes a transfer still pending first (0x1f988). */
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
