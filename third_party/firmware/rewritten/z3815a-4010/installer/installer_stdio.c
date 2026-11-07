/* installer_vsprintf and installer_console_read (`platform/stdio`), the
 * image's own: image_diff finds the 58503A's vsprintf changed from the
 * Z3805A's, and console_read reads the installer's SCI driver;
 * common/installer/installer_stdio.c has the installer's other
 * formatting routines.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype. */

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
