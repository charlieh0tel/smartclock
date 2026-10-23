/* What the SCPI engine's files share (scpi_*.c): the console port's
 * object, the token and parameter kinds, the lexer's character classes,
 * a number's digits, the length of a string, a long parameter and its
 * value kinds, the comparison of two texts, and the lexer's routines its
 * installer copies share. The exported C includes
 * the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it.  The
 * structures are docs/firmware/structures.md's "Numeric SCPI
 * parameters", "A numeric parameter's units", "The SCPI lexer's token
 * and port" and "The SCPI session"; docs/scpi/README.md describes the
 * language. */

/* The console port's object in ROM: console_port_read, its method table
 * at 0x29dd4, then the prompts (structures.md, "The SCPI lexer's token
 * and port"); the export names no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x29e18;

/* The first value above a character, which scpi_console_putc writes as
 * CR LF. */
static const int16_t SCPI_PORT_CHARACTERS = 0x100;

/* What a port's read gives for the end of its input or a read error
 * (structures.md, "The SCPI session"). */
static const int16_t SCPI_PORT_END = 0x101;

/* console_read's error bits, which console_port_read and its installer
 * copy report: the DUART's received break and framing error, overrun
 * and parity error (MC68681, SRA), and its own bit 0. */
static const uint8_t SCPI_CONSOLE_BREAK_OR_FRAMING = 0xc0;

static const uint8_t SCPI_CONSOLE_OVERRUN = 0x10;

static const uint8_t SCPI_CONSOLE_PARITY = 0x20;

static const uint8_t SCPI_CONSOLE_COMMUNICATION = 0x01;

/* A node's path word, or a parameter's kind word, that is none. */
enum {
  SCPI_NONE = -1,
};

/* A number's digits: scpi_number.digits, d0 d1 ... d15, the integer
 * d0d1...d15 times 10 to the exponent, so that exponent + 16 of them
 * are its integer part.  A digit from 5 rounds the last kept up. */
static const uint8_t SCPI_NUMBER_DIGITS = 16;

static const uint8_t SCPI_NUMBER_BASE = 10;

/* scpi_real_to_text's resolution for every digit asked for, below a
 * double's least exponent, and which it converts zero at too. */
static const int16_t SCPI_REAL_EVERY_DIGIT = -308;

/* The significant digits scpi_real_to_text is given for a double and
 * for a float (the formatters; format_si). */
static const int16_t SCPI_REAL_DOUBLE_DIGITS = 15;

static const int16_t SCPI_REAL_FLOAT_DIGITS = 6;

/* A string parameter as scpi_parse_string gives it its handler. */
typedef struct scpi_engine_string_value {
  char* text;      /* +0x00 */
  uint16_t length; /* +0x04 */
} scpi_engine_string_value;

/* A text's length. */
static int32_t scpi_engine_length(char* text) {
  int32_t length = 0;

  while (text[length] != 0) {
    length++;
  }
  return length;
}

/* scpi_scan_token's kinds of token, by the character class it starts
 * with (the jump table at 0x26f20). */
enum {
  SCPI_TOKEN_SEMICOLON = 1,
  SCPI_TOKEN_END = 2,         /* a newline or an end */
  SCPI_TOKEN_SKIPPED_END = 3, /* SCPI_PORT_SKIPPED_END */
  SCPI_TOKEN_WHITE = 4,
  SCPI_TOKEN_CLOSE = 5,
  SCPI_TOKEN_AT = 6,
  SCPI_TOKEN_COMMA = 9,
  SCPI_TOKEN_STAR = 10,
  SCPI_TOKEN_QUESTION = 11,
  SCPI_TOKEN_COLON = 12,
  SCPI_TOKEN_NON_DECIMAL = 13, /* #H, #Q or #B and digits */
  SCPI_TOKEN_DECIMAL = 14,
  SCPI_TOKEN_STRING = 15,
  SCPI_TOKEN_OPEN = 16,
  SCPI_TOKEN_DOLLAR = 17, /* $ and a digit 1 to 9 */
  SCPI_TOKEN_BLOCK = 18,  /* a block's header */
  SCPI_TOKEN_KEYWORD = 19,
  SCPI_TOKEN_QUERY = 20,         /* a keyword and ? */
  SCPI_TOKEN_KEYWORD_COLON = 21, /* a keyword and : */
};

/* A parameter's kind, the index of its converter and of
 * scpi_parameter_kind_errors, by the kind of its token (the table at
 * 0x26ed8), from SCPI_TOKEN_NON_DECIMAL to SCPI_TOKEN_KEYWORD. */
