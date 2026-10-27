/* The installer's copies of the formatting routines (`platform/stdio`;
 * stdio.c has the primary's), whose bodies, libc.h's, both share; but
 * vsprintf, which differs between the images, and console_read, which
 * reads installer_duartb, each image's own (its
 * rewritten/installer/installer_stdio.c).  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* '0' plus *n % 10, dividing *n by ten. */
int32_t installer_next_decimal_digit(uint32_t* n) {
  return libc_next_decimal_digit(n);
}

/* The 128-bit product of the 64-bit numbers product[0..1] and
 * product[2..3], high word first, into product[0..3]. */
void installer_double_to_digits_multiply(uint32_t* product) {
  libc_double_to_digits_multiply(product);
}

/* value's decimal digits into digits, NUL-terminated, its sign
 * through negative and through exponent the power of ten of its first
 * digit plus one; count digits after the point or significant ones, by
 * mode. */
void installer_double_to_digits(double value, int32_t count, int32_t* exponent,
                                uint32_t* negative, char* digits,
                                int32_t mode) {
  libc_double_to_digits(value, count, exponent, negative, digits, mode);
}

/* The formatting engine under sprintf, printf's, through put to
 * output; the count the conversions' widths make. */
int32_t installer_doprnt(char* format, void* arguments,
                         sprintf_put_function* put, sprintf_output* output) {
  return libc_doprnt(installer_ctype_table + 1, format, arguments, put, output);
}

/* Copies so many items of doprnt's output, of a size of 1 only, to
 * sprintf's buffer; returns the count copied, 0 for another size. */
int32_t installer_sprintf_put(char* text, int32_t size, int32_t count,
                              sprintf_output* output) {
  return libc_sprintf_put(text, size, count, output);
}

/* Formats into s through installer_doprnt and installer_sprintf_put and
 * ends it with a NUL; returns doprnt's count. */
int32_t installer_sprintf(char* s, char* format, ...) {
  sprintf_output output;
  va_list arguments;

  output.cursor = s;
  va_start(arguments, format);
  const int32_t length =
      installer_doprnt(format, arguments, installer_sprintf_put, &output);

  va_end(arguments);
  *output.cursor = '\0';
  return length;
}

/* Writes a string to the console, device 0; returns its length. */
uint32_t installer_console_write(char* s) {
  uint32_t length;

  installer_psos_io_write(IO_DEVICE_DUART_B, s, &length);
  return length;
}

/* Formats into a buffer of LIBC_PRINTF_LENGTH through installer_vsprintf
 * and writes it to the console; returns installer_console_write's
 * length. */
int32_t installer_printf(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  installer_vsprintf(text, format, arguments);
  va_end(arguments);
  return installer_console_write(text);
}
