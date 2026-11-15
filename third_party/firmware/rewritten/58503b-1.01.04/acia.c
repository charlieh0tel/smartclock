/* The 58503B 1.01.04's own driver of the ACIA at 0x200000, the Oncore
 * engine's link, where the other images drive DUART channel A
 * (docs/firmware/58503b-1.01.04.md, "The SCI and the ACIA"): its two
 * registers, acia_status_control and acia_data, and the shadow of the
 * control register, acia_control.  The routines the matches carry under
 * DUART names, duart_test, duart_stop and the two primary_reset calls,
 * are here too, each acting on the ACIA or on nothing.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

/* The control register's value the routines that stop the ACIA write,
 * and acia_init writes first (0x2d268): its two low bits set, the
 * MC6850's master reset, inferred; third_party/ holds no data sheet. */
static const uint8_t ACIA_CONTROL_RESET = 0x03;

/* The control register's transmit interrupt bit: acia_send_message sets
 * it in acia_control and writes it to start a message (0x2d340),
 * acia_tx_interrupt clears it at the message's end (0x2d514), and the
 * interrupt's entry sends the next byte while it and the status'
 * transmitter bit are set (0x2d56a). */
static const uint8_t ACIA_CONTROL_TRANSMIT_INTERRUPT = 0x20;

/* The status' bits acia_isr gathers into acia.receive_errors, which
 * acia_get_byte names in its messages (0x2d3c6 to 0x2d3f2): the framing
 * error, the overrun, which acia_isr also marks in acia.overrun, and the
 * parity error. */
static const uint8_t ACIA_STATUS_FRAMING_ERROR = 0x10;
static const uint8_t ACIA_STATUS_OVERRUN = 0x20;
static const uint8_t ACIA_STATUS_PARITY_ERROR = 0x40;
static const uint8_t ACIA_STATUS_ERRORS =
    ACIA_STATUS_FRAMING_ERROR | ACIA_STATUS_OVERRUN | ACIA_STATUS_PARITY_ERROR;

/* Sends a message to the GPS engine on the ACIA by interrupt, waiting
 * until it is out; only the GPS task may, and another gets 1 back.  The
 * other images' drta_send_message, on the ACIA's control register
 * (0x2d2f4). */