enum {
  SCPI_PARAMETER_DECIMAL = 0,
  SCPI_PARAMETER_STRING = 1,
  SCPI_PARAMETER_CHARACTER = 2,
  SCPI_PARAMETER_BLOCK = 3,
  SCPI_PARAMETER_NON_DECIMAL = 4,
  SCPI_PARAMETER_EXPRESSION = 5,
};

/* A long parameter as scpi_long_resolve_bound takes it: its kind and,
 * for a number, its value. */
typedef struct scpi_engine_long_parameter {
  int16_t kind; /* +0x00 */
  union {
    int32_t value; /* +0x02: a number's */
    int16_t bound; /* +0x02: a bound's, SCPI_BOUND_MAXIMUM or _MINIMUM */
  };
} scpi_engine_long_parameter;

/* The parameters as the parsers leave them in the call's buffer, each
 * its kind -- SCPI_PARAMETER_DECIMAL, _CHARACTER for a keyword, or
 * SCPI_NONE for an optional one left out -- then its value and, for a
 * number, its unit (scpi_parse_short, _long, _float, scpi_parse_boolean,
 * scpi_parse_choice).  A short, a boolean, or a keyword's value in the
 * node's list: */
typedef struct scpi_word_parameter {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
} scpi_word_parameter;

/* A string's text, as scpi_parse_text gives it its handler. */
typedef struct scpi_text_parameter {
  int16_t kind; /* +0x00 */
  char* text;   /* +0x02 */
} scpi_text_parameter;

/* A short number and its unit. */
typedef struct scpi_short_parameter {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
  int16_t unit;  /* +0x04 */
} scpi_short_parameter;

/* A real number or a keyword, and the unit. */
typedef struct scpi_float_parameter {
  int16_t kind; /* +0x00 */
  union {
    float value;     /* +0x02: a number's */
    int16_t keyword; /* +0x02: a keyword's value in the node's list */
  };
  int16_t unit; /* +0x06 */
} scpi_float_parameter;

/* A long number or a keyword, and the unit. */
typedef struct scpi_long_parameter {
  int16_t kind; /* +0x00 */
  union {
    int32_t value;   /* +0x02: a number's */
    int16_t keyword; /* +0x02: a keyword's value in the node's list */
  };
  int16_t unit; /* +0x06 */
} scpi_long_parameter;

/* A short response item, as scpi_format_short reads it: 4 bytes, a
 * float's size. */
typedef struct scpi_short_item {
  int16_t value;  /* +0x00 */
  int16_t unused; /* +0x02 */
} scpi_short_item;

/* A numeric parameter's kind, as scpi_numeric_set, _get and
 * scpi_long_resolve_bound read it: a number, or MINimum or MAXimum, by
 * the bound after it. */
enum {
  SCPI_VALUE_NUMBER = 0,
  SCPI_VALUE_BOUND = 2,
  SCPI_BOUND_MAXIMUM = 0,
  SCPI_BOUND_MINIMUM = 1,
};

/* A choice of two keywords, 0 or 1, as the setters' switches take it
 * (0x2e3d0, 0x2e450, 0x2ee76); any other leaves otherwise, the D2 their
 * callers left, which the parser never makes them read
 * (docs/firmware/quirks.md). */
static uint8_t scpi_two_choice(int16_t choice, uint8_t otherwise) {
  return choice == 0 || choice == 1 ? choice : otherwise;
}

/* Whether two texts are the same. */
static bool scpi_parameters_same(const char* text, const char* other) {
  for (; *text == *other; text++, other++) {
    if (*text == 0) {
      return true;
    }
  }
  return false;
}

/* scpi_suffix_compare_tail's and its installer copy's body: how many
 * characters of a unit's reversed name match the suffix's end, 0 unless
 * all of the name does. */
static int16_t scpi_compare_suffix_tail(char* suffix, int32_t length,
                                        char* name) {
  int16_t count = length;
  int16_t matched = 0;

  while (matched < count && suffix[count - matched - 1] == name[matched]) {
    matched++;
  }
  return name[matched] == 0 ? matched : 0;
}

/* The character a response's text puts between its values. */
static const char SCPI_SEPARATOR = ',';

/* The lexer's, which its installer copies share (scpi_lexer.c,
 * installer_scpi_lexer.c). */

/* A port's values above a character: 0x102, which
 * scpi_skip_to_terminator reads past, and the three the lexer reports
 * (scpi_engine_port_error). */
enum {
  SCPI_PORT_SKIPPED_END = 0x102,
  SCPI_PORT_CLOSED = 0x103,
  SCPI_PORT_UNTERMINATED = 0x104,
  SCPI_PORT_INTERRUPTED = 0x105,
};

