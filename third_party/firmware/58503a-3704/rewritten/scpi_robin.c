/* The SCPI :ROBin tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/robin`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.
 *
 * :ROBin is the 59551A's time tagging, :SENSe:DATA (097-59551-02,
 * "Clearing Time Stamp Memory" to "Processing Memory Overflows"), under
 * another name (docs/scpi/undocumented.md), and a stub of it: a data
 * set's name, "TSTamp 1" or the like, selects a Time Tag input, but the
 * answers are made up from the input alone, and nothing is cleared. */

/* The input a name selects, 0 for "TSTamp 1"; or every input. */
enum {
  SCPI_ROBIN_ALL_INPUTS = '@',
};

/* The inputs SCPI_ROBIN_ALL_INPUTS answers for, and the last one a name
 * selects. */
#define SCPI_ROBIN_INPUTS 3

static const int8_t SCPI_ROBIN_LAST_INPUT = 3;

/* The points each input is made up to hold, more than its index. */
static const int16_t SCPI_ROBIN_FIRST_POINTS = 4;

/* What the parser answers for a name it does not take. */
static const int32_t SCPI_ROBIN_INVALID = STATUS_SCPI_INVALID_STRING;

/* A :ROBin handler's parameters: a data set's name, and :TSTamp?'s
 * entry. */
typedef struct scpi_robin_parameters {
  int16_t name_kind;  /* +0x00: SCPI_NONE when the name is left out */
  char* name;         /* +0x02 */
  int16_t entry_kind; /* +0x06 */
  int16_t entry;      /* +0x08: 1 to 256 */
} scpi_robin_parameters;

/* A keyword of a data set's name, as robin_lookup and
 * robin_keyword_match read it.  24 bytes. */
typedef struct scpi_robin_keyword {
  char short_form[5]; /* +0x00 */
  char long_form[11]; /* +0x05: the whole keyword */
  int32_t code;       /* +0x10: robin_keyword_code's, or one of the below */
  int32_t form;       /* +0x14: its arguments', SCPI_ROBIN_FORMS' index */
} scpi_robin_keyword;

/* A keyword's code: after a list's last keyword, and for one followed by
 * a ':' and another. */
enum {
  SCPI_ROBIN_END = -1,
  SCPI_ROBIN_BRANCH = -2,
};

/* The names robin_lookup takes, each a list of keywords to an end, a
 * keyword's code SCPI_ROBIN_END: "TSTamp" and an input's number
 * (0x2ef84), and a list that is only its end, after the last (0x2efb4);
 * the lists in turn at 0x2efcc. */
static const scpi_robin_keyword SCPI_ROBIN_TSTAMP[] = {
    {"TST", "TSTAMP", 0, 3},
    {"", "", SCPI_ROBIN_END, SCPI_ROBIN_END},
};

static const scpi_robin_keyword SCPI_ROBIN_NO_NAME[] = {
    {"", "", SCPI_ROBIN_END, SCPI_ROBIN_END},
};

static const scpi_robin_keyword* const SCPI_ROBIN_NAMES[] = {
    SCPI_ROBIN_TSTAMP,
    SCPI_ROBIN_NO_NAME,
};

/* A keyword's arguments, robin_parse_arguments' switch (0x2e590): an
 * optional number from 1 to a last, or an optional pair of numbers, the
 * first from 1 to a last and the second, when the first is 1, from 2 to
 * a last, else 1.  Left out, a pair is 1,2. */
typedef struct scpi_robin_form {
  bool pair;
  char last_first;  /* the first number's last digit */
  char last_second; /* the second's, after a 1 */
} scpi_robin_form;

static const scpi_robin_form SCPI_ROBIN_FORMS[] = {
    {false, '1', 0},  {false, '2', 0},  {false, '3', 0},  {false, '4', 0},
    {true, '1', '2'}, {true, '3', '3'}, {true, '2', '2'},
};

/* Steps a cursor past an optional XNON: or XNONE: data class, with or
 * without a ':' before it, or past a lone ':' (0x2e3d2). */
void robin_strip_xnon(char** cursor) {
  /* In the order the image tries them. */
  static const char* const PREFIXES[] = {
      "XNON:", ":XNON:", "XNONE:", ":XNONE:", ":"};
  char* text = *cursor;

  for (uint8_t index = 0; index < sizeof PREFIXES / sizeof *PREFIXES; index++) {
    char* prefix = (char*)PREFIXES[index];
    int32_t length = scpi_engine_length(prefix);

    if (strncmp(text, prefix, length) == 0) {
      *cursor = text + length;
      return;
    }
  }
}

