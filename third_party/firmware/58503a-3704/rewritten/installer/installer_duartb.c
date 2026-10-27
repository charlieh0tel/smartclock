/* The installer's copies of DUART channel B's driver (`devices/duartb`;
 * duartb.c has the primary's), which keep the installer's driver records and
 * call its routines. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Writes a string to DUART channel B by interrupt, waiting until it is
 * out; returns how much went (installer::0x10313e). */
int32_t installer_drtb_puts(char* text) {
  char* end = text;
  uint16_t sr;

  installer_psos_get_v(SIG_SOQ, 0, 0);
  installer_duartb.transmit_next = text;
  while (*end != 0) {
    end++;
  }
  installer_duartb.transmit_count = end - text;
  sr = cpu_interrupts_raise(CPU_IPL_6);
  if (!installer_duartb.reconfiguring) {
    installer_duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = installer_duart_shadow.imr;
  }
  cpu_sr_write(sr);
  if ((uint8_t)installer_psos_wait_v(SIG_SOQ, 0, 0, 0) != 0) {
    installer_fatal_error("wait_v SIG_SOQ error in _drtb_puts");
  }
  return installer_duartb.transmit_next - text;
}

/* Reads a character from DUART channel B's ring, waiting for one, and
 * sends XON once the ring has room again (installer::0x1031ce). */
uint8_t installer_drtb_getc(void) {
  uint8_t character;

  installer_psos_get_v(SIG_SIQ, 0, 0);
  if (installer_duartb_ring.write == installer_duartb_ring.read &&
      (uint8_t)installer_psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    installer_fatal_error("wait_v SIG_SIQ error in getc");
  }
  character = installer_duartb_ring.data[installer_duartb_ring.read++];
  if ((installer_duartb.flow & DUART_FLOW_XOFF_SENT) != 0 &&
      (uint8_t)(installer_duartb_ring.read - installer_duartb_ring.write) >
          DUART_XON_ABOVE_FREE) {
    installer_duartb.flow &= ~DUART_FLOW_XOFF_SENT;
    if (installer_duartb.xon_xoff) {
      installer_drtb_putc(DUART_XON);
    }
  }
  return character;
}

/* Reads a line from DUART channel B, its receive errors the byte
 * before its ring, as drtb_gets's are (installer::0x103268). */
int32_t installer_drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, installer_drtb_getc, &installer_duartb.echo,
                    &installer_duartb.last_terminator,
                    &installer_duartb_receive_errors,
                    &installer_duartb.read_errors, NULL, false);
}

/* Writes a character to channel B under the write lock, if it can take
 * it (installer::0x10383c). */
void installer_drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)installer_mutex_acquire(&installer_duartb.write_token) <= 0) {
    installer_drtb_puts(text);
    installer_mutex_release(&installer_duartb.write_token);
  }
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free
 * (installer::0x103878). */
void installer_drtb_send_async(char* block, uint8_t count) {
  if (installer_duartb.async_allowed && !installer_duartb.reconfiguring &&
      !installer_duartb.in_transmit_interrupt) {
    installer_duartb.async_next = block;
    installer_duartb.async_count = count;
    installer_duartb.async_pending = 1;
    installer_duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = installer_duart_shadow.imr;
  }
}
