/* console_read (`platform/stdio`), the image's own: it reads duartb,
 * whose layout differs between the images; common/stdio.c has the rest
 * of stdio.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype. */

/* Reads from the console, device 0, and its last read errors
 * (0x1dd14). */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = duartb.read_errors;
  }
  return length;
}