/* The lexer's results for them that are not conditions, which
 * scpi_interpret takes as the input's end (0x29596) and as a failure,
 * which it returns -1 for (0x295be); the others are conditions,
 * STATUS_SCPI_QUERY_UNTERMINATED and STATUS_SCPI_QUERY_INTERRUPTED. */
enum {
  SCPI_STATUS_INPUT_CLOSED = -102,
  SCPI_STATUS_PORT_FAILED = -103,
};

/* Not a condition: scpi_interpret takes it as the response's end, and
 * anything after it as STATUS_SCPI_QUERY_AFTER_INDEFINITE, -440 Query
 * UNTERMINATED after indefinite response (0x294a6, 0x29446). */
enum {
  SCPI_STATUS_INDEFINITE_RESPONSE = -90,
};

/* A unit's exponent for one that takes a multiplier before it, and
 * scpi_multiplier_exponent's result for no multiplier (structures.md,
 * "A numeric parameter's units"). */
static const int8_t SCPI_NO_MULTIPLIER = -128;

/* A port value's character class, by its image's class table, 0 to
 * 0x105, which the lexer indexes unchecked by the value as a signed
 * word (0x26f02): a high-bit character a port gives sign extended, as
 * console_port_read does (0x29c4e), reads the bytes before it. */
static scpi_char_class scpi_engine_class(const scpi_char_class* classes,
                                         int32_t character) {
  return classes[(int16_t)character];
}

/* The next value from a port, its own routine's (0x26ef6). */
static int16_t scpi_engine_read(scpi_port* input) { return input->read(input); }

/* The lexer's result for a port's error value. */
static int32_t scpi_engine_port_error(int16_t character) {
  switch (character) {
    case SCPI_PORT_CLOSED:
      return SCPI_STATUS_INPUT_CLOSED;
    case SCPI_PORT_UNTERMINATED:
      return STATUS_SCPI_QUERY_UNTERMINATED;
    case SCPI_PORT_INTERRUPTED:
      return STATUS_SCPI_QUERY_INTERRUPTED;
  }
  return SCPI_STATUS_PORT_FAILED;
}

/* scpi_char_upper's and its installer copy's body: a letter in upper
 * case, by its image's class table, which it indexes unchecked. */
static uint32_t scpi_engine_upper(const scpi_char_class* classes,
                                  uint32_t character) {
  /* Clears the lower-case bit, and the high byte of the value's
   * word with it (0x277e2). */
  static const uint32_t UPPER_CASE = 0xffff00df;

  if (classes[character] == SCPI_CLASS_LETTER) {
    character &= UPPER_CASE;
  }
  return character;
}

/* scpi_scan_exponent's and its installer copy's body: scans a number's
 * exponent, E and a signed integer, after its mantissa; an E with no
 * digits after it starts the suffix; classes is its image's class
 * table. */
static int32_t scpi_engine_scan_exponent(const scpi_char_class* classes,
                                         int32_t character, scpi_number* number,
                                         scpi_port* input) {
  static const char EXPONENT_LETTER = 'E';
  /* The most an exponent takes before another digit, a word's
   * tenth. */
  static const int16_t EXPONENT_MOST_TENTHS = 0xccb;

  if ((int16_t)scpi_engine_upper(classes, (int16_t)character) !=
      EXPONENT_LETTER) {
    input->methods->unread(input);
    number->exponent = 0;
    return 0;
  }
  int16_t next = scpi_engine_read(input);
  scpi_char_class class = classes[next];

  if (class != SCPI_CLASS_DIGIT && class != SCPI_CLASS_PLUS &&
      class != SCPI_CLASS_MINUS && class != SCPI_CLASS_WHITE) {
    number->suffix[number->suffix_length++] = EXPONENT_LETTER;
    input->methods->unread(input);
    number->exponent = 0;
    return 0;
  }
  while (class == SCPI_CLASS_WHITE) {
    next = scpi_engine_read(input);
    class = classes[next];
  }
  bool negative = class == SCPI_CLASS_MINUS;

  if (class == SCPI_CLASS_MINUS || class == SCPI_CLASS_PLUS) {
    next = scpi_engine_read(input);
    class = classes[next];
  }
  if (class == SCPI_CLASS_DIGIT) {
    int16_t exponent = next - '0';

    for (;;) {
      next = scpi_engine_read(input);
      if (classes[next] != SCPI_CLASS_DIGIT) {
        break;
      }
      if (exponent > EXPONENT_MOST_TENTHS) {
        return STATUS_SCPI_EXPONENT_TOO_LARGE;
      }
      exponent = exponent * SCPI_NUMBER_BASE + next - '0';
    }
    number->exponent = negative ? -exponent : exponent;
    input->methods->unread(input);
    return 0;
  }
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
  input->methods->unread(input);
  return STATUS_SCPI_INVALID_NUMBER;
}

