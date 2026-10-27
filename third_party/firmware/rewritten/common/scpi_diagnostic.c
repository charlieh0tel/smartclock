/* The SCPI :DIAGnostic tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/diagnostic`).
 * 097-z3801-01, table 4-2, documents :IDENtification:GPSystem?,
 * :LIFetime:COUNt?, :LOG, :ROSCillator:EFControl and :QUERy:RESPonse?;
 * the rest are undocumented.  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype. */

/* A response's long, scpi_format_long's and _unsigned_long's 6 bytes. */
typedef struct scpi_diagnostic_long {
  int32_t value;   /* +0x00 */
  uint16_t unused; /* +0x04 */
} scpi_diagnostic_long;

/* A response's float, scpi_format_float's 6 bytes. */
typedef struct scpi_diagnostic_float {
  float value;     /* +0x00 */
  uint16_t unused; /* +0x04 */
} scpi_diagnostic_float;

/* A parameter of a word, a boolean's or a short's: its kind, the
 * value. */
typedef struct scpi_diagnostic_word {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
} scpi_diagnostic_word;

/* A string parameter: its kind, then as scpi_parse_string gives it. */
typedef struct scpi_diagnostic_string {
  int16_t kind;                   /* +0x00 */
  scpi_engine_string_value value; /* +0x02 */
} scpi_diagnostic_string;

/* A numeric setting's parameter as scpi_numeric_set and _get take it:
 * its kind, then a float's bits, a long or a bound, MAXimum or
 * MINimum. */
typedef struct scpi_diagnostic_numeric {
  int16_t kind; /* +0x00 */
  union {
    uint32_t real_bits; /* +0x02 */
    int32_t integer;    /* +0x02 */
    int16_t bound;      /* +0x02 */
  };
} scpi_diagnostic_numeric;

/* A parameter after one path word: the word, a numeric suffix's or a
 * subtree's, then the parameter. */
typedef struct scpi_diagnostic_indexed_word {
  int16_t index;              /* +0x00 */
  scpi_diagnostic_word given; /* +0x02 */
} scpi_diagnostic_indexed_word;

/* The :LOG and :SLOG subtrees' path word: the event log, or the
 * reset-cause log at 0x4001c0. */
enum {
  SCPI_DIAGNOSTIC_EVENT_LOG = 0,
  SCPI_DIAGNOSTIC_RESET_LOG = 1,
};

