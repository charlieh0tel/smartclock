/* The Z3817A 3752's stdio.c, copied for the 58503B 1.01.04, each of its
 * functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses are the 58503B's.  The Z3817A's own: from
 * common/stdio.c, the Z3817A 3752's own beside common/'s: the C library's
 * formatting routines, whose bodies are libc.h's, each the same in both
 * images (docs/firmware/z3817a-3752.md, "The function check"); not printf or
 * console_write, which write the console through this image's own I/O table.
 * Each keeps its generated prototype. */

/* Reads from the console, device 0, and its last read errors, the
 * SCI's, the Z3815A's (0x2e8a6). */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = sci.read_errors;
  }
  return length;
}
