/* stdio.c's functions that are this image's own: the rest are
 * common/stdio.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Each keeps its generated prototype. */

/* Reads from the console, device 0, and its last read errors, the
 * SCI's (0x2f37c). */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = sci.read_errors;
  }
  return length;
}
