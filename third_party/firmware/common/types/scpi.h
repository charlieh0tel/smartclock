/* The SCPI engine's ports, tokens, tree, parameters and responses, and pllc's
 * value kinds, as the images' smartclock.h declare them (each includes this
 * header where they stand): docs/firmware/rewrite.md, "Shared C".
 * Where a comment cites code, it is the Z3805A 3543B's, which the
 * 58503A's copy of the comment came from. */

/* A numeric descriptor's is_integer: how a pllc request carries its
 * values, pllc_request.values (structures.md, "The pllc task's SCPI
 * requests"). */
typedef enum __attribute__((packed)) pllc_value_kind {
  PLLC_VALUE_FLOAT = 0,   /* a float, in values[1] */
  PLLC_VALUE_INTEGER = 1, /* an integer, in values[0] */
  PLLC_VALUE_LIST = 2,    /* index + 1 longs, at values[0] */
  PLLC_VALUE_PAIR = 3     /* two, in values[0] and values[1] */
} pllc_value_kind;

typedef struct scpi_numeric_param {
  scpi_param_type type; /* SCPI_PARAM_NUMERIC */
  uint8_t last_index;   /* +0x01: values less one */
  int8_t* nv_widths;    /* +0x02: bits in the NV register; negative, signed */
  void* minimum;        /* +0x06: the least value accepted */
  void* maximum;        /* +0x0a: the greatest */
  scpi_validate_function*
      validate;   /* +0x0e: validate_num's check; clamp_to_limits */
  void* changed;  /* +0x12: called as each value is stored */
  void* value;    /* +0x16: the current value, in RAM */
  void* done;     /* +0x1a: called once they are stored */
  uint16_t flags; /* +0x1e: 2 saved in the NV register, 4 at 0x400000 */
  gps_request_code gps_request; /* +0x20: as scpi_setting_param's */
  uint8_t field_21;
  void* group_cell;           /* +0x22: a scpi_group_cells word, as
                                 scpi_setting_param's; a double parameter's
                                 (SCPI_PARAM_DOUBLE,
                                 scpi_param_SAMPle_FARR+0x4a) is instead its
                                 scpi_double_step
                                 (scpi_param_SAMPle_FARR+0x3e), which
                                 scpi_numeric_get_double alone reads */
  pllc_value_kind is_integer; /* +0x26 */
  uint8_t field_27;
  void* steps; /* +0x28: 8 bytes a value, whose routine at +4 gives validate
                  the step */
} scpi_numeric_param;

/* A value's step, one of scpi_numeric_param's steps, or on a setter's
 * stack for the clamp routines it calls: its routine, given the step,
 * the value and its index, gives the step; load_long gives the long at
 * +0. */
/* A keyword scpi_node_find looks up: its length, then its text from +2
 * (scpi_node_find+0x8, scpi_node_find+0x1c).  The text's size is
 * scpi_trg_defined's buffer's, the 20-byte frame at scpi_trg_defined. */
typedef struct scpi_keyword {
  int16_t length; /* +0x00 */
  char text[18];  /* +0x02 */
} scpi_keyword;

/* A decimal number scpi_scan_number parses into the lexer's token, and
 * the converters scpi_number_to_short to _double read: the parameters'
 * number docs/scpi/README.md describes.  0x26 bytes, the token's frame
 * in scpi_parse_message_unit (-0x28 to -0x2 of A6, scpi_parse_message_unit+0x22
 * and the word at -0x2). */
typedef struct scpi_number {
  int16_t negative;      /* +0x00: 1 for a leading '-' (scpi_scan_token+0x122
                            to scpi_scan_token+0x12c), which the signed
                            converters test (scpi_number_to_short+0x7e) */
  int8_t digits[16];     /* +0x02: the digits' values, 0 to 9
                            (scpi_scan_number+0xc4), zero-filled to 16
                            (scpi_scan_number+0x128); digits[0] 0 for a zero */
  int16_t exponent;      /* +0x12: the written exponent, scpi_scan_exponent's,
                            adjusted by the point's place
                            (scpi_scan_number+0x1c0 to
                            scpi_scan_number+0x1e4) */
  int16_t suffix_length; /* +0x14: the units suffix's characters
                            (scpi_scan_number+0x294), 0 for none */
  char suffix[12]; /* +0x16: the suffix in upper case (scpi_scan_number+0x22c);
                      a 13th character, which the limit at
                      scpi_scan_number+0x210 allows, lands in scale, which
                      scpi_apply_suffix then sets */
  int16_t scale;   /* +0x22: the suffix's power of ten, scpi_match_suffix's, 0
                      for no suffix (scpi_apply_suffix); the converters'
                      scale */
  int16_t
      unit; /* +0x24: the matched unit's index plus one, 0 for no suffix; the
               parsers copy it after the value (scpi_parse_short+0x34) */
} scpi_number;

