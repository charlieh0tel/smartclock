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
