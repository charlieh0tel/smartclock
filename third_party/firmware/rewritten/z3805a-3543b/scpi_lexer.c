/* The SCPI lexer: tokens, numbers, units and suffixes, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/scpi/lexer`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* A non-decimal number's radix letters. */
enum {
  SCPI_RADIX_HEXADECIMAL = 'H',
  SCPI_RADIX_OCTAL = 'Q',
  SCPI_RADIX_BINARY = 'B',
};

/* A lexer's failure on a character: its port error, or else the
 * error given, the character given back. */
static int32_t scpi_engine_reject(scpi_port* input, int16_t character,
                                  int32_t error) {
  if (scpi_engine_class(character) == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(character);
  }
  input->methods->unread(input);
  return error;
}

/* A keyword from its first character: letters in upper case, digits and
 * underscores as they are, to 16 (0x26f8c). */
static int32_t scpi_engine_scan_keyword(scpi_port* input, scpi_keyword* keyword,
                                        int16_t first) {
  static const uint8_t UPPER_CASE = 0xdf;
  /* A keyword's most characters. */
  static const int16_t KEYWORD_MOST = 16;
  int16_t count = 0;
  int16_t character = first;
  scpi_char_class class = SCPI_CLASS_LETTER;

  for (;;) {
    keyword->text[count++] =
        class == SCPI_CLASS_LETTER ? character & UPPER_CASE : character;
    character = scpi_engine_read(input);
    class = scpi_engine_class(character);
    if (class > SCPI_CLASS_UNDERSCORE) {
      break;
    }
    if (count >= KEYWORD_MOST) {
      return STATUS_SCPI_MNEMONIC_TOO_LONG;
    }
  }
  keyword->length = count;
  if (character == '?') {
    return SCPI_TOKEN_QUERY;
  }
  if (character == ':') {
    return SCPI_TOKEN_KEYWORD_COLON;
  }
  return scpi_engine_reject(input, character, SCPI_TOKEN_KEYWORD);
}

/* A block's header after its #: a digit, 0 for an indefinite block, or
 * the count of the length's digits and the length (0x27076). */
static int32_t scpi_engine_scan_block(scpi_port* input, scpi_token_block* block,
                                      int16_t count_digit) {
  int16_t digits = count_digit - '0';

  if (digits == 0) {
    block->indefinite = 1;
    return SCPI_TOKEN_BLOCK;
  }
  block->indefinite = 0;
  uint32_t length = 0;

  for (int16_t index = 0; index < digits; index++) {
    int16_t character = scpi_engine_read(input);

    if (scpi_engine_class(character) != SCPI_CLASS_DIGIT) {
      return scpi_engine_reject(input, character, STATUS_SCPI_INVALID_BLOCK);
    }
    length = length * SCPI_NUMBER_BASE + (character - '0');
  }
  block->length = length;
  return SCPI_TOKEN_BLOCK;
}

/* A non-decimal number after its # and radix letter, base 2 to the
 * bits; digits past 32 bits make it out of range (0x27148). */
static int32_t scpi_engine_scan_radix(scpi_port* input, scpi_token* token,
                                      int16_t base, int16_t bits) {
  static const uint8_t UPPER_CASE = 0xdf;
  static const int16_t LONG_BITS = 32;
  /* The value of a letter digit in upper case, 'A' as 10. */
  static const int16_t LETTER_DIGIT_BIAS = 'A' - 10;
  /* The value's top digit, which must be clear for another. */
  uint32_t top = (uint32_t)(base - 1) << (LONG_BITS - bits);
  uint32_t value = 0;
  bool first = true;
  bool fits = true;

  for (;;) {
    int16_t character = scpi_engine_read(input);
    scpi_char_class class = scpi_engine_class(character);
    int16_t digit;

    if (class == SCPI_CLASS_LETTER) {
      digit = (character & UPPER_CASE) - LETTER_DIGIT_BIAS;
    } else if (class == SCPI_CLASS_DIGIT) {
      digit = character - '0';
    } else if (first) {
      return scpi_engine_reject(input, character, STATUS_SCPI_INVALID_DIGIT);
    } else {
      input->methods->unread(input);
      if (class == SCPI_CLASS_PORT_ERROR) {
        return scpi_engine_port_error(character);
      }
      if (!fits) {
        return STATUS_SCPI_DATA_OUT_OF_RANGE;
      }
      token->non_decimal.base = base;
      token->non_decimal.value = value;
      return SCPI_TOKEN_NON_DECIMAL;
    }
    if (digit >= base) {
      return STATUS_SCPI_INVALID_DIGIT;
    }
    if ((value & top) != 0) {
      fits = false;
    } else {
      value = value << bits | digit;
      first = false;
    }
  }
}