uint8_t acia_send_message(uint8_t* message, int16_t length) {
  if (psos_my_pid() != task_gpsm) {
    return 1;
  }
  psos_get_v(SIG_SOQA, 0, 0);
  acia.transmit_next = message;
  acia.transmit_count = length;
  const uint16_t sr = set_interrupt_mask(CPU_IPL_6);

  if (!acia.held) {
    acia_control |= ACIA_CONTROL_TRANSMIT_INTERRUPT;
    acia_status_control = acia_control;
  }
  set_interrupt_mask(sr);
  if ((uint8_t)psos_wait_v(SIG_SOQA, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQA error in acia_send_message");
  }
  return 0;
}

/* Whether the GPS engine's ring holds a byte (0x2d38c); the other
 * images' duarta_rx_pending, of DUART channel A's ring. */
bool acia_rx_pending(void) { return acia_ring.write != acia_ring.read; }

/* Reads a byte from the GPS engine's ring, waiting for one; hands back
 * through overrun whether the receiver overran since the last, and
 * reports each kind of receive error gathered since, where the other
 * images' drta_get_byte reports an overrun alone (0x2d3a2). */
uint8_t acia_get_byte(char* overrun) {
  if (overrun != 0) {
    *overrun = acia.overrun;
  }
  if (acia.receive_errors != 0) {
    if ((acia.receive_errors & ACIA_STATUS_OVERRUN) != 0) {
      task_debug_message("ACIA receiver overrun occurred\r\n");
    }
    if ((acia.receive_errors & ACIA_STATUS_FRAMING_ERROR) != 0) {
      task_debug_message("ACIA receiver framing error\r\n");
    }
    if ((acia.receive_errors & ACIA_STATUS_PARITY_ERROR) != 0) {
      task_debug_message("ACIA receiver parity error\r\n");
    }
    acia.receive_errors = 0;
  }
  acia.overrun = 0;
  psos_get_v(SIG_SIQ, 0, 0);
  if (acia_ring.write == acia_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ error in acia_get_byte");
  }
  return acia_ring.data[acia_ring.read++];
}

/* The DUART self-test's place in the power-on tests (selftest_uart,
 * 0x28fb6): clears selftest.uart and tests nothing, with no DUART to
 * test (0x2d466). */
void duart_test(void) { selftest.uart = 0; }

/* The ACIA's receive interrupt, which its interrupt's entry calls with
 * the status it read (0x2d55c): gathers the status' errors, marks an
 * overrun, takes the byte into the ring, signaling the GPS task, and
 * notes the ring's least room (0x2d46e).  The other images' duarta_isr
 * reads its status itself and takes every byte waiting. */
void acia_isr(uint8_t status) {
  if ((status & ACIA_STATUS_OVERRUN) != 0) {
    acia.overrun = 1;
  }
  acia.receive_errors |= status & ACIA_STATUS_ERRORS;
  acia_ring.data[acia_ring.write++] = acia_data;
  if (task_gpsm != 0 && (uint8_t)psos_signal_v(task_gpsm, SIG_SIQ) != 0) {
    fatal_error("ACIA isr signaling SIG_SIQ");
  }
  const uint8_t free_bytes = acia_ring.read - acia_ring.write;

  if (free_bytes < acia.least_free) {
    acia.least_free = free_bytes;
  }
}

/* The ACIA's transmit interrupt: sends the message's next byte, or at
 * its end turns the interrupt off and signals the GPS task; the other
 * images' duarta_tx_interrupt on the ACIA's control register
 * (0x2d4f4). */
void acia_tx_interrupt(void) {
  if (acia.transmit_count != 0) {
    acia_data = *acia.transmit_next++;
    acia.transmit_count--;
  } else {
    acia_control &= ~ACIA_CONTROL_TRANSMIT_INTERRUPT;
    acia_status_control = acia_control;
    psos_signal_v(task_gpsm, SIG_SOQA);
  }
}

/* Resets the ACIA: shutdown's last call (0x2227a), under the name its
 * matches carry, where the other images stop the DUART's channels and
 * mask its interrupts (0x2d582). */
void duart_stop(void) { acia_status_control = ACIA_CONTROL_RESET; }

/* The power-on tests' DUART test, primary_reset's call after acia_reset
 * (0x249d2), where the other images' tests the DUART's interrupt vector
 * register and sets channel B up: clears selftest.uart alone
 * (0x2d58c). */
void duart_early_init(void) { selftest.uart = 0; }

/* Resets the ACIA, as duart_stop does: primary_reset's call before
 * duart_early_init (0x249cc) (0x2d594). */
void acia_reset(void) { acia_status_control = ACIA_CONTROL_RESET; }

/* The ACIA's registers as acia_init reaches them, each access kept: it
 * writes the control register three times running and reads the status
 * and the data in a loop. */
static volatile uint8_t* const ACIA_CONTROL = &acia_status_control;
static volatile uint8_t* const ACIA_STATUS = &acia_status_control;
static volatile uint8_t* const ACIA_RECEIVE = &acia_data;

/* The control register's values acia_init writes after the reset, as an
 * MC6850's, inferred as ACIA_CONTROL_RESET is: divide by 16 with eight
 * bits, no parity and one stop bit, then the receive interrupt enabled;
 * and its status's receive data full. */
static const uint8_t ACIA_DIVIDE_16_8N1 = 0x15;
static const uint8_t ACIA_RECEIVE_INTERRUPT = 0x80;
static const uint8_t ACIA_RECEIVE_FULL = 0x01;

/* Sets the ACIA up for the GPS engine and its driver state afresh:
 * resets the part, sets its format, keeping what it writes in
 * acia_control, its shadow, waits 100000 counts, empties its receiver,
 * then clears the driver's state and ring (0x2d268); primary_start's
 * (0x22f2a).  The image keeps every register, D0, D1, A0 and A1 too, as
 * CPU_KEEPING_SCRATCH's wrapper does. */
static __attribute__((used, noipa)) void acia_init_kept(void) {
  static const int32_t SETTLE_COUNTS = 100000;
  /* acia's least free count while no message has been sent. */
  static const uint8_t ACIA_LEAST_FREE_NONE = 0xff;

  *ACIA_CONTROL = ACIA_CONTROL_RESET;
  acia_control = ACIA_DIVIDE_16_8N1;
  *ACIA_CONTROL = acia_control;
  acia_control |= ACIA_RECEIVE_INTERRUPT;
  *ACIA_CONTROL = acia_control;
  for (int32_t count = 0; count < SETTLE_COUNTS; count++) {
    /* a delay, which the empty asm keeps from being optimized away */
    __asm__ volatile("");
  }
  while ((*ACIA_STATUS & ACIA_RECEIVE_FULL) != 0) {
    (void)*ACIA_RECEIVE;
  }
  acia.transmit_next = (uint8_t*)loopback_idle_text;
  acia.transmit_count = 0;
  acia_ring.write = 0;
  acia_ring.read = 0;
  acia.overrun = 0;
  acia.least_free = ACIA_LEAST_FREE_NONE;
  acia.held = 0;
  acia.receive_errors = 0;
}

CPU_KEEPING_SCRATCH(acia_init, acia_init_kept);
