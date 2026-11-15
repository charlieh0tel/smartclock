/* stdio.c's functions that are this image's own: the rest are
 * common/stdio.c's, the same in every image (docs/firmware/rewrite.md, "Shared
 * C").  console_read, which reads the console through this image's own I/O
 * table (docs/firmware/z3816a-4001.md, "The function check").  Each keeps its
 * generated prototype. */

/* Reads from the console, device 0, the SCI here, and its last read
 * errors, the SCI's (0x2f25c).  The Z3815A 4010's. */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = sci.read_errors;
  }
  return length;
}