/* What follows a #: a block's header, or a non-decimal number by its
 * radix letter (0x27064). */
static int32_t scpi_engine_scan_hash(scpi_port* input, scpi_token* token) {
  static const uint8_t UPPER_CASE = 0xdf;
  static const int16_t HEXADECIMAL = 16;
  static const int16_t OCTAL = 8;
  static const int16_t BINARY = 2;
  int16_t character = scpi_engine_read(input);

  if (scpi_engine_class(character) == SCPI_CLASS_DIGIT) {
    return scpi_engine_scan_block(input, &token->block, character);
  }
  switch (character & UPPER_CASE) {
    case SCPI_RADIX_HEXADECIMAL:
      return scpi_engine_scan_radix(input, token, HEXADECIMAL, 4);
    case SCPI_RADIX_BINARY:
      return scpi_engine_scan_radix(input, token, BINARY, 1);
    case SCPI_RADIX_OCTAL:
      return scpi_engine_scan_radix(input, token, OCTAL, 3);
  }
  return scpi_engine_reject(input, character, STATUS_SCPI_INVALID_RADIX);
}

/* A quoted string, each doubled quote one, into room the arena gives:
 * its text, NUL-terminated, the room after it given back (0x27212). */
static int32_t scpi_engine_scan_string(scpi_port* input,
                                       scpi_token_string* string, int16_t quote,
                                       scpi_arena* arena) {
  /* A string's most characters with its NUL, the room scpi_scan_token
   * takes for it from the arena. */
  static const int16_t STRING_SIZE = 0xff;
  char* text = arena_alloc(STRING_SIZE, arena);
  int16_t count = 0;
  int16_t character;

  if (text == 0) {
    return STATUS_SCPI_TOO_MUCH_DATA;
  }
  do {
    character = scpi_engine_read(input);
    if (character >= SCPI_PORT_CHARACTERS) {
      goto ended;
    }
    if (character == quote && (character = scpi_engine_read(input)) != quote) {
      input->methods->unread(input);
      text[count] = 0;
      string->text = arena_take(text, count + 1, arena);
      string->length = count;
      return SCPI_TOKEN_STRING;
    }
    text[count++] = character;
  } while (count < STRING_SIZE);
  /* Too long: the rest of it, to its closing quote. */
  for (;;) {
    character = scpi_engine_read(input);
    if (character >= SCPI_PORT_CHARACTERS) {
      goto ended;
    }
    if (character == quote && (character = scpi_engine_read(input)) != quote) {
      break;
    }
  }
  input->methods->unread(input);
  return STATUS_SCPI_TOO_MUCH_DATA;
ended:
  return scpi_engine_reject(input, character, STATUS_SCPI_INVALID_STRING);
}

/* The SCPI lexer: the next token from the input, its kind or an error,
 * filling the token for a keyword, a number, a string, a block's header
 * or a $ digit; a string's text from the arena (0x26ee0). */
int32_t scpi_scan_token(scpi_port* input, scpi_token* token,
                        scpi_arena* arena) {
  int16_t character = scpi_engine_read(input);

  switch (scpi_engine_class(character)) {
    case SCPI_CLASS_LETTER:
      return scpi_engine_scan_keyword(input, &token->keyword, character);
    case SCPI_CLASS_DIGIT:
    case SCPI_CLASS_POINT:
      token->number.negative = 0;
      return scpi_scan_number(&token->number, 1, character, input);
    case SCPI_CLASS_PLUS:
    case SCPI_CLASS_MINUS: {
      token->number.negative = character == '-';
      int16_t next = scpi_engine_read(input);
      scpi_char_class class = scpi_engine_class(next);

      if (class == SCPI_CLASS_DIGIT || class == SCPI_CLASS_POINT) {
        return scpi_scan_number(&token->number, 1, next, input);
      }
      return scpi_engine_reject(input, next, STATUS_SCPI_INVALID_NUMBER);
    }
    case SCPI_CLASS_WHITE:
      do {
        character = scpi_engine_read(input);
      } while (scpi_engine_class(character) == SCPI_CLASS_WHITE);
      input->methods->unread(input);
      return SCPI_TOKEN_WHITE;
    case SCPI_CLASS_HASH:
      return scpi_engine_scan_hash(input, token);
    case SCPI_CLASS_QUOTE:
      return scpi_engine_scan_string(input, &token->string, character, arena);
    case SCPI_CLASS_DOLLAR: {
      int16_t digit = scpi_engine_read(input);

      if (scpi_engine_class(digit) == SCPI_CLASS_DIGIT && digit != '0') {
        token->digit = digit - '0';
        return SCPI_TOKEN_DOLLAR;
      }
      return scpi_engine_reject(input, digit, STATUS_SCPI_INVALID_CHARACTER);
    }
    case SCPI_CLASS_PORT_ERROR:
      return scpi_engine_port_error(character);
    case SCPI_CLASS_COLON:
      return SCPI_TOKEN_COLON;
    case SCPI_CLASS_SEMICOLON:
      return SCPI_TOKEN_SEMICOLON;
    case SCPI_CLASS_QUESTION:
      return SCPI_TOKEN_QUESTION;
    case SCPI_CLASS_OPEN:
      return SCPI_TOKEN_OPEN;
    case SCPI_CLASS_CLOSE:
      return SCPI_TOKEN_CLOSE;
    case SCPI_CLASS_STAR:
      return SCPI_TOKEN_STAR;
    case SCPI_CLASS_COMMA:
      return SCPI_TOKEN_COMMA;
    case SCPI_CLASS_NEWLINE:
    case SCPI_CLASS_END:
      return SCPI_TOKEN_END;
    case SCPI_CLASS_SKIPPED_END:
      return SCPI_TOKEN_SKIPPED_END;
    case SCPI_CLASS_AT:
      return SCPI_TOKEN_AT;
  }
  return STATUS_SCPI_INVALID_CHARACTER;
}

