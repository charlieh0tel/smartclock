/* From the 58503B 1.01.04's stdio.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those common/ does not hold, each identical
 * or relocated in image_diff's matches (docs/firmware/z3805a-1.01.03.md,
 * "The export and the checks"); their comments' addresses are the Z3805A
 * 1.01.03's.  The 58503B's note follows. */

/* The Z3817A 3752's stdio.c, copied for the 58503B 1.01.04, each of its
 * functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses were the 58503B's.  The Z3817A's own: from
 * common/stdio.c, the Z3817A 3752's until it shares common/: the C
 * library's formatting routines, whose bodies are libc.h's, each the same
 * in both images (docs/firmware/z3817a-3752.md, "The function check");
 * not printf or console_write, which write the console through this
 * image's own I/O table. Each keeps its generated prototype. */

/* Reads from the console, device 0, and its last read errors
 * (0x1df9e). */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = duartb.read_errors;
  }
  return length;
}