/* Matches a keyword's long form, else its short, at a cursor and steps
 * past it: STATUS_SCPI_INVALID_STRING for neither; for a branch 0, past
 * the ':' that must follow, or STATUS_SCPI_INVALID_STRING; else the
 * code less SCPI_ROBIN_BRANCH, nonzero (0x2e490). */
int32_t robin_keyword_match(char* entry, char** cursor) {
  scpi_robin_keyword* keyword = (scpi_robin_keyword*)entry;
  char* text = *cursor;
  char* form;

  if (strncmp(keyword->long_form, text,
              scpi_engine_length(keyword->long_form)) == 0) {
    form = keyword->long_form;
  } else if (strncmp(keyword->short_form, text,
                     scpi_engine_length(keyword->short_form)) == 0) {
    form = keyword->short_form;
  } else {
    return SCPI_ROBIN_INVALID;
  }
  text += scpi_engine_length(form);
  *cursor = text;
  if (keyword->code != SCPI_ROBIN_BRANCH) {
    return keyword->code - SCPI_ROBIN_BRANCH;
  }
  if (*text != ':') {
    return SCPI_ROBIN_INVALID;
  }
  *cursor = text + 1;
  return 0;
}

/* Finds the name at a cursor among SCPI_ROBIN_NAMES, the first whose
 * every keyword matches, and steps past it: its last keyword's code and
 * arguments' form, and 0; or STATUS_SCPI_INVALID_STRING (0x2e524). */
uint32_t robin_lookup(char** cursor, uint32_t* code, uint32_t* form) {
  for (const scpi_robin_keyword* const* name = SCPI_ROBIN_NAMES;; name++) {
    const scpi_robin_keyword* keyword = *name;
    char* next = *cursor;

    if (keyword->code == SCPI_ROBIN_END) {
      return SCPI_ROBIN_INVALID;
    }
    while (keyword->code != SCPI_ROBIN_END &&
           robin_keyword_match((char*)keyword, &next) != SCPI_ROBIN_INVALID) {
      *code = keyword->code;
      *form = keyword->form;
      keyword++;
    }
    if (keyword->code == SCPI_ROBIN_END) {
      *cursor = next;
      return 0;
    }
  }
}

/* Reads a keyword's arguments of a form at a cursor, stepping past them:
 * each number, less 1, and 0; or STATUS_SCPI_INVALID_STRING.  A form past
 * the last, 6, answers itself less 6, as the image's
 * switch leaves it (0x2e590). */
int32_t robin_parse_arguments(char** text, uint32_t form, uint32_t* first,
                              uint32_t* second) {
  static const uint32_t LAST_FORM = 6;

  *first = 0;
  *second = 0;
  if (form > LAST_FORM) {
    return form - LAST_FORM;
  }

  const scpi_robin_form* shape = &SCPI_ROBIN_FORMS[form];

  if (**text == 0) {
    if (shape->pair) {
      *second = 1;
    }
    return 0;
  }
  if (**text != ' ') {
    return SCPI_ROBIN_INVALID;
  }
  skip_spaces(text);

  char first_digit = **text;

  if (first_digit < '1' || first_digit > shape->last_first) {
    return SCPI_ROBIN_INVALID;
  }
  (*text)++;
  if (!shape->pair) {
    *first = first_digit - '1';
    return 0;
  }
  skip_spaces(text);
  if (**text != ',') {
    return SCPI_ROBIN_INVALID;
  }
  (*text)++;
  skip_spaces(text);

  char second_digit = **text;
  bool after_one = first_digit == '1';

  if (second_digit < (after_one ? '2' : '1') ||
      second_digit > (after_one ? shape->last_second : '1')) {
    return SCPI_ROBIN_INVALID;
  }
  (*text)++;
  *first = first_digit - '1';
  *second = second_digit - '1';
  return 0;
}

/* Parses a data set's name, upper-cased, trimmed and its data class
 * stripped: its arguments, robin_keyword_code its code, and 0; or
 * STATUS_SCPI_INVALID_STRING.  The name is copied, unbounded, into 100
 * bytes, as the image copies it (0x2e84a). */
