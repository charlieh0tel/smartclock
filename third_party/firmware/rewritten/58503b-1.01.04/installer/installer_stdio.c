/* The Z3817A 3752's installer/installer_stdio.c, copied for the 58503B
 * 1.01.04, each of its functions identical or relocated in image_diff's
 * matches (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses are the 58503B's.  The Z3817A's own: from
 * common/installer/installer_stdio.c and the Z3815A 4010's
 * rewritten/installer/installer_stdio.c, the Z3817A 3752's own beside
 * common/'s: the installer's copies of the formatting routines, whose bodies
 * are libc.h's, the same in both images, whose installers are one byte for
 * byte (docs/firmware/z3817a-3752.md, "The function check").  Each keeps its
 * generated prototype.  With them, from other images' rewritten C, of its
 * functions, those the 58503B's code carries identical or relocated, with the
 * static helpers they use (docs/firmware/58503b-1.01.04.md, "The link list");
 * their comments' addresses are the 58503B's, but where they name another
 * image. */

/* Formats into a buffer through doprnt and sprintf_put and ends it with
 * a NUL; returns doprnt's count.  44 bytes, the installer's last
 * routine: the export sizes it to the RAM after its code
 * (installer::0x10ac24). */
int32_t installer_vsprintf(char* s, char* format, void* arg) {
  sprintf_output output;

  output.cursor = s;
  const int32_t length =
      installer_doprnt(format, arg, installer_sprintf_put, &output);

  *output.cursor = '\0';
  return length;
}

/* Reads from the console, device 0, and its last read errors, the
 * installer's SCI driver's, as console_read reads sci's
 * (installer::0x103d2e). */
uint32_t installer_console_read(void* buffer, uint32_t length,
                                uint8_t* errors) {
  installer_psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = installer_sci.read_errors;
  }
  return length;
}
