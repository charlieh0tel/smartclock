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
 * 4 (0x2050c). */
void qspi_release(void) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_4);

  qspi_hand_on();
  cpu_sr_write(sr);
}

/* Sets a transfer up and starts it, or on a device it does not know
 * releases the lock; then waits while the interrupt has the transfer
 * running (0x2081e). */
void qspi_start(qspi_device device, uint16_t* transmit, uint16_t count,
                char read) {
  uint16_t spcr1;

  qspi.running = 0;
  /* qspi_queue_setup's low byte is nonzero for a device it does not know */
  if ((uint8_t)qspi_queue_setup(device, transmit, count, read, &spcr1) == 0) {
    qsm.spcr1 = spcr1;
  } else {
    qspi_release();
    if (read) {
      qspi.copy_pending = 0;
    }
  }
  while (qspi.running) {
  }
}

/* Takes the QSPI lock, or queues the task and waits until it is handed
 * on, at interrupt level 4 while it looks (0x20874). */
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
 * clear requires (MC68331UM 6.3.5.1) (0x20972). */
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

/* Writes a QSPI device through the kernel, device 1 (0x20acc). */
void qspi_write(qspi_device device, void* buffers, uint32_t count) {
  psos_io_write(IO_DEVICE_QSPI | device, buffers, &count);
}

/* Reads a QSPI device through the kernel, device 1 (0x20aec). */
void qspi_read(qspi_device device, void* buffers, uint32_t count) {
  psos_io_read(IO_DEVICE_QSPI | device, buffers, &count);
}

/* Sends to a QSPI device by polling, in supervisor mode with interrupts
 * masked, without the receive RAM's copy; finishes a transfer still
 * pending first (0x20b0c). */
void qspi_send(qspi_device device, uint16_t* transmit, uint16_t count) {
  uint16_t sr = enter_supervisor();
  uint16_t spcr1;

  cpu_interrupts_raise(CPU_IPL_7);
  qspi.field_00 = 1;
  while ((qsm.spcr1 & SPCR1_SPE) != 0) {
  }
  if ((qsm.spsr & SPSR_SPIF) != 0) {
    qspi_isr();
  }
  if ((uint8_t)qspi_queue_setup(device, transmit, count, 0, &spcr1) == 0) {
    qsm.spcr1 = spcr1;
    while ((qsm.spsr & SPSR_SPIF) == 0) {
    }
    (void)qsm.spsr;
    qsm.spsr = 0;
  }
  cpu_sr_write(sr);
}
