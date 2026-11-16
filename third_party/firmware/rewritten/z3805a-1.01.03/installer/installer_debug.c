/* The Z3805A 3543B's installer/installer_debug.c, the Z3805A 1.01.03's own
 * beside common/'s, whose installer is the 3543B's byte for byte
 * (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of its
 * functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_debug.c's. Its comments'
 * installer addresses are the 3543B's, which are this image's; a primary
 * address is moved to this image's. Each keeps its generated prototype. */

/* The installer's copies of the exception and fatal error reports
 * (`platform/debug`; debug.c has the primary's), simpler than the
 * primary's: an exception is reported as the boot code's, "Boot: ",
 * and kept nowhere, and a fatal error only stops.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

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