/* A non-decimal number, #H, #Q or #B, token 0xd (scpi_scan_token+0x320). */
typedef struct scpi_token_non_decimal {
  uint16_t base;  /* +0x00: 16, 8 or 2 */
  uint32_t value; /* +0x02 */
} scpi_token_non_decimal;

/* A quoted string, token 0xf (scpi_scan_token+0x398, scpi_scan_token+0x39c):
 * its text, NUL terminated, taken from the arena scpi_scan_token is given. */
typedef struct scpi_token_string {
  uint16_t length; /* +0x00: without the NUL */
  char* text;      /* +0x02 */
} scpi_token_string;

/* A definite or indefinite length block's header, #0 or #n and n
 * digits, token 0x12. */
typedef struct scpi_token_block {
  uint32_t length;     /* +0x00: the digits' value (scpi_scan_token+0x204) */
  uint16_t indefinite; /* +0x04: 1 for #0 (scpi_scan_token+0x208), else 0
                          (scpi_scan_token+0x1ac) */
} scpi_token_block;

/* The SCPI lexer's class of a port's value, 0 to 0x105, as
 * scpi_char_classes gives it. */
typedef enum __attribute__((packed)) scpi_char_class {
  SCPI_CLASS_LETTER = 0,
  SCPI_CLASS_DIGIT = 1,
  SCPI_CLASS_UNDERSCORE = 2,
  SCPI_CLASS_WHITE = 3, /* controls and space */
  SCPI_CLASS_HASH = 4,
  SCPI_CLASS_QUOTE = 5, /* " and ' */
  SCPI_CLASS_OTHER = 6,
  SCPI_CLASS_COLON = 7,
  SCPI_CLASS_SEMICOLON = 8,
  SCPI_CLASS_QUESTION = 9,
  SCPI_CLASS_OPEN = 10,
  SCPI_CLASS_CLOSE = 11,
  SCPI_CLASS_STAR = 15,
  SCPI_CLASS_PLUS = 16,
  SCPI_CLASS_MINUS = 17,
  SCPI_CLASS_POINT = 18,
  SCPI_CLASS_SLASH = 19,
  SCPI_CLASS_COMMA = 20,
  SCPI_CLASS_NEWLINE = 25,
  SCPI_CLASS_END = 26,        /* 0x100 and SCPI_PORT_END */
  SCPI_CLASS_PORT_ERROR = 27, /* SCPI_PORT_CLOSED to SCPI_PORT_INTERRUPTED */
  SCPI_CLASS_SKIPPED_END =
      28, /* 0x102, which scpi_skip_to_terminator reads past */
  SCPI_CLASS_DOLLAR = 30,
  SCPI_CLASS_AT = 31,
} scpi_char_class;

/* The SCPI lexer's token: scpi_scan_token fills the member for the
 * kind of token it returns, and the parameter converters take the
 * number.  A keyword (0x13, 0x14 with '?', 0x15 with ':') is its length
 * and up to 16 upper-case characters (scpi_scan_token+0xb8,
 * scpi_scan_token+0xec); '$' and a digit 1 to 9 (0x11) is the digit, at +0
 * (scpi_scan_token+0x46a). */
typedef union scpi_token {
  scpi_number number;
  scpi_keyword keyword;
  scpi_token_non_decimal non_decimal;
  scpi_token_string string;
  scpi_token_block block;
  int16_t digit;
} scpi_token;

struct scpi_port;
/* A port's character routines.  read gives the next character, or
 * above 0xff an end: the console's gives 0x101 for a NUL; 0x103 to 0x105,
 * which the lexer reports as errors -102, -12 and -13 (scpi_scan_token+0x47c).
 */
typedef int32_t scpi_port_read_function(struct scpi_port* port);
typedef void scpi_port_unread_function(struct scpi_port* port);
typedef void scpi_port_put_function(struct scpi_port* port, int character);
typedef void scpi_port_write_function(struct scpi_port* port, char* text,
                                      int length);
typedef void scpi_port_bits_function(struct scpi_port* port, int bits);
typedef uint8_t scpi_port_byte_function(struct scpi_port* port);
typedef void scpi_port_close_function(struct scpi_port* port);
/* A port's signal: given one of scpi_interpret's codes
 * (scpi_response_begin_code to scpi_input_end_code), its error, which
 * scpi_interpret reports at a message's end (scpi_interpret+0x22c to
 * scpi_interpret+0x252); scpi_console_return_zero gives 0. */