int32_t robin_parse(char* name, uint32_t* first, uint32_t* second) {
  char copy[100];
  char* text = copy;
  uint32_t code;
  uint32_t form;

  for (uint32_t index = 0; (copy[index] = name[index]) != 0; index++) {
  }
  string_upper(text);
  skip_spaces(&text);
  string_trim_right(&text);
  robin_strip_xnon(&text);

  /* The image compares each result's low byte. */
  int8_t error = robin_lookup(&text, &code, &form);

  if (error == SCPI_ROBIN_INVALID) {
    return error;
  }
  error = robin_parse_arguments(&text, form, first, second);
  if (error == SCPI_ROBIN_INVALID) {
    return error;
  }
  if (*text != 0) {
    return SCPI_ROBIN_INVALID;
  }
  robin_keyword_code = code;
  return 0;
}

/* Makes an input's points up: SCPI_ROBIN_FIRST_POINTS more than its
 * index, counting up from it; none for another (0x2e8e2). */
void robin_point_list(uint16_t* points, uint16_t* count, char kind) {
  if (kind < 0 || kind > SCPI_ROBIN_LAST_INPUT) {
    *count = 0;
    return;
  }
  for (uint8_t index = 0; index < kind + SCPI_ROBIN_FIRST_POINTS; index++) {
    points[index] = kind + index;
  }
  *count = kind + SCPI_ROBIN_FIRST_POINTS;
}

/* Makes up an input's time stamp: the year, month, day, hours, minutes,
 * seconds, milliseconds, microseconds and nanoseconds of 1994-08-03
 * 09:15:30.700800900, each plus the input and the entry; not the tenth
 * value, the TFOM (0x2e99a).  It leaves the eighth's low byte in D0,
 * which scpi_query_ROBin_TSTamp tests. */
void robin_timestamp_values(int8_t kind, int16_t entry, int16_t* values) {
  static const int16_t TIMESTAMP[] = {1994, 8, 3, 9, 15, 30, 700, 800, 900};

  for (uint8_t index = 0; index < sizeof TIMESTAMP / sizeof *TIMESTAMP;
       index++) {
    values[index] = kind + entry + TIMESTAMP[index];
  }
}

/* Each input's points, as robin_point_list makes them, for an input or
 * for SCPI_ROBIN_ALL_INPUTS; 0 for another (0x2ea1c). */
void robin_point_count(char kind, int16_t* counts) {
  if (kind == SCPI_ROBIN_ALL_INPUTS) {
    for (uint8_t input = 0; input < SCPI_ROBIN_INPUTS; input++) {
      counts[input] = input + SCPI_ROBIN_FIRST_POINTS;
    }
  } else if (kind >= 0 && kind <= SCPI_ROBIN_LAST_INPUT) {
    *counts = kind + SCPI_ROBIN_FIRST_POINTS;
  } else {
    *counts = 0;
  }
}

/* Each input's overflow count, made up as its points are (0x2ea80). */
void robin_overflow_count(char kind, double* counts) {
  if (kind == SCPI_ROBIN_ALL_INPUTS) {
    for (uint8_t input = 0; input < SCPI_ROBIN_INPUTS; input++) {
      counts[input] = integer_to_double(input + SCPI_ROBIN_FIRST_POINTS);
    }
  } else if (kind >= 0 && kind <= SCPI_ROBIN_LAST_INPUT) {
    *counts = integer_to_double(kind + SCPI_ROBIN_FIRST_POINTS);
  } else {
    *counts = 0;
  }
}

/* Clears an input's time stamps; this firmware has none (0x2eb08). */
void robin_clear(uint8_t kind) {}

/* :ROBin?, :SENSe:DATA? "TSTamp 1": an input's points, in the
 * :FORMat:DATA format (0x2eb8a). */
int32_t scpi_query_ROBin(void* parameters, void* response, scpi_call* call) {
  scpi_robin_parameters* given = parameters;
  uint32_t kind;
  uint32_t second;
  int32_t error = robin_parse(given->name, &kind, &second);
  request_source source = return_zero(call->input);
  int16_t* points = robin_points[source];
  uint16_t count;

  if (error != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  robin_point_list((uint16_t*)points, &count, kind);
  robin_store(response, points, count, nv_settings.format_data, source);
  return 0;
}

/* :ROBin:CLEar, an input's time stamps or, with no name, all of them
 * (0x2ec34). */
int32_t scpi_set_ROBin_CLEar(void* parameters, void* response,
                             scpi_call* call) {
  scpi_robin_parameters* given = parameters;
  uint32_t kind;
  uint32_t second;

  if (given->name_kind == SCPI_NONE) {
    robin_clear(SCPI_ROBIN_ALL_INPUTS);
  } else if (robin_parse(given->name, &kind, &second) == 0) {
    robin_clear(kind);
  } else {
    status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE,
                       return_zero(call->input));
  }
  return 0;
}

