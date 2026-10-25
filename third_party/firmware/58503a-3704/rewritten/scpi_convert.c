/* SCPI numbers to and from text, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/convert`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  scpi_convert.h holds the bodies its installer copies
 * share. */

/* The engine's conversions are register routines that return in the
 * condition codes, which C has no names for: each is called in an asm
 * statement that binds its registers and reads the flag with Scc, into
 * memory where the routine changes every data register. */

/* Packs a parsed number into a BCD record at a scale, its exponent the
 * number's plus the scale plus 16: scpi_number_to_bcd (0x24752) takes
 * the number in A0, the scale in D0's word and the record in A1, and
 * changes D0, D1, A0 and A1; whether it failed, Z clear and D0 -1, for an
 * exponent beyond 309. */
__attribute__((always_inline)) static inline bool scpi_convert_number_to_bcd(
    scpi_number* number, int16_t scale, scpi_bcd* bcd) {
  register int32_t scpi_status __asm__("d0") = scale;
  register scpi_number* scpi_number_at __asm__("a0") = number;
  register scpi_bcd* scpi_bcd_at __asm__("a1") = bcd;
  uint8_t failed;

  __asm__ volatile("jsr scpi_number_to_bcd\n\tsne %[failed]"
                   : "+r"(scpi_status), "+r"(scpi_number_at),
                     "+r"(scpi_bcd_at), [failed] "=d"(failed)
                   :
                   : "d1", "cc", "memory");
  return failed != 0;
}

/* A BCD record's 16 digits as a double, scaled by its exponent:
 * scpi_bcd_to_double (0x242ce) takes the record in A0 and returns the
 * double in D0:D1, high first, through scpi_scale_by_power_of_ten
 * (0x24438) and scpi_double_multiply (0x24510), which change D2 to D7,
 * A0 and A1 too, as its caller expects, saving D2 to D7 (0x2470e);
 * whether it failed, Z set, for a number that does not fit a double
 * (0x244ac). */
__attribute__((always_inline)) static inline bool scpi_convert_bcd_to_double(
    scpi_bcd* bcd, double* value) {
  register uint32_t scpi_high __asm__("d0");
  register uint32_t scpi_low __asm__("d1");
  register scpi_bcd* scpi_bcd_at __asm__("a0") = bcd;
  uint8_t failed;

  __asm__ volatile("jsr scpi_bcd_to_double\n\tseq %[failed]"
                   : "=r"(scpi_high), "=r"(scpi_low),
                     "+r"(scpi_bcd_at), [failed] "=m"(failed)
                   :
                   : "d2", "d3", "d4", "d5", "d6", "d7", "a1", "cc", "memory");
  /* The double's bits, high first, as the routine leaves them. */
  union {
    uint32_t words[2];
    double value;
  } bits = {{scpi_high, scpi_low}};

  *value = bits.value;
  return failed != 0;
}

/* A double, its two longs high first, as a decimal record of so many
 * digits: scpi_double_to_decimal (0x2406c) takes them in D0:D1, the
 * digits in D7 and the record in A0, and changes D0 to D7, A0 and A1,
 * as its caller expects, saving D2 to D7 (0x24792); whether it failed,
 * Z set, for a subnormal, infinite or NaN double (0x24062). */
__attribute__((always_inline)) static inline bool
scpi_convert_double_to_decimal(int32_t high, int32_t low, int16_t digits,
                               scpi_decimal* decimal) {
  register int32_t scpi_high __asm__("d0") = high;
  register int32_t scpi_low __asm__("d1") = low;
  register int32_t scpi_digits __asm__("d7") = digits;
  register scpi_decimal* scpi_decimal_at __asm__("a0") = decimal;
  uint8_t failed;

  __asm__ volatile("jsr scpi_double_to_decimal\n\tseq %[failed]"
                   : "+r"(scpi_high), "+r"(scpi_low), "+r"(scpi_digits),
                     "+r"(scpi_decimal_at), [failed] "=m"(failed)
                   :
                   : "d2", "d3", "d4", "d5", "d6", "a1", "cc", "memory");
  return failed != 0;
}

/* A number at a scale as a double; -1 when it does not fit (0x2470e). */
int32_t scpi_number_to_double(scpi_number* number, double* value,
                              int32_t scale) {
  scpi_bcd bcd;
  double converted;

  if (scpi_convert_number_to_bcd(number, scale, &bcd)) {
    return -1;
  }
  if (scpi_convert_bcd_to_double(&bcd, &converted)) {
    return -1;
  }
  *value = converted;
  return 0;
}

/* Zero's text, "+0", or "+0.0" with the point, which raises the
 * exponent, the resolution, by one; where its "E" goes. */
static uint8_t* scpi_engine_zero_text(uint8_t* text, char point,
                                      int16_t* exponent) {
  text[0] = '+';
  text[1] = '0';
  if (!point) {
    return text + 2;
  }
  text[2] = '.';
  text[3] = '0';
  ++*exponent;
  return text + 4;
}

/* A double, given as its two longs, high first, as NR3 text "+d.ddEsxx":
 * so many significant digits, none below the resolution, and so many
 * exponent digits; with the point, a single digit is "+0.d" (0x2478e).
 * 1 when the text cannot be made: no digits, a double the conversion
 * refuses, or an exponent too wide. */