typedef int32_t scpi_port_signal_function(struct scpi_port* port,
                                          uint32_t* code);

/* A port's method table, the console's at status_update_rqs+0x3c: what the SCPI
 * parser reads commands through, the formatters write responses through, and
 * the status system keeps the status byte through.  Untyped entries are
 * those whose arguments no caller shows. */
typedef struct scpi_port_methods {
  void* open; /* +0x00: scpi_session_init, which returns the port */
  scpi_port_close_function*
      close; /* +0x04: scpi_session_close, which reads no argument */
  scpi_port_read_function*
      read; /* +0x08: console_port_read, as the port's own */
  scpi_port_unread_function*
      unread;      /* +0x0c: scpi_input_ended: the next read gives the last
                      character again; the lexer calls it on the character
                      after a token (scpi_scan_token+0x98) */
  void* read_text; /* +0x10: scpi_console_read, up to so many characters */
  scpi_port_put_function* put; /* +0x14: scpi_console_putc; above 0xff, CR LF */
  scpi_port_write_function*
      write; /* +0x18: scpi_console_write, up to length characters */
  scpi_port_write_function*
      write_block; /* +0x1c: scpi_console_write; scpi_format_block's */
  void* field_20;  /* +0x20: scpi_console_return_zero */
  void* field_24;  /* +0x24: scpi_console_return_zero */
  scpi_port_signal_function*
      signal;     /* +0x28: scpi_console_return_zero; scpi_interpret passes it
                     scpi_response_begin_code, scpi_message_end_code or
                     scpi_input_end_code (scpi_interpret+0x18c,
                     scpi_interpret+0x22c, scpi_interpret+0x2c6) */
  void* field_2c; /* +0x2c: scpi_console_return_zero */
  scpi_port_bits_function*
      status_set_bits; /* +0x30: status_byte_set_bits; status_condition_set's */
  scpi_port_bits_function* status_clear_bits; /* +0x34: status_byte_clear_bits;
                                                 status_condition_clear's */
  scpi_port_byte_function* status_byte_get;   /* +0x38: status_byte_get */
  scpi_port_bits_function* sre_set;           /* +0x3c: status_sre_set */
  scpi_port_byte_function* sre_get;           /* +0x40: status_sre_get */
} scpi_port_methods;

/* A port: the console's is at status_update_rqs+0x80, which scpi_session_init
 * returns. The lexer reads through read, and through methods for the rest
 * (scpi_scan_token+0x16, scpi_scan_token+0x9a). */
typedef struct scpi_port {
  scpi_port_read_function* read; /* +0x00: console_port_read */
  scpi_port_methods* methods;    /* +0x04: status_update_rqs+0x3c */
} scpi_port;

struct scpi_step;
/* A step's routine: the step for value, the setting at index. */
typedef int32_t scpi_step_function(struct scpi_step* step, void* value,
                                   int32_t index);
typedef struct scpi_step {
  uint32_t value;               /* +0x00: the step's bits, a float or a long */
  scpi_step_function* function; /* +0x04 */
} scpi_step;

struct scpi_double_step;
/* A double step's routine: the step for value, the setting at index,
 * in D0:D1 (clamp_double_to_step_and_limits+0x32, both stored at
 * clamp_double_to_step_and_limits+0x3c). */
typedef double scpi_double_step_function(struct scpi_double_step* step,
                                         double* value, int32_t index);
/* A double parameter's step, as clamp_double_to_step_and_limits reads
 * it: its routine follows 8 bytes, where scpi_step's follows 4
 * (clamp_double_to_step_and_limits+0x2e). */
typedef struct scpi_double_step {
  double value; /* +0x00: the step, which load_double gives */
  scpi_double_step_function* function; /* +0x08 */
} scpi_double_step;

/* The packed decimal record scpi_number_to_bcd fills and
 * scpi_bcd_to_double reads (docs/scpi/README.md, "Parameters and
 * responses"): two digits a byte, and the exponent of the 16 digits'
 * integer plus 16 and the scale. */
typedef struct scpi_bcd {
  int16_t negative;  /* +0x00 */
  uint8_t digits[8]; /* +0x02 */
  int16_t exponent;  /* +0x0a */
} scpi_bcd;

/* An arena, at the start of the buffer it hands out: its next free
 * byte and its last (arena_init, arena_init). */