/* scpi_multiplier_exponent's and its installer copy's body: a unit
 * multiplier's power of ten by its one or two letters;
 * SCPI_NO_MULTIPLIER for none. */
static int8_t scpi_engine_multiplier(int32_t name) {
  /* The image's table at 0x27b2e, scpi_multipliers, to a name of 0; the
   * installer's at installer::0x1057dc holds the same. */
  static const scpi_multiplier MULTIPLIERS[] = {
      {.name = 'M', .exponent = -3},
      {.name = 'K', .exponent = 3},
      {.name = 'U', .exponent = -6},
      {.name = 'M' << 8 | 'A', .exponent = 6},
      {.name = 'N', .exponent = -9},
      {.name = 'G', .exponent = 9},
      {.name = 'P', .exponent = -12},
      {.name = 'T', .exponent = 12},
      {.name = 'F', .exponent = -15},
      {.name = 'A', .exponent = -18},
      {.name = 'P' << 8 | 'E', .exponent = 15},
      {.name = 'E' << 8 | 'X', .exponent = 18},
      {0},
  };
  const scpi_multiplier* entry = MULTIPLIERS;

  do {
    if (entry->name == (uint16_t)name) {
      return entry->exponent;
    }
    entry++;
  } while (entry->name != 0);
  return SCPI_NO_MULTIPLIER;
}

/* scpi_match_suffix's and its installer copy's body: the unit a suffix
 * names, its index plus one, and its power of ten, allowing a one- or
 * two-letter multiplier before a unit that takes one; 0 for none. */
static int16_t scpi_engine_match_suffix(int32_t length, char* suffix,
                                        scpi_unit* units, int16_t* scale) {
  static const int16_t MULTIPLIER_MOST = 2;
  static const uint8_t LETTER_BITS = 8;
  int16_t count = length;

  for (int16_t index = 0; units[index].name != 0; index++) {
    int16_t matched =
        scpi_compare_suffix_tail(suffix, count, units[index].name);

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
      *scale = scpi_engine_multiplier(multiplier);
      if (*scale != SCPI_NO_MULTIPLIER) {
        return index + 1;
      }
    }
  }
  return 0;
}

/* scpi_apply_suffix's and its installer copy's body: matches a number's
 * suffix against the node's units, setting its scale and unit. */
static int32_t scpi_engine_apply_suffix(scpi_number* number, scpi_unit* units) {
  if (number->suffix_length == 0) {
    number->unit = 0;
    number->scale = 0;
    return 0;
  }
  if (units == 0) {
    return STATUS_SCPI_SUFFIX_NOT_ALLOWED;
  }
  number->unit = scpi_engine_match_suffix(number->suffix_length, number->suffix,
                                          units, &number->scale);
  if (number->unit == 0) {
    return STATUS_SCPI_INVALID_SUFFIX;
  }
  return 0;
}

/* The arena's, which its installer copies share (scpi_engine.c,
 * installer_scpi_engine.c). */

/* A size rounded up to even, as the arena hands out room. */
static uint32_t scpi_arena_even(int32_t size) { return (size + 1U) & ~1U; }

/* arena_alloc's and its installer copy's body: takes size bytes,
 * rounded up to even, from an arena; 0 when it does not hold them. */
static void* scpi_arena_alloc(int32_t size, scpi_arena* arena) {
  uint8_t* start = arena->next;
  uint8_t* end = start + scpi_arena_even(size);

  if (arena->last < end) {
    return 0;
  }
  arena->next = end;
  return start;
}

/* arena_remaining's and its installer copy's body: the bytes left in an
 * arena, and its next free byte. */
static void* scpi_arena_remaining(int32_t* remaining, scpi_arena* arena) {
  *remaining = arena->last - arena->next + 1;
  return arena->next;
}

/* arena_take's and its installer copy's body: takes size bytes,
 * rounded up to even, from a buffer: the buffer, the arena's next free
 * byte the one after them. */
static void* scpi_arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  uint8_t* start = buffer;

  arena->next = start + scpi_arena_even(size);
  return start;
}

/* arena_init's and its installer copy's body: makes an arena of a
 * buffer of size bytes, at its start, into its slot. */
static void scpi_arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena* arena = buffer;
  uint8_t* bytes = buffer;

  *slot = arena;
  arena->next = bytes + sizeof *arena;
  arena->last = bytes + (size & ~1U) - 1;
}