/* :ROBin:MEMory:OVERflow:COUNt?, an input's overflow count or, with no
 * name, each input's (0x2ec9a). */
int32_t scpi_query_ROBin_MEMory_OVERflow_COUNt(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_robin_parameters* given = parameters;
  scpi_array* array = response;
  request_source source = return_zero(call->input);
  double* counts = robin_overflows[source];
  uint32_t kind;
  uint32_t second;

  if (given->name_kind == SCPI_NONE) {
    array->count = SCPI_ROBIN_INPUTS;
    array->values = counts;
    robin_overflow_count(SCPI_ROBIN_ALL_INPUTS, counts);
    return 0;
  }
  if (robin_parse(given->name, &kind, &second) != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  array->count = 1;
  array->values = counts;
  robin_overflow_count(kind, counts);
  return 0;
}

/* :ROBin:MEMory:SAVE, FIRSt or LAST (0x2ed4e). */
int32_t scpi_set_ROBin_MEMory_SAVE(void* parameters, void* response,
                                   scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_ROBin_MEMory_SAVE, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :ROBin:MEMory:SAVE? (0x2ed7e). */
int32_t scpi_query_ROBin_MEMory_SAVE(void* parameters, void* response,
                                     scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(nv_settings.memory_save);
  return 0;
}

/* :ROBin:POINts?, an input's points or, with no name, each input's
 * (0x2ed98). */
int32_t scpi_query_ROBin_POINts(void* parameters, void* response,
                                scpi_call* call) {
  scpi_robin_parameters* given = parameters;
  scpi_array* array = response;
  request_source source = return_zero(call->input);
  int16_t* counts = scpi_response_lists[source];
  uint32_t kind;
  uint32_t second;

  if (given->name_kind == SCPI_NONE) {
    array->count = SCPI_ROBIN_INPUTS;
    array->values = counts;
    robin_point_count(SCPI_ROBIN_ALL_INPUTS, counts);
    return 0;
  }
  if (robin_parse(given->name, &kind, &second) != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  array->count = 1;
  array->values = counts;
  robin_point_count(kind, counts);
  return 0;
}

/* :ROBin:TSTamp?, an input's entry'th time stamp, its ten values in the
 * :FORMat:DATA format (0x2ee40).  The tenth is what the frame held, which
 * robin_timestamp_values leaves unset and the image copies. */
int32_t scpi_query_ROBin_TSTamp(void* parameters, void* response,
                                scpi_call* call) {
  static const int16_t LAST_ENTRY = 256;
  static const uint8_t TIMESTAMP_VALUES = 10;
  /* The value whose low byte robin_timestamp_values leaves in D0. */
  static const uint8_t TESTED_VALUE = 7;
  scpi_robin_parameters* given = parameters;
  uint32_t kind;
  uint32_t second;
  int32_t error = robin_parse(given->name, &kind, &second);
  request_source source = return_zero(call->input);
  int16_t* points = robin_points[source];
  int16_t values[TIMESTAMP_VALUES];

  if (error != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  if (given->entry < 1 || given->entry > LAST_ENTRY) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  robin_timestamp_values(kind, given->entry, values);
  if ((uint8_t)values[TESTED_VALUE] == 0) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  for (uint8_t index = 0; index < TIMESTAMP_VALUES; index++) {
    points[index] = values[index];
  }
  robin_store(response, points, TIMESTAMP_VALUES, nv_settings.format_data,
              source);
  return 0;
}

/* Makes a :ROBin query's response of its points, by its format, in the
 * source's record of that kind (0x28cfe). */
void robin_store(scpi_array_record* record, int16_t* points, int32_t count,
                 data_format format, request_source source) {
  switch (format) {
    case FORMAT_INTEGER: {
      scpi_block* block = &robin_blocks[source];

      block->length = count * sizeof *points;
      block->data = points;
      record->kind = SCPI_ARRAY_BLOCK;
      record->array = block;
      break;
    }
    case FORMAT_ASCII: {
      scpi_array* array = &robin_arrays[source];

      array->count = count;
      array->values = points;
      record->kind = SCPI_ARRAY_SHORTS;
      record->array = array;
      break;
    }
  }
}
