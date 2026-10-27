/* The installer's copies of the program's exit (`platform/stdlib`; stdlib.c has
 * the primary's). Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Runs the atexit functions and reports the exit value with its own
 * return address; never returns. */
void installer_program_exit(uint32_t value) {
  installer_call_atexit_functions();
  installer_program_end_report(value, (uint32_t)__builtin_return_address(0));
  for (;;) {
  }
}

/* Builds "Prog end, returned N" in program_end_message and stops with
 * fatal_error_with_code, whose code is return_address, program_exit's;
 * never returns (the primary's is program_end_report).  The digits are
 * built backward from a stop byte at the buffer's end, where they stay. */
void installer_program_end_report(uint32_t value, uint32_t return_address) {
  uint8_t* digit = &installer_program_end_message[LIBC_PROGRAM_END_DIGITS_END];
  uint32_t magnitude = value;

  installer_program_end_message[0] = 0;
  for (const char* text = "Prog end, returned "; *text != '\0'; text++) {
    libc_message_append(installer_program_end_message, *text);
  }
  *digit = LIBC_PROGRAM_END_DIGITS_STOP;
  if ((int32_t)value < 0) {
    magnitude = -value;
    libc_message_append(installer_program_end_message, '-');
  }
  do {
    *--digit = magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  for (; *digit != LIBC_PROGRAM_END_DIGITS_STOP; digit++) {
    libc_message_append(installer_program_end_message, '0' + *digit);
  }
  installer_fatal_error_with_code((char*)installer_program_end_message,
                                  (uint32_t)__builtin_return_address(0), value,
                                  return_address);
  for (;;) {
  }
}

/* Calls the functions atexit registered, the last first; the count is
 * signed (installer_call_atexit_functions_entry). */
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
 * room.  The primary has no strtol. */
int32_t installer_strtol(char* s, char** end, int32_t base) {
  static const int32_t BASE_MOST = 36;
  static const int32_t LONG_LEAST = (int32_t)LIBC_SIGN;
  static const int32_t LONG_MOST = (int32_t)(LIBC_SIGN - 1);
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
    base = c != '0'                     ? LIBC_DECIMAL
           : s[1] == 'x' || s[1] == 'X' ? LIBC_HEXADECIMAL
                                        : LIBC_OCTAL;
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
  if (base == LIBC_HEXADECIMAL && c == '0' &&
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

/* Registers a function for installer_call_atexit_functions: 1 for NULL
 * or a full table, else 0; nothing calls it. */
int32_t installer_atexit(atexit_function* function) {
  static const uint16_t FUNCTIONS_MOST =
      sizeof installer_atexit_functions / sizeof installer_atexit_functions[0];

  if (function == NULL || installer_atexit_count == FUNCTIONS_MOST) {
    return 1;
  }
  installer_atexit_functions[(int16_t)installer_atexit_count++] = function;
  return 0;
}

/* Saves its caller's registers, frame, return and stack, and through
 * the installer's trap #1 the status register, in a buffer; returns 0,
 * and again the value installer_longjmp gives. */
uint32_t installer_setjmp(uint32_t* buffer) {
  libc_setjmp_save();
  return 0;
}

/* Restores the registers installer_setjmp saved in a buffer, and
 * through the installer's trap #1 its status register, and returns to
 * installer_setjmp's caller with value in D0. */
uint64_t installer_longjmp(uint32_t* buffer, uint32_t value) {
  libc_longjmp_restore();
}
