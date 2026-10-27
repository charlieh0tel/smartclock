/* The C library's formatted output and the console device, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `platform/stdio`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  console_read, which reads duartb, whose layout differs
 * between the images, is each image's own (its rewritten/stdio.c). */

/* Writes a string to the console, device 0; returns its length. */
uint32_t console_write(char* s) {
  uint32_t length;

  psos_io_write(IO_DEVICE_DUART_B, s, &length);
  return length;
}

/* Formats into a buffer of LIBC_PRINTF_LENGTH through vsprintf and
 * writes it to the console; returns console_write's length. */
int32_t printf(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  return console_write(text);
}

/* '0' plus *n % 10, dividing *n by ten. */
int32_t next_decimal_digit(uint32_t* n) { return libc_next_decimal_digit(n); }

/* The 128-bit product of the 64-bit numbers product[0..1] and
 * product[2..3], high word first, into product[0..3]. */
void double_to_digits_multiply(uint32_t* product) {
  libc_double_to_digits_multiply(product);
}

/* value's decimal digits into digits, NUL-terminated, its sign
 * through negative and through exponent the power of ten of its first
 * digit plus one; count digits after the point or significant ones, by
 * mode. */
void double_to_digits(double value, int32_t count, int32_t* exponent,
                      uint32_t* negative, char* digits, int32_t mode) {
  libc_double_to_digits(value, count, exponent, negative, digits, mode);
}

/* Copies so many items of doprnt's output, of a size of 1 only, to
 * sprintf's buffer; returns the count copied, 0 for another size. */
int32_t sprintf_put(char* text, int32_t size, int32_t count,
                    sprintf_output* output) {
  return libc_sprintf_put(text, size, count, output);
}

/* The formatting engine under sprintf, printf's, through put to
 * output; the count the conversions' widths make. */
int32_t doprnt(char* format, void* arguments, sprintf_put_function* put,
               sprintf_output* output) {
  return libc_doprnt(ctype_table + 1, format, arguments, put, output);
}

/* Formats into a buffer through doprnt and sprintf_put and ends it with
 * a NUL; returns doprnt's count. */
int32_t vsprintf(char* s, char* format, void* arg) {
  sprintf_output output;

  output.cursor = s;
  const int32_t length = doprnt(format, arg, sprintf_put, &output);

  *output.cursor = '\0';
  return length;
}

/* Formats into s through doprnt and sprintf_put and ends it with a NUL;
 * returns doprnt's count. */
int32_t sprintf(char* s, char* format, ...) {
  sprintf_output output;
  va_list arguments;

  output.cursor = s;
  va_start(arguments, format);
  const int32_t length = doprnt(format, arguments, sprintf_put, &output);

  va_end(arguments);
  *output.cursor = '\0';
  return length;
}