typedef struct scpi_arena {
  uint8_t* next; /* +0x00 */
  uint8_t* last; /* +0x04 */
} scpi_arena;

/* A choice parameter's list, the node's for scpi_parse_choice and
 * scpi_format_choice: its keywords to a NULL, and the value each gives,
 * or NULL for its index. */
typedef struct scpi_choices {
  char** keywords; /* +0x00 */
  int16_t* values; /* +0x04 */
} scpi_choices;

struct scpi_call;
/* A handler, the routine a node's setter or query entry holds: the
 * call's parameters, its response and the call (scpi_call_handler,
 * scpi_call_handler), as SmartClockSetup.java types every handler. */
typedef int32_t scpi_handler_function(void* parameters, void* response,
                                      struct scpi_call* call);

/* A response item's formatter: the port, the value at its place in the
 * response, the item's choice list and the call (scpi_format_response,
 * scpi_format_response); only scpi_format_choice reads the list. */
typedef int32_t scpi_format_function(struct scpi_port* output, void* value,
                                     scpi_choices* choices,
                                     struct scpi_call* call);

/* A parameter's descriptor, what scpi_parameter_match and
 * scpi_parse_message_unit read (scpi_parse_message_unit+0x188 to
 * scpi_parse_message_unit+0x1ca), 0x24 bytes apart in every image
 * (docs/scpi/README.md, "As C"). */
typedef struct scpi_parameter_kinds {
  int16_t size;          /* +0x00: the value's bytes, after its kind word */
  int16_t optional;      /* +0x02: nonzero when it may be left out */
  scpi_unit* units;      /* +0x04 */
  scpi_choices* choices; /* +0x08 */
  void* converters[6];   /* +0x0c: one for each parameter kind, scpi.h's
                            SCPI_PARAMETER_DECIMAL to _EXPRESSION, by which the
                            parser indexes it; NULL for one it does not take;
                            each kind's routine takes its own arguments */
} scpi_parameter_kinds;

/* A parameter a node takes, one of the list scpi_parameter_match walks. */
typedef struct scpi_parameter {
  struct scpi_parameter* next; /* +0x00 */
  scpi_parameter_kinds* kinds; /* +0x04 */
} scpi_parameter;

/* A node's setter or query entry, 10 bytes (docs/scpi/README.md, "How
 * it is read"; scpi_call_handler, scpi_call_handler+0x16). */
typedef struct scpi_handler {
  scpi_handler_function* function; /* +0x00 */
  scpi_parameter* parameters;      /* +0x04: what it takes
                                      (scpi_header_resolve,
                                      scpi_header_resolve) */
  int16_t parameters_size; /* +0x08: the bytes they take in the call's buffer */
} scpi_handler;

/* One item of a query's response: how scpi_format_response writes the
 * value at its place in the response, 10 bytes (scpi_format_response+0x4c). */
typedef struct scpi_response_item {
  scpi_format_function* format; /* +0x00: NULL after the last */
  int16_t size;                 /* +0x04: the value's bytes */
  scpi_choices* choices; /* +0x06: the node's list, for scpi_format_choice;
                            NULL for every other formatter */
} scpi_response_item;

/* A node of the SCPI tree (docs/scpi/README.md, "How it is read"), 0x32
 * bytes: every node in every image is followed by 14 zero bytes before
 * the next record, whatever it is, and no routine reads them. */
typedef struct scpi_node {
  char* keywords; /* +0x00: the short form, a NUL, the rest of the long form */
  struct scpi_child_list* children; /* +0x04: NULL for a leaf */
  scpi_handler handlers[2];         /* +0x08: the setter, then the query */
  scpi_response_item*
      response_items;    /* +0x1c: the query's (scpi_interpret+0x1b2) */
  int16_t response_size; /* +0x20: the bytes the query's response takes
                            (scpi_call_handler+0x24) */
  int16_t path_word;     /* +0x22: the word it adds to the path, -1 for none
                            (scpi_node_find_with_defaults+0x2e) */
  uint8_t unused[14];    /* +0x24: zero */
} scpi_node;

/* A node's children, sorted by keyword for scpi_node_find's bisection
 * (docs/scpi/README.md, "How it is read"), the main tree's root list
 * among them.  The children follow the record, an array of their own
 * that a names-file row types. */
typedef struct scpi_child_list {
  uint16_t id;           /* +0x00 */
  int16_t count;         /* +0x02 */
  int16_t default_child; /* +0x04: the child a header may leave out, -1 for
                            none (scpi_node_find_with_defaults+0x5e) */
  scpi_node* children[]; /* +0x06 */
} scpi_child_list;
