/* The installer's copies of the program's exit (`platform/stdlib`; stdlib.c has
 * the primary's). Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Runs the atexit functions and reports the exit value with its own
 * return address; never returns (installer::0x1013ae). */
void installer_program_exit(uint32_t value) {
  installer_call_atexit_functions();
  installer_program_end_report(value, (uint32_t)__builtin_return_address(0));
  for (;;) {
  }
}

/* Builds "Prog end, returned N" in program_end_message and stops with
 * fatal_error_with_code, whose code is return_address, program_exit's;
 * never returns (0x149bc).  The digits are built backward from a stop
 * byte at the buffer's end, where they stay. */
void installer_program_end_report(uint32_t value, uint32_t return_address) {
  static const int32_t DIGITS_END = 0x2d;
  static const uint8_t DIGITS_STOP = 0xff;
  uint8_t* digit = &installer_program_end_message[DIGITS_END];
  uint32_t magnitude = value;

  installer_program_end_message[0] = 0;
  for (const char* text = "Prog end, returned "; *text != '\0'; text++) {
    libc_message_append(installer_program_end_message, *text);
  }
  *digit = DIGITS_STOP;
  if ((int32_t)value < 0) {
    magnitude = -value;
    libc_message_append(installer_program_end_message, '-');
  }
  do {
    *--digit = magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  for (; *digit != DIGITS_STOP; digit++) {
    libc_message_append(installer_program_end_message, '0' + *digit);
  }
  installer_fatal_error_with_code((char*)installer_program_end_message,
                                  (uint32_t)__builtin_return_address(0), value,
                                  return_address);
  for (;;) {
  }
}

/* Calls the functions atexit registered, the last first; the count is
 * signed (installer::0x109036). */
void installer_call_atexit_functions(void) {
  while ((int16_t)installer_atexit_count > 0) {
    installer_atexit_count--;
    installer_atexit_functions[(int16_t)installer_atexit_count]();
  }
}

/* A long from a string in a base, 2 to 36, or 0 for C's prefixes, and
 * through end where it stopped; 0 for a base out of range or no digit,
 * and on an overflow the long's limit, with errno ERANGE.  White space
 * and a sign are skipped only before a character that is not a digit or
 * a letter.  It counts down in negatives, so that the least long has
 * room.  The primary has no strtol (installer::0x1091ac). */
int32_t installer_strtol(char* s, char** end, int32_t base) {
  static const int32_t BASE_MOST = 36;
  static const int32_t LONG_LEAST = (int32_t)LIBC_SIGN;
  static const int32_t LONG_MOST = (int32_t)(LIBC_SIGN - 1);
  static const int32_t HEXADECIMAL = 16;
  static const int32_t OCTAL = 8;
  static const int32_t DECIMAL = 10;
  /* A letter's digit: 10 for a or A on. */
  static const int32_t LOWER_CASE = 0x20;
  static const int32_t LETTER_TO_DIGIT = 'a' - 10;
  static const uint8_t ALPHANUMERIC =
      LIBC_CTYPE_UPPER | LIBC_CTYPE_LOWER | LIBC_CTYPE_DIGIT;
  /* The ctype table from its EOF entry, which a character indexes signed:
   * 0x80 to 0xfe read the 127 bytes before the table, 0xff its EOF
   * entry. */
  const uint8_t* classes = installer_ctype_table + 1;
  bool negative = false;
  bool overflow = false;

  if (end != NULL) {
    *end = s;
  }
  if (base < 0 || base > BASE_MOST) {
    return 0;
  }
  int32_t c = (int8_t)*s;

  if ((classes[c] & ALPHANUMERIC) == 0) {
    while ((classes[c] & LIBC_CTYPE_SPACE) != 0) {
      c = (int8_t)*++s;
    }
    if (c == '-') {
      negative = true;
      c = (int8_t)*++s;
    } else if (c == '+') {
      c = (int8_t)*++s;
    }
  }
  if (base == 0) {
    base = c != '0'                     ? DECIMAL
           : s[1] == 'x' || s[1] == 'X' ? HEXADECIMAL
                                        : OCTAL;
  }
  if ((classes[c] & ALPHANUMERIC) == 0) {
    return 0;
  }
  int32_t digit = (classes[c] & LIBC_CTYPE_DIGIT) != 0
                      ? c - '0'
                      : (c | LOWER_CASE) - LETTER_TO_DIGIT;

  if (digit >= base) {
    return 0;
  }
  if (base == HEXADECIMAL && c == '0' &&
      (classes[(int8_t)s[2]] & LIBC_CTYPE_HEX) != 0 &&
      (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    c = (int8_t)*s;
    digit = (classes[c] & LIBC_CTYPE_DIGIT) != 0
                ? c - '0'
                : (c | LOWER_CASE) - LETTER_TO_DIGIT;
  }
  int32_t value = -digit;

  for (;;) {
    c = (int8_t)*++s;
    if ((classes[c] & ALPHANUMERIC) == 0) {
      break;
    }
    digit = (classes[c] & LIBC_CTYPE_DIGIT) != 0
                ? c - '0'
                : (c | LOWER_CASE) - LETTER_TO_DIGIT;
    if (digit >= base) {
      break;
    }
    const int32_t next = value * base - digit;

    overflow |= value < next;
    value = next;
  }
  if (end != NULL) {
    *end = s;
  }
  if (value == LONG_LEAST && !negative) {
    overflow = true;
  }
  if (overflow) {
    installer_errno = LIBC_ERANGE;
    return negative ? LONG_LEAST : LONG_MOST;
  }
  return negative ? value : -value;
}