/* :DIAGnostic:GPSystem:POSition:DEGRees? LATitude|LONGitude, mas: the
 * angle as hemisphere, degrees, minutes and seconds. */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_DEGRees(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  /* The axis's choice
   * (scpi_choices_query_DIAGnostic_GPSystem_POSition_DEGRees_1) and the
   * hemisphere's
   * (scpi_choices_response_DIAGnostic_GPSystem_POSition_DEGRees_1). */
  enum { LATITUDE = 0 };
  enum { NORTH = 0, SOUTH = 1, EAST = 2, WEST = 3 };
  const struct {
    int16_t axis_kind; /* +0x00 */
    int16_t axis;      /* +0x02 */
    int16_t mas_kind;  /* +0x04 */
    int32_t mas;       /* +0x06 */
  }* given = parameters;
  struct {
    int16_t hemisphere;            /* +0x00 */
    scpi_short_item degrees;       /* +0x02 */
    scpi_short_item minutes;       /* +0x06 */
    scpi_diagnostic_float seconds; /* +0x0a */
  }* angle = response;
  uint16_t degrees;
  uint8_t minutes;
  double seconds;

  if (given->mas < 0) {
    angle->hemisphere = given->axis == LATITUDE ? SOUTH : WEST;
  } else {
    angle->hemisphere = given->axis == LATITUDE ? NORTH : EAST;
  }
  mas_to_dms(labs(given->mas), &degrees, &minutes, &seconds);
  angle->degrees.value = degrees;
  angle->minutes.value = minutes;
  angle->seconds.value = double_to_float(seconds);
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:MSEConds? hemisphere, degrees, minutes,
 * seconds: the angle in milliarcseconds, negative south and west;
 * -222 for a negative part. */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_MSEConds(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  /* The hemisphere's choice
   * (scpi_choices_response_DIAGnostic_GPSystem_POSition_DEGRees_1). */
  enum { SOUTH = 1, WEST = 3 };
  const struct {
    int16_t hemisphere_kind; /* +0x00 */
    int16_t hemisphere;      /* +0x02 */
    int16_t degrees_kind;    /* +0x04 */
    int16_t degrees;         /* +0x06 */
    int16_t degrees_unit;    /* +0x08 */
    int16_t minutes_kind;    /* +0x0a */
    int16_t minutes;         /* +0x0c */
    int16_t minutes_unit;    /* +0x0e */
    int16_t seconds_kind;    /* +0x10 */
    double seconds;          /* +0x12 */
  }* given = parameters;
  scpi_diagnostic_long* angle = response;
  int32_t mas;

  /* double_compare(0, x) is negative when x < 0. */
  if (given->degrees < 0 || given->minutes < 0 ||
      double_compare(0.0, given->seconds) < 0) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  dms_to_mas(&mas, given->degrees, given->minutes, given->seconds);
  if (given->hemisphere == SOUTH || given->hemisphere == WEST) {
    mas = -mas;
  }
  angle->value = mas;
  return 0;
}

/* :DIAGnostic:GPSystem:TIME?: the receiver's local time and date, -230
 * before it has reported them. */
int32_t scpi_query_DIAGnostic_GPSystem_TIME(void* parameters, void* response,
                                            scpi_call* call) {
  struct {
    scpi_short_item hour;   /* +0x00 */
    scpi_short_item minute; /* +0x04 */
    scpi_short_item second; /* +0x08 */
    scpi_short_item year;   /* +0x0c */
    scpi_short_item month;  /* +0x10 */
    scpi_short_item day;    /* +0x14 */
  }* now = response;
  time_of_day time;
  /* The date as a long, which gps_time_local_get fills by its parts. */
  union {
    uint32_t packed;
    calendar_date parts;
  } date;

  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  bool known = gps_time_local_get(&time, &date.packed);

  set_interrupt_mask(sr);
  if (!known) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  now->hour.value = time.hour;
  now->minute.value = time.minute;
  now->second.value = time.second;
  now->year.value = date.parts.year;
  now->month.value = date.parts.month;
  now->day.value = date.parts.day;
  return 0;
}

/* :DIAGnostic:GPSystem:UTC, by pllc. */
int32_t scpi_set_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* Writes the identification record, reporting a memory error if the
 * write fails. */
static void scpi_diagnostic_identification_write(scpi_port* input) {
  if (identification_record_write() == 0) {
    status_report_from(STATUS_MEMORY_ERROR, return_zero(input));
  }
}

/* Copies a text that fits, with its NUL. */
static void scpi_diagnostic_copy(char* destination, const char* text) {
  uint8_t index;

  for (index = 0; text[index] != 0; index++) {
    destination[index] = text[index];
  }
  destination[index] = 0;
}

/* :DIAGnostic:IDENtification:HARDware: the hardware suffix, a character
 * or none; -223 for more. */
int32_t scpi_set_DIAGnostic_IDENtification_HARDware(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  static const int32_t SUFFIX_LONGEST = 1;
  const scpi_diagnostic_string* given = parameters;

  if (scpi_engine_length(given->value.text) > SUFFIX_LONGEST) {
    status_report_from(STATUS_TOO_MUCH_DATA, return_zero(call->input));
    return 0;
  }
  idn_suffix = given->value.text[0];
  scpi_diagnostic_identification_write(call->input);
  return 0;
}

/* :DIAGnostic:IDENtification:MODel: the model, up to 17 characters;
 * -223 for more. */
int32_t scpi_set_DIAGnostic_IDENtification_MODel(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const scpi_diagnostic_string* given = parameters;

  if (scpi_engine_length(given->value.text) >= (int32_t)sizeof idn_model) {
    status_report_from(STATUS_TOO_MUCH_DATA, return_zero(call->input));
    return 0;
  }
  scpi_diagnostic_copy(idn_model, given->value.text);
  scpi_diagnostic_identification_write(call->input);
  return 0;
}

/* :DIAGnostic:IDENtification:SERial: the serial number, up to 17
 * characters; -223 for more. */
int32_t scpi_set_DIAGnostic_IDENtification_SERial(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_string* given = parameters;

  if (scpi_engine_length(given->value.text) >= (int32_t)sizeof idn_serial) {
    status_report_from(STATUS_TOO_MUCH_DATA, return_zero(call->input));
    return 0;
  }
  scpi_diagnostic_copy(idn_serial, given->value.text);
  scpi_diagnostic_identification_write(call->input);
  return 0;
}

/* :DIAGnostic:LIFetime:COUNt?: the days the receiver has run. */
int32_t scpi_query_DIAGnostic_LIFetime_COUNt(void* parameters, void* response,
                                             scpi_call* call) {
  scpi_diagnostic_long* count = response;

  count->value = lifetime.count;
  return 0;
}

/* :DIAGnostic:LOG:CLEar [count]: empties the event log, given its count
 * if one is given; -222 for a count not the log's, -300 for another
 * failure. */
int32_t scpi_set_DIAGnostic_LOG_CLEar(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  int16_t result;

  if (given->given.kind == SCPI_NONE) {
    result = log_erase_locked(return_zero(call->input));
  } else {
    result = log_erase_checked(given->given.value, return_zero(call->input));
  }
  if (result == 0 || result == RECORDS_LOG_WRITE_FAILED) {
    return 0;
  }
  status_report_from(result == RECORDS_LOG_COUNT_DIFFERS
                         ? STATUS_DATA_OUT_OF_RANGE
                         : STATUS_DEVICE_SPECIFIC_ERROR,
                     return_zero(call->input));
  return 0;
}

/* A log's entries, the event log's or the reset-cause log's. */
static int16_t scpi_diagnostic_log_entries(int16_t log) {
  if (log == SCPI_DIAGNOSTIC_EVENT_LOG) {
    return log_entry_total(0);
  }
  return log_count(0);
}

/* :DIAGnostic:LOG:COUNt? and :SLOG's: the log's entries. */
int32_t scpi_query_DIAGnostic_LOG_COUNt(void* parameters, void* response,
                                        scpi_call* call) {
  const int16_t* log = parameters;
  scpi_short_item* count = response;
  int16_t entries = scpi_diagnostic_log_entries(*log);

  if (entries < 0) {
    return scpi_query_failed(STATUS_DEVICE_SPECIFIC_ERROR, call);
  }
  count->value = entries;
  return 0;
}

/* log_read's and log_entry_text's text and heading: array sizes, so
 * #define. */
#define SCPI_DIAGNOSTIC_LOG_TEXT_SIZE 255

#define SCPI_DIAGNOSTIC_LOG_HEADING_SIZE 27

/* :DIAGnostic:LOG:READ? [number] and :SLOG's: an entry, the newest when
 * none is given, as its heading, ": " and its text, in the source's
 * text buffer; -222 for a number outside the log. */
int32_t scpi_query_DIAGnostic_LOG_READ(void* parameters, void* response,
                                       scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  char** entry = response;
  char* line = scpi_response_texts[(uint8_t)return_zero(call->input)];
  char text[SCPI_DIAGNOSTIC_LOG_TEXT_SIZE];
  char heading[SCPI_DIAGNOSTIC_LOG_HEADING_SIZE];

  text[0] = 0;
  heading[0] = 0;
  line[0] = 0;
  int16_t number = scpi_diagnostic_log_entries(given->index);

  if (number > 0) {
    if (given->given.kind != SCPI_NONE) {
      if (given->given.value < 1 || given->given.value > number) {
        return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
      }
      number = given->given.value;
    }
    int16_t error = given->index == SCPI_DIAGNOSTIC_EVENT_LOG
                        ? log_read(number, text, heading)
                        : log_entry_text(number, text, heading);

    if (error == 0) {
      strcat(line, heading);
      strcat(line, ": ");
      strcat(line, text);
      *entry = line;
      return 0;
    }
  }
  return scpi_query_failed(STATUS_DEVICE_SPECIFIC_ERROR, call);
}

/* :DIAGnostic:OS:MEMory?: prints the kernel's memory report; an empty
 * response. */
int32_t scpi_query_DIAGnostic_OS_MEMory(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  os_memory_report(return_zero(call->input));
  return 0;
}

/* :DIAGnostic:OS:PROCess?: prints the kernel's tasks; an empty response. */
int32_t scpi_query_DIAGnostic_OS_PROCess(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  os_process_report(return_zero(call->input));
  return 0;
}

/* :DIAGnostic:OS:STACk?: prints the tasks' stacks; an empty response. */
int32_t scpi_query_DIAGnostic_OS_STACk(void* parameters, void* response,
                                       scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  task_stack_report(return_zero(call->input));
  return 0;
}

/* :DIAGnostic:PTIMe:TINTerval?: a time-interval reading, through pllc,
 * and its tag; -230 when it is not valid. */
int32_t scpi_query_DIAGnostic_PTIMe_TINTerval(void* parameters, void* response,
                                              scpi_call* call) {
  scpi_float_resolution* interval = response;

  pllc_request_b(&scpi_action_DIAGnostic_PTIMe_TINTerval, 0,
                 return_zero(call->input));
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (interval_reading.valid == 0) {
    set_interrupt_mask(sr);
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  interval->value = double_to_float(interval_reading.value);
  interval->resolution = interval_reading.tag;
  set_interrupt_mask(sr);
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient, by pllc. */
int32_t scpi_set_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   return_zero(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient?, or its bound. */
int32_t scpi_query_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* coefficient = response;

  coefficient->value =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                       given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:SER1:EGResponse: undefined, -113. */
int32_t scpi_set_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                            scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :DIAGnostic:SER1:EGResponse?: undefined, -113. */
int32_t scpi_query_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                              scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:SER1:RESTricted, by pllc. */
int32_t scpi_set_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                            scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_SER1_RESTricted, 0, given->given.value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:SLOG:CLEar: empties the reset-cause log; -311 if it fails. */
int32_t scpi_set_DIAGnostic_SLOG_CLEar(void* parameters, void* response,
                                       scpi_call* call) {
  if (log_clear() != 0) {
    status_report_from(STATUS_MEMORY_ERROR, return_zero(call->input));
  }
  return 0;
}

/* :DIAGnostic:STATus:ERRor code: reports an error condition, 1 to
 * STATUS_INPUT_OVERRUN; -222 for another. */
int32_t scpi_set_DIAGnostic_STATus_ERRor(void* parameters, void* response,
                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  if (given->value < STATUS_PARAMETER_NOT_ALLOWED ||
      given->value > STATUS_INPUT_OVERRUN) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE, return_zero(call->input));
  } else {
    status_report_from(given->value, return_zero(call->input));
  }
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput: the kernel's debug output, at once and by
 * pllc. */
int32_t scpi_set_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                           scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  debug_enabled = given->value;
  pllc_request_a(&scpi_param_DIAGnostic_SYSTem_DOUTput, 0, given->value,
                 return_zero(call->input));
  psos_debug_enabled = debug_enabled;
  psos_debug_output(debug_enabled);
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup: pForth at start-up, by pllc. */
int32_t scpi_set_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                            scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_SYSTem_PSTartup, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:TOFFset: a temporary hold, then the offset by pllc. */
int32_t scpi_set_DIAGnostic_TOFFset(void* parameters, void* response,
                                    scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  pllc_request_b(&scpi_action_temporary_hold, 0, return_zero(call->input));
  scpi_numeric_set(&scpi_param_DIAGnostic_TOFFset, 0, given->kind, given->bound,
                   given->real_bits, 0, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:TOFFset?, or its bound. */
int32_t scpi_query_DIAGnostic_TOFFset(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* offset = response;

  offset->value = scpi_numeric_get(&scpi_param_DIAGnostic_TOFFset, 0,
                                   given->kind, given->bound);
  return 0;
}