/* Reads to the next newline or an end, then gives the last value back
 * (0x27388). */
void scpi_skip_to_terminator(scpi_port* input) {
  scpi_port_read_function* read = input->read;
  int16_t character;

  do {
    character = read(input);
    if (character >= SCPI_PORT_CHARACTERS &&
        character != SCPI_PORT_SKIPPED_END) {
      break;
    }
  } while (scpi_engine_class(character) != SCPI_CLASS_NEWLINE);
  input->methods->unread(input);
}

/* Scans a decimal number's mantissa, exponent and units suffix into a
 * parsed number, from the character given when have_character is set,
 * else from the next; SCPI_TOKEN_DECIMAL, or an error (0x273cc).  Up to
 * 16 digits are kept, leading zeros dropped; shift is what the point's
 * place and the digits dropped take from the written exponent. */
int32_t scpi_scan_number(scpi_number* number, int32_t have_character,
                         int32_t character, scpi_port* input) {
  /* The most a number's exponent and shift take, a word's. */
  static const int16_t WORD_LEAST = -0x8000;
  static const int16_t WORD_MOST = 0x7fff;
  /* The suffix's last index it writes: 12 in suffix's 12 bytes, so
   * that a 13th character lands in scale (structures.md, "The SCPI
   * lexer's token and port"). */
  static const int16_t SUFFIX_LAST = 12;
  int16_t next = (int16_t)have_character != 0
                     ? (int16_t)character
                     : (int16_t)scpi_engine_read(input);
  scpi_char_class class = scpi_engine_class(next);
  int16_t digits = 0;
  int16_t point = 0;
  int16_t shift = 0;
  bool leading_zero = false;

  /* Leading zeros and the point before the first other digit. */
  for (;; next = scpi_engine_read(input), class = scpi_engine_class(next)) {
    if (next == '0') {
      shift -= point;
      leading_zero = true;
      if (shift == WORD_LEAST) {
        return STATUS_SCPI_TOO_MANY_DIGITS;
      }
    } else if (next == '.') {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else if (class == SCPI_CLASS_DIGIT) {
      break;
    } else if (leading_zero) {
      digits = 1;
      number->digits[0] = 0;
      break;
    } else {
      goto not_a_number;
    }
  }
  /* The digits kept. */
  for (;; next = scpi_engine_read(input), class = scpi_engine_class(next)) {
    if (class == SCPI_CLASS_DIGIT) {
      number->digits[digits++] = next - '0';
      shift -= point;
      if (shift == WORD_LEAST) {
        return STATUS_SCPI_TOO_MANY_DIGITS;
      }
      if (digits == SCPI_NUMBER_DIGITS) {
        break;
      }
    } else if (class == SCPI_CLASS_POINT) {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else if (digits != 0) {
      while (digits < SCPI_NUMBER_DIGITS) {
        number->digits[digits++] = 0;
        shift--;
        if (shift == WORD_LEAST) {
          return STATUS_SCPI_TOO_MANY_DIGITS;
        }
      }
      goto exponent;
    } else {
      goto not_a_number;
    }
  }
  /* The digits past 16, dropped. */
  for (;;) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
    if (class == SCPI_CLASS_DIGIT) {
      if (point == 0 && ++shift > WORD_MOST - 1) {
        return STATUS_SCPI_TOO_MANY_DIGITS;
      }
    } else if (class == SCPI_CLASS_POINT) {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else {
      break;
    }
  }
exponent:
  while (class == SCPI_CLASS_WHITE) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  }
  number->suffix_length = 0;
  int32_t error = scpi_scan_exponent(next, number, input);

  if (error != 0) {
    return error;
  }
  int32_t exponent = number->exponent + shift;

  if (exponent > WORD_MOST || exponent < WORD_LEAST) {
    return STATUS_SCPI_EXPONENT_TOO_LARGE;
  }
  number->exponent = exponent;
  do {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  } while (class == SCPI_CLASS_WHITE);
  /* The units suffix, which starts with a letter or a slash. */
  int16_t length = number->suffix_length;

  if (class == SCPI_CLASS_SLASH || class == SCPI_CLASS_LETTER) {
    while (class == SCPI_CLASS_MINUS || class == SCPI_CLASS_SLASH ||
           class == SCPI_CLASS_DIGIT || class == SCPI_CLASS_POINT ||
           class == SCPI_CLASS_LETTER) {
      if (length > SUFFIX_LAST) {
        return STATUS_SCPI_SUFFIX_TOO_LONG;
      }
      number->suffix[length++] = scpi_char_upper(next);
      next = scpi_engine_read(input);
      class = scpi_engine_class(next);
    }
  }
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
  input->methods->unread(input);
  number->suffix_length = length;
  return SCPI_TOKEN_DECIMAL;

not_a_number:
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
invalid:
  input->methods->unread(input);
  return STATUS_SCPI_INVALID_NUMBER;
}

/* Scans a number's exponent, E and a signed integer, after its
 * mantissa; an E with no digits after it starts the suffix (0x2766e). */
int32_t scpi_scan_exponent(int32_t character, scpi_number* number,
                           scpi_port* input) {
  return scpi_engine_scan_exponent(scpi_char_classes, character, number, input);
}

/* A letter in upper case, by the lexer's classes (0x277d2). */
uint32_t scpi_char_upper(uint32_t character) {
  return scpi_engine_upper(scpi_char_classes, character);
}

/* How many characters of a unit's reversed name match the suffix's end,
 * 0 unless all of the name does (0x279d6). */
int16_t scpi_suffix_compare_tail(char* suffix, int32_t length, char* name) {
  return scpi_compare_suffix_tail(suffix, length, name);
}

/* A unit multiplier's power of ten by its one or two letters;
 * SCPI_NO_MULTIPLIER for none (0x27a0c). */
int8_t scpi_multiplier_exponent(int32_t name) {
  return scpi_engine_multiplier(name);
}

/* The unit a suffix names, its index plus one, and its power of ten,
 * allowing a one- or two-letter multiplier before a unit that takes
 * one; 0 for none (0x27a2a). */
int16_t scpi_match_suffix(int32_t length, char* suffix, scpi_unit* units,
                          int16_t* scale) {
  static const int16_t MULTIPLIER_MOST = 2;
  static const uint8_t LETTER_BITS = 8;
  int16_t count = length;

  for (int16_t index = 0; units[index].name != 0; index++) {
    int16_t matched =
        scpi_suffix_compare_tail(suffix, count, units[index].name);

    if (matched == 0 || count - matched > MULTIPLIER_MOST) {
      continue;
    }
    if (count == matched) {
      *scale = units[index].exponent;
      if (*scale == SCPI_NO_MULTIPLIER) {
        *scale = 0;
      }
      return index + 1;
    }
    if (units[index].exponent == SCPI_NO_MULTIPLIER) {
      uint16_t multiplier = suffix[0];

      if (matched + 1 < count) {
        multiplier = (multiplier << LETTER_BITS) + suffix[1];
      }
      *scale = scpi_multiplier_exponent(multiplier);
      if (*scale != SCPI_NO_MULTIPLIER) {
        return index + 1;
      }
    }
  }
  return 0;
}

/* Matches a number's suffix against the node's units, setting its scale
 * and unit (0x27ae4). */
int32_t scpi_apply_suffix(scpi_number* number, scpi_unit* units) {
  if (number->suffix_length == 0) {
    number->unit = 0;
    number->scale = 0;
    return 0;
  }
  if (units == 0) {
    return STATUS_SCPI_SUFFIX_NOT_ALLOWED;
  }
  number->unit = scpi_match_suffix(number->suffix_length, number->suffix, units,
                                   &number->scale);
  if (number->unit == 0) {
    return STATUS_SCPI_INVALID_SUFFIX;
  }
  return 0;
}
