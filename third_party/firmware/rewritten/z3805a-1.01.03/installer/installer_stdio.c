/* The Z3805A 3543B's installer/installer_stdio.c, the Z3805A 1.01.03's own
 * beside common/'s, whose installer is the 3543B's byte for byte
 * (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of its
 * functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_stdio.c's. Its comments'
 * installer addresses are the 3543B's, which are this image's; a primary
 * address is moved to this image's. Each keeps its generated prototype. */

/* The installer's copies of the formatting routines (`platform/stdio`;
 * stdio.c has the primary's), whose bodies, libc.h's, both share; but
 * vsprintf, which differs between the images, and console_read, which
 * reads installer_duartb, each image's own (its
 * rewritten/installer/installer_stdio.c).  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* The installer's vsprintf and console_read (`platform/stdio`), the
 * image's own: the 58503A's vsprintf differs, and console_read reads
 * installer_duartb, whose layout differs between the images;
 * common/installer/installer_stdio.c has the installer's other
 * formatting routines.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Formats into a buffer through doprnt and sprintf_put and ends it with
 * a NUL; returns doprnt's count (installer::0x10aaa6). */
int32_t installer_vsprintf(char* s, char* format, void* arg) {
  sprintf_output output;

  output.cursor = s;
  const int32_t length =
      installer_doprnt(format, arg, installer_sprintf_put, &output);

  *output.cursor = '\0';
  return length;
}

/* Reads from the console, device 0, and its last read errors
 * (installer::0x10392e). */
uint32_t installer_console_read(void* buffer, uint32_t length,
                                uint8_t* errors) {
  installer_psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = installer_duartb.read_errors;
  }
  return length;
}
