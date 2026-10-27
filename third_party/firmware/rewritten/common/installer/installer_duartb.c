/* The functions of installer/installer_duartb.c the images share, the same in
 * each (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/installer/installer_duartb.c describes the module and holds the
 * rest.  Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* DUART channel B's baud rate from the serial settings, 19200 for a
 * code out of range. */
void installer_duartb_baud_from_settings(void) {
  uint8_t clock;

  switch (installer_duartb_settings.baud) {
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
  installer_duart_shadow.csrb = clock;
  duart.b.sr_csr = clock;
  installer_duartb_applied.baud = installer_duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on. */
uint8_t installer_duartb_putc_polled(uint8_t c) {
  return duart_b_putc_polled(c);
}

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length. */
int32_t installer_console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = installer_enter_supervisor();

  for (; *s != 0; s++, count++) {
    installer_duartb_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* The done hook of channel B's setting descriptors: reconfigures device
 * 0. */
void installer_duartb_settings_done(void) {
  installer_psos_io_control(IO_DEVICE_DUART_B, 0, 0);
}
