/* DUART channel B as the serial settings set it, the same in each image
 * that includes this file (docs/firmware/rewrite.md, "Shared C", "A file
 * for some images"): its baud rate from the settings and its polled
 * write.  An image whose port of that name is the SCI
 * leaves the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* DUART channel B's baud rate from the serial settings, 19200 for a
 * code out of range. */
void duartb_baud_from_settings(void) {
  uint8_t clock;

  switch (duartb_settings.baud) {
    case SERIAL_BAUD_1200:
      clock = DUART_CSR_1200;
      break;
    case SERIAL_BAUD_2400:
      clock = DUART_CSR_2400;
      break;
    case SERIAL_BAUD_9600:
      clock = DUART_CSR_9600;
      break;
    default:
      clock = DUART_CSR_19200;
      break;
  }
  clock |= clock << DUART_CSR_CLOCK_BITS;
  duart_shadow.csrb = clock;
  duart.b.sr_csr = clock;
  duartb_applied.baud = duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on. */
uint8_t duartb_putc_polled(uint8_t c) { return duart_b_putc_polled(c); }
