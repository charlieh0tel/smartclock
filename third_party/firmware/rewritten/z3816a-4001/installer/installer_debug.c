/* installer_debug.c's functions that are this image's own: the rest are
 * common/installer/installer_debug.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the Z3815A 4010's
 * rewritten/installer/installer_debug.c: of its functions, those the Z3816A's
 * code carries (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* installer_printf_polled's buffer: an array size, so #define.  80
 * bytes, half installer_printf's LIBC_PRINTF_LENGTH, where the 58503A's
 * has LIBC_PRINTF_LENGTH (installer::0x103d9e). */
#define INSTALLER_PRINTF_POLLED_LENGTH 80

/* Formats into a buffer of INSTALLER_PRINTF_POLLED_LENGTH through
 * installer_vsprintf and writes it to the console by polling; a longer
 * text runs past it, over the frame (installer::0x103d9e). */
void installer_printf_polled(char* format, ...) {
  char text[INSTALLER_PRINTF_POLLED_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  installer_vsprintf(text, format, arguments);
  va_end(arguments);
  installer_console_write_polled(text);
}
