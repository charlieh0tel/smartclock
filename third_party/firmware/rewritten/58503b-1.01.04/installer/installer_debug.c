/* From the Z3815A 4010's rewritten/installer/installer_debug.c, the 58503B
 * 1.01.04's own beside common/'s: of its functions, those the 58503B's code
 * carries identical or relocated (docs/firmware/58503b-1.01.04.md, "The link
 * list").  Its comments' addresses are the 58503B's, but where they name
 * another image.  Each keeps its generated prototype. */

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
