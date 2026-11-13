/* installer_stdio.c's functions that are this image's own: the rest are
 * common/installer/installer_stdio.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the Z3815A 4010's
 * rewritten/installer/installer_stdio.c, whose installer is this image's
 * byte for byte (docs/firmware/z3817a-3752.md, "The function check").
 * Each keeps its generated prototype. */

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
