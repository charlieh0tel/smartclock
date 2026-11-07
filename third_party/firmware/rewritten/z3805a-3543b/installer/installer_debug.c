/* installer_printf_polled: this image's own, split from
 * common/installer/installer_debug.c where it differs between the images
 * (rewrite.md, "Shared C", rule 6). */

/* Formats into a buffer of LIBC_PRINTF_LENGTH through installer_vsprintf
 * and writes it to the console by polling. */
void installer_printf_polled(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  installer_vsprintf(text, format, arguments);
  va_end(arguments);
  installer_console_write_polled(text);
}
