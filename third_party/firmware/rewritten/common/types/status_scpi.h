/* The status conditions of the SCPI standard, the same in every image:
 * the SCPI engine's negative codes, none, and the SCPI errors 1 to 0x16
 * queue.  Not a header of its own: the head of the members of each
 * image's status_condition_code, whose smartclock.h includes it inside
 * that enum, where the image's own conditions follow
 * (docs/firmware/rewrite.md, "Shared C"). */
/* clang-format off */
  STATUS_SCPI_NO_MEMORY = -56, /* -310 System error: an allocation failed */
  STATUS_SCPI_NUMERIC_DATA_ERROR = -55, /* -120 Numeric data error */
  STATUS_SCPI_GET_NOT_ALLOWED =
      -54, /* -105 GET not allowed: SCPI_PORT_SKIPPED_END after white space */
  STATUS_SCPI_SUFFIX_TOO_LONG = -53, /* -134 Suffix too long */
  STATUS_SCPI_TOO_MUCH_DATA = -52,   /* -223 Too much data */
  STATUS_SCPI_DATA_TYPE_ERROR = -51, /* -104 Data type error */
  STATUS_SCPI_EXPRESSION_NOT_ALLOWED =
      -50,                              /* -178 Expression data not allowed */
  STATUS_SCPI_BLOCK_NOT_ALLOWED = -49,  /* -168 Block data not allowed */
  STATUS_SCPI_STRING_NOT_ALLOWED = -48, /* -158 String data not allowed */
  STATUS_SCPI_CHARACTER_NOT_ALLOWED = -47, /* -148 Character data not allowed */
  STATUS_SCPI_NUMERIC_NOT_ALLOWED = -46,   /* -128 Numeric data not allowed */
  STATUS_SCPI_EXPONENT_TOO_LARGE = -45,    /* -123 Exponent too large */
  STATUS_SCPI_MACRO_EXECUTION_ERROR = -44, /* -272 Macro execution error */
  STATUS_SCPI_INVALID_SEPARATOR = -43,     /* -103 Invalid separator */
  STATUS_SCPI_MACRO_RECURSION = -42,       /* -276 Macro recursion error */
  STATUS_SCPI_INSIDE_MACRO = -41, /* -183 Invalid inside macro definition */
  STATUS_SCPI_FORMAT_FAILED =
      -40, /* -310 System error: a response it cannot format */
  STATUS_SCPI_INVALID_CHARACTER = -39,      /* -101 Invalid character */
  STATUS_SCPI_EXTRA_PARAMETER = -38,        /* -108 Parameter not allowed */
  STATUS_SCPI_MISSING_PARAMETER = -37,      /* -109 Missing parameter */
  STATUS_SCPI_MACRO_HEADER_NOT_FOUND = -36, /* -278 Macro header not found */
  STATUS_SCPI_ILLEGAL_MACRO_LABEL = -35,    /* -273 Illegal macro label */
  STATUS_SCPI_MACRO_REDEFINITION =
      -34, /* -277 Macro redefinition not allowed */
  STATUS_SCPI_HEADER_LEAF =
      -33, /* -113 Undefined header: a keyword past a leaf */
  STATUS_SCPI_UNDEFINED_HEADER = -32, /* -113 Undefined header */
  STATUS_SCPI_INVALID_SUFFIX = -31,   /* -131 Invalid suffix */
  STATUS_SCPI_PARAMETER_DOLLAR =
      -30, /* -181 Invalid outside macro definition: a $ digit as a parameter */
  STATUS_SCPI_NO_LIST =
      -28, /* -310 System error: a choice without its keywords */
  STATUS_SCPI_ILLEGAL_PARAMETER_VALUE = -27, /* -224 Illegal parameter value */
  STATUS_SCPI_EXPRESSION_ERROR = -24,        /* -170 Expression error */
  STATUS_SCPI_TOO_MANY_DIGITS = -23,         /* -124 Too many digits */
  STATUS_SCPI_STRING_ERROR = -22,            /* -150 String data error */
  STATUS_SCPI_INVALID_STRING = -21,          /* -151 Invalid string data */
  STATUS_SCPI_INVALID_DIGIT = -20,     /* -121 Invalid character in number */
  STATUS_SCPI_INVALID_RADIX = -19,     /* -101 Invalid character */
  STATUS_SCPI_INVALID_BLOCK = -18,     /* -161 Invalid block data */
  STATUS_SCPI_INVALID_NUMBER = -17,    /* -121 Invalid character in number */
  STATUS_SCPI_MNEMONIC_TOO_LONG = -16, /* -112 Program mnemonic too long */
  STATUS_SCPI_QUERY_AFTER_INDEFINITE =
      -15, /* -440 Query UNTERMINATED after indefinite response */
  STATUS_SCPI_QUERY_DEADLOCKED = -14,    /* -430 Query DEADLOCKED */
  STATUS_SCPI_QUERY_INTERRUPTED = -13,   /* -410 Query INTERRUPTED */
  STATUS_SCPI_QUERY_UNTERMINATED = -12,  /* -420 Query UNTERMINATED */
  STATUS_SCPI_DATA_OUT_OF_RANGE = -11,   /* -222 Data out of range */
  STATUS_SCPI_QUERY_ERROR = -10,         /* -400 Query error */
  STATUS_SCPI_SUFFIX_NOT_ALLOWED = -9,   /* -138 Suffix not allowed */
  STATUS_SCPI_EXECUTION_ERROR = -8,      /* -200 Execution error */
  STATUS_SCPI_COMMAND_ERROR = -7,        /* -100 Command error */
  STATUS_SCPI_SYNTAX_ERROR = -6,         /* -102 Syntax error */
  STATUS_SCPI_OUT_OF_MEMORY = -5,        /* -321 Out of memory */
  STATUS_ESR_QUERY_ERROR = -4,           /* Standard Event bit 2 */
  STATUS_ESR_DEVICE_ERROR = -3,          /* Standard Event bit 3 */
  STATUS_ESR_EXECUTION_ERROR = -2,       /* Standard Event bit 4 */
  STATUS_ESR_COMMAND_ERROR = -1,         /* Standard Event bit 5 */
  STATUS_NO_CONDITION = 0,               /* does nothing */
  STATUS_PARAMETER_NOT_ALLOWED = 0x01,   /* -108 Parameter not allowed */
  STATUS_MISSING_PARAMETER = 0x02,       /* -109 Missing parameter */
  STATUS_UNDEFINED_HEADER = 0x03,        /* -113 Undefined header */
  STATUS_INVALID_SUFFIX = 0x04,          /* -131 Invalid suffix */
  STATUS_INVALID_STRING_DATA = 0x05,     /* -151 Invalid string data */
  STATUS_EXECUTION_ERROR = 0x06,         /* -200 Execution error */
  STATUS_PARAMETER_ERROR = 0x07,         /* -220 Parameter error */
  STATUS_SETTINGS_CONFLICT = 0x08,       /* -221 Settings conflict */
  STATUS_DATA_OUT_OF_RANGE = 0x09,       /* -222 Data out of range */
  STATUS_TOO_MUCH_DATA = 0x0a,           /* -223 Too much data */
  STATUS_ILLEGAL_PARAMETER_VALUE = 0x0b, /* -224 Illegal parameter value */
  STATUS_DATA_CORRUPT_OR_STALE = 0x0c,   /* -230 Data corrupt or stale */
  STATUS_HARDWARE_MISSING = 0x0d,        /* -241 Hardware missing */
  STATUS_DEVICE_SPECIFIC_ERROR = 0x0e,   /* -300 Device-specific error */
  STATUS_SYSTEM_ERROR = 0x0f,            /* -310 System error */
  STATUS_MEMORY_ERROR = 0x10,            /* -311 Memory error */
  STATUS_CONFIGURATION_LOST = 0x11,      /* -315 Configuration memory lost */
  STATUS_SELFTEST_FAILED = 0x12,         /* -330 Self-test failed */
  STATUS_COMMUNICATION_ERROR = 0x13,     /* -360 Communication error */
  STATUS_PARITY_ERROR = 0x14,            /* -361 Parity error */
  STATUS_FRAMING_ERROR = 0x15,           /* -362 Framing error */
  STATUS_INPUT_OVERRUN = 0x16,           /* -363 Input buffer overrun */
    /* clang-format on */
