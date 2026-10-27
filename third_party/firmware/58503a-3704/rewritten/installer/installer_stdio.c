/* installer_console_read (`platform/stdio`), the image's own: it
 * reads installer_duartb, whose layout differs between the images;
 * common/installer/installer_stdio.c has the installer's other
 * formatting routines.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype. */

/* Reads from the console, device 0, and its last read errors
 * (installer::0x103902). */
uint32_t installer_console_read(void* buffer, uint32_t length,
                                uint8_t* errors) {
  installer_psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = installer_duartb.read_errors;
  }
  return length;
}
