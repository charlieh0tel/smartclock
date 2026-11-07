/* strtol and strtoul (`platform/stdlib`), the image's own: the other
 * images' primaries have neither, and the NMEA decoder calls them
 * (gps_nmea.c); common/stdlib.c has the rest of the general utilities.  strtol
 * is the installer's installer_strtol, the same instructions
 * (installer::0x10932a) on the primary's ctype_table and errno.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* strtol on the primary's ctype table and errno (libc_strtol)
 * (0x6eeec). */
int32_t strtol(char* s, char** end, int32_t base) {
  return libc_strtol(s, end, base, ctype_table, &errno);
}

/* An unsigned long from a string, as strtol reads one (0x6f0a6), but
 * for three things.  Its "0x" is taken through toupper (0x6f192).  An
 * overflow is a value that a digit leaves less than it was, so that one
 * that wraps past it goes unseen (a firmware fault, quirks.md); at the
 * first it stops, errno ERANGE, end one past that digit, and returns
 * ULONG_MAX.  And a value past LONG_MAX after a minus sign is an
 * overflow too, ULONG_MAX with errno ERANGE; any other after one is
 * negated. */
uint32_t strtoul(char* s, char** end, int32_t base) {
  static const uint32_t LONG_MOST = LIBC_SIGN - 1;
  static const uint32_t UNSIGNED_LONG_MOST = ~(uint32_t)0;
  const uint8_t* classes = ctype_table + 1;
  bool negative = false;

  if (end != NULL) {
    *end = s;
  }
  if (base < 0 || base > LIBC_STRTOL_BASE_MOST) {
    return 0;
  }
  int32_t c = (int8_t)*s;

  if ((classes[c] & LIBC_STRTOL_ALPHANUMERIC) == 0) {
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
  if ((classes[c] & LIBC_STRTOL_ALPHANUMERIC) == 0 ||
      libc_strtol_digit(classes, c) >= base) {
    return 0;
  }
  if (base == LIBC_HEXADECIMAL && c == '0' &&
      (classes[(int8_t)s[2]] & LIBC_CTYPE_HEX) != 0 &&
      toupper((int8_t)s[1]) == 'X') {
    s += 2;
    c = (int8_t)*s;
  }
  uint32_t value = libc_strtol_digit(classes, c);

  for (;;) {
    c = (int8_t)*++s;
    if ((classes[c] & LIBC_STRTOL_ALPHANUMERIC) == 0) {
      break;
    }
    const int32_t digit = libc_strtol_digit(classes, c);

    if (digit >= base) {
      break;
    }
    const uint32_t next = value * base + digit;

    if (value > next) {
      errno = LIBC_ERANGE;
      if (end != NULL) {
        *end = s + 1;
      }
      return UNSIGNED_LONG_MOST;
    }
    value = next;
  }
  if (end != NULL) {
    *end = s;
  }
  if (value > LONG_MOST && negative) {
    errno = LIBC_ERANGE;
    return UNSIGNED_LONG_MOST;
  }
  return negative ? -value : value;
}
