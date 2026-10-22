/* What the SCPI engine's files share (scpi_*.c): the console port's
 * object, the token and parameter kinds, the lexer's character classes,
 * a number's digits, the length of a string, a long parameter and its
 * value kinds, and the comparison of two texts. The exported C includes
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

/* A node's path word, or a parameter's kind word, that is none. */
enum {
  SCPI_NONE = -1,
};

/* A number's digits: scpi_number.digits, d0 d1 ... d15, the integer
 * d0d1...d15 times 10 to the exponent, so that exponent + 16 of them
 * are its integer part.  A digit from 5 rounds the last kept up. */
static const uint8_t SCPI_NUMBER_DIGITS = 16;

static const uint8_t SCPI_NUMBER_BASE = 10;

enum {
  SCPI_ARRAY_BLOCK = 0,
  SCPI_ARRAY_DOUBLES = 1,
  SCPI_ARRAY_SHORTS = 2,
};

/* A string parameter as scpi_parse_string gives it its handler, and a
 * text response as scpi_query_failed empties it. */
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

/* A numeric parameter's kind, as scpi_numeric_set, _get and
 * scpi_long_resolve_bound read it: a number, or MINimum or MAXimum, by
 * the bound after it. */
enum {
  SCPI_VALUE_NUMBER = 0,
  SCPI_VALUE_BOUND = 2,
  SCPI_BOUND_MAXIMUM = 0,
  SCPI_BOUND_MINIMUM = 1,
};

/* Whether two texts are the same. */
static bool scpi_parameters_same(const char* text, const char* other) {
  for (; *text == *other; text++, other++) {
    if (*text == 0) {
      return true;
    }
  }
  return false;
}