uint32_t scpi_real_to_text(int32_t high, int32_t low, uint8_t* text,
                           int16_t digits, int16_t exponent_width,
                           int16_t resolution, char point) {
  /* The exponent's four digits, two pairs from decimal_digit_pairs. */
  static const int8_t EXPONENT_MOST_DIGITS = 4;
  static const int16_t EXPONENT_PAIR = 100;
  const int16_t decimal_digits = sizeof(((scpi_decimal*)0)->digits);
  union {
    uint16_t pairs[2];
    char digits[4];
  } exponent_text;
  scpi_decimal decimal;
  bool zero = high == 0 && low == 0 && resolution != SCPI_REAL_EVERY_DIGIT;

  if (!zero) {
    if (digits <= 0) {
      return 1;
    }
    if (scpi_convert_double_to_decimal(high, low, digits, &decimal)) {
      return 1;
    }
    /* The digits above the resolution, a word's difference, as the image
     * takes it, though it compares the exponents themselves. */
    int16_t places = decimal.exponent - resolution;

    if (decimal.exponent < resolution) {
      zero = true;
    } else {
      if (places > digits) {
        places = digits;
      }
      if (places < decimal_digits) {
        scpi_round_decimal((char*)&decimal, places);
        places = decimal.exponent - resolution;
        if (decimal.exponent <= resolution) {
          zero = true;
        } else if (places <= digits) {
          digits = places;
        }
      }
    }
  }
  int16_t exponent = resolution;
  uint8_t* end;

  if (zero) {
    end = scpi_engine_zero_text(text, point, &exponent);
  } else {
    /* The image tests the sign word's low byte. */
    text[0] = (uint8_t)decimal.sign != 0 ? '-' : '+';
    text[1] = decimal.digits[0];
    text[2] = '.';
    exponent = (text[1] == '0' ? 1 : decimal.exponent) - 1;
    if (digits >= 2) {
      for (int16_t index = 1; index < digits; index++) {
        text[2 + index] = decimal.digits[index];
      }
      end = text + 2 + digits;
    } else if (point) {
      text[3] = text[1];
      text[1] = '0';
      exponent++;
      end = text + 4;
    } else {
      end = text + 2;
    }
  }
  *end++ = 'E';
  *end = '+';
  if (exponent < 0) {
    *end = '-';
    exponent = -exponent;
  }
  end++;
  int16_t hundreds = exponent / EXPONENT_PAIR;
  int16_t units = exponent % EXPONENT_PAIR;

  if (exponent < 0) {
    /* Only -32768 is negative still, and its divu.w overflows, leaving
     * the dividend 0xffff8000, so that the hundreds' pair is its low
     * word's, the first, and the units' its high word's, the word before
     * the table (0x24882). */
    hundreds = 0;
    units = -1;
  }
  /* Unchecked: hundreds from 100 read past the table (quirks.md). */
  exponent_text.pairs[0] = decimal_digit_pairs[hundreds];
  exponent_text.pairs[1] = decimal_digit_pairs[units];
  if (exponent_width <= 0) {
    return 1;
  }
  /* The image takes the width's low byte, signed (0x248ac). */
  int8_t width = exponent_width;

  if (width > EXPONENT_MOST_DIGITS) {
    return 1;
  }
  if (width < 0) {
    /* The image checks 4 - width bytes from these digits up, past them
     * into its frame and its caller's, the farthest first, failing
     * unless every one is '0'; C cannot read them, and fails. */
    return 1;
  }
  int8_t skipped = EXPONENT_MOST_DIGITS - width;

  for (int8_t index = 0; index < skipped; index++) {
    if (exponent_text.digits[index] != '0') {
      return 1;
    }
  }
  /* The image copies the whole width, so that one above 0xff reads past
   * these digits into its frame, which C cannot say; the C copies the low
   * byte's. */
  for (int8_t index = skipped; index < EXPONENT_MOST_DIGITS; index++) {
    *end++ = exponent_text.digits[index];
  }
  *end = '\0';
  return 0;
}

/* A number at a scale as a short, rounded (0x252ee). */
int32_t scpi_number_to_short(scpi_number* number, int16_t* value,
                             int32_t scale) {
  return scpi_engine_number_to_short(number, value, scale);
}

/* Its magnitude at a scale as an unsigned short, rounded (0x25394). */
int32_t scpi_number_to_unsigned_short(scpi_number* number, uint16_t* value,
                                      int32_t scale) {
  return scpi_engine_number_to_unsigned_short(number, value, scale);
}

/* A number at a scale as a long, rounded (0x25438). */
int32_t scpi_number_to_long(scpi_number* number, int32_t* value,
                            int32_t scale) {
  return scpi_engine_number_to_long(number, value, scale);
}

/* Its magnitude at a scale as an unsigned long, rounded (0x254de). */
int32_t scpi_number_to_unsigned_long(scpi_number* number, uint32_t* value,
                                     int32_t scale) {
  return scpi_engine_number_to_unsigned_long(number, value, scale);
}

/* A number at a scale as a boolean: nonzero once rounded (0x25580). */
void scpi_number_to_boolean(scpi_number* number, uint16_t* value,
                            int32_t scale) {
  int32_t places = scpi_engine_places(number, scale);

  *value = number->digits[0] != 0 &&
           (places > 0 ||
            (places == 0 && number->digits[0] >= SCPI_NUMBER_ROUND_UP));
}

/* A value's decimal digits right-aligned in digits, to its index 16,
 * and the first's index (0x25734). */
int32_t scpi_unsigned_to_digits(uint32_t value, char* digits, int16_t* start) {
  return scpi_engine_unsigned_to_digits(value, digits, start);
}
