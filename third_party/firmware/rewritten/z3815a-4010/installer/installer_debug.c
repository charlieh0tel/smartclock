/* installer_printf_polled: this image's own, split from
 * common/installer/installer_debug.c where it differs between the images
 * (rewrite.md, "Shared C", rule 6): its installer_console_write_polled
 * polls the SCI. */

/* installer_printf_polled's buffer: an array size, so #define.  80
 * bytes, half installer_printf's LIBC_PRINTF_LENGTH, where the 58503A's
 * has LIBC_PRINTF_LENGTH (installer::0x103dba). */
#define INSTALLER_PRINTF_POLLED_LENGTH 80

/* Formats into a buffer of INSTALLER_PRINTF_POLLED_LENGTH through
 * installer_vsprintf and writes it to the console by polling; a longer
 * text runs past it, over the frame (installer::0x103dba). */
void installer_printf_polled(char* format, ...) {
  char text[INSTALLER_PRINTF_POLLED_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  installer_vsprintf(text, format, arguments);
  va_end(arguments);
  installer_console_write_polled(text);
}
