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

typedef struct scpi_diagnostic_indexed_numeric {
  int16_t index;                 /* +0x00 */
  scpi_diagnostic_numeric given; /* +0x02 */
} scpi_diagnostic_indexed_numeric;

/* The :LOG and :SLOG subtrees' path word: the event log, or the
 * reset-cause log at 0x4001c0. */
enum {
  SCPI_DIAGNOSTIC_EVENT_LOG = 0,
  SCPI_DIAGNOSTIC_RESET_LOG = 1,
};

/* log.c's results that the :LOG setters tell apart (its LOG_FULL,
 * LOG_PAGE_WRITE_FAILED's low byte, which log_erase has already
 * reported, and log_erase_checked's count that differs). */
static const int16_t SCPI_DIAGNOSTIC_LOG_FULL = 0xfc;

static const int16_t SCPI_DIAGNOSTIC_LOG_WRITE_FAILED = 0xfe;

static const int16_t SCPI_DIAGNOSTIC_LOG_COUNT_DIFFERS = 0xff;

/* STATUS_NO_CONDITION when the receiver takes @@Bn, Time RAIM; else
 * unknown before it has sent its ID, or -241 Hardware missing. */
static status_condition_code scpi_diagnostic_raim_missing(
    status_condition_code unknown) {
  uint8_t supported;

  if (gps_message_supported(GPS_REQUEST_TRAIM, &supported) == 0) {
    return unknown;
  }
  return supported == 0 ? STATUS_HARDWARE_MISSING : STATUS_NO_CONDITION;
}

/* :DIAGnostic:GPSystem:POSition:DEGRees? LATitude|LONGitude, mas: the
 * angle as hemisphere, degrees, minutes and seconds (0x2b02c). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_DEGRees(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  /* The axis's choice (0x53bac) and the hemisphere's (0x53cb6). */
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
 * -222 for a negative part (0x2afa2). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_MSEConds(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  /* The hemisphere's choice (0x53cb6). */
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

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe: the receiver's Time
 * RAIM, by pllc (0x2acb8). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe(void* parameters,
                                                              void* response,
                                                              scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  status_condition_code missing =
      scpi_diagnostic_raim_missing(STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, return_zero(call->input));
    return 0;
  }
  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe,
                 given->index, given->given.value, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe? (0x2ad30). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* index = parameters;
  int16_t* state = response;
  status_condition_code missing =
      scpi_diagnostic_raim_missing(STATUS_DATA_CORRUPT_OR_STALE);

  if (missing != STATUS_NO_CONDITION) {
    return scpi_query_failed(missing, call);
  }
  *state = (int8_t)nv_settings.raim_state[*index];
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold: Time RAIM's alarm
 * limit, by pllc (0x2ad8c). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  status_condition_code missing =
      scpi_diagnostic_raim_missing(STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, return_zero(call->input));
    return 0;
  }
  scpi_numeric_set(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold,
                   given->index, given->given.kind, given->given.bound, 0,
                   given->given.integer, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold?, or its bound
 * (0x2ae10). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  scpi_diagnostic_long* threshold = response;
  status_condition_code missing =
      scpi_diagnostic_raim_missing(STATUS_DATA_CORRUPT_OR_STALE);

  if (missing != STATUS_NO_CONDITION) {
    return scpi_query_failed(missing, call);
  }
  threshold->value = float_to_integer(scpi_numeric_get(
      &scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold,
      given->index, given->given.kind, given->given.bound));
  return 0;
}

/* :DIAGnostic:GPSystem:TIME?: the receiver's local time and date, -230
 * before it has reported them (0x2b0ae). */
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

/* :DIAGnostic:GPSystem:UTC, by pllc (0x2b13a). */
int32_t scpi_set_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:UTC? (0x2b16a). */
int32_t scpi_query_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                           scpi_call* call) {
  int16_t* utc = response;

  *utc = (int8_t)nv_settings.gps_utc;
  return 0;
}

/* :DIAGnostic:IDENtification:DEFault?: the default model and hardware
 * suffix (0x2b17c). */
int32_t scpi_query_DIAGnostic_IDENtification_DEFault(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  struct {
    const char* model;  /* +0x00 */
    const char* suffix; /* +0x04 */
  }* defaults = response;

  defaults->model = "Z3805A";
  defaults->suffix = "S";
  return 0;
}

/* :DIAGnostic:IDENtification:GPSystem?: the receiver's ten ID lines,
 * -230 before it has sent them (0x2b196). */
int32_t scpi_query_DIAGnostic_IDENtification_GPSystem(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  const char** lines = response;
  oncore_receiver_id* id = &gps.receiver_id;

  if (gps.receiver_id_received == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  lines[0] = id->copyright;
  lines[1] = id->software_part_number;
  lines[2] = id->software_version;
  lines[3] = id->software_revision;
  lines[4] = id->software_date;
  lines[5] = id->model_number;
  lines[6] = id->hardware_part_number;
  lines[7] = id->serial_number;
  lines[8] = id->manufacture_date;
  lines[9] = id->options;
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
 * or none; -223 for more (0x2b20e). */
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
 * -223 for more (0x2b27e). */
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
 * characters; -223 for more (0x2b31a). */
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

/* :DIAGnostic:LIFetime:COUNt?: the days the receiver has run
 * (0x2b3b6). */
int32_t scpi_query_DIAGnostic_LIFetime_COUNt(void* parameters, void* response,
                                             scpi_call* call) {
  scpi_diagnostic_long* count = response;

  count->value = lifetime.count;
  return 0;
}

/* :DIAGnostic:LOG:CLEar [count]: empties the event log, given its count
 * if one is given; -222 for a count not the log's, -300 for another
 * failure (0x2b3c4). */
int32_t scpi_set_DIAGnostic_LOG_CLEar(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  int16_t result;

  if (given->given.kind == SCPI_NONE) {
    result = log_erase_locked(return_zero(call->input));
  } else {
    result = log_erase_checked(given->given.value, return_zero(call->input));
  }
  if (result == 0 || result == SCPI_DIAGNOSTIC_LOG_WRITE_FAILED) {
    return 0;
  }
  status_report_from(result == SCPI_DIAGNOSTIC_LOG_COUNT_DIFFERS
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

/* :DIAGnostic:LOG:COUNt? and :SLOG's: the log's entries (0x2b47c). */
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

/* :DIAGnostic:LOG:READ? [number] and :SLOG's: an entry, the newest when
 * none is given, as its heading, ": " and its text, in the source's
 * text buffer; -222 for a number outside the log (0x2b632). */
int32_t scpi_query_DIAGnostic_LOG_READ(void* parameters, void* response,
                                       scpi_call* call) {
  /* log_read's and log_entry_text's text and heading. */
  enum { TEXT_SIZE = 255, HEADING_SIZE = 27 };
  const scpi_diagnostic_indexed_word* given = parameters;
  char** entry = response;
  char* line = scpi_response_texts[(uint8_t)return_zero(call->input)];
  char text[TEXT_SIZE];
  char heading[HEADING_SIZE];

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

/* :DIAGnostic:LOG:READ:ALL? and :SLOG's: prints the log to the console,
 * when the query came from it; an empty response (0x2b762). */
int32_t scpi_query_DIAGnostic_LOG_READ_ALL(void* parameters, void* response,
                                           scpi_call* call) {
  const int16_t* log = parameters;
  scpi_token_string* text = response;

  text->length = 0;
  if (scpi_console_port() == call->input) {
    log_print(*log, console_write, printf);
  }
  return 0;
}

/* :DIAGnostic:LOG:WRITe number and :SLOG's: logs "diag entry number"
 * to the event log; -200 when it is full, -311 when the EEPROM write
 * fails, else -300 for a failure (0x2b798). */
int32_t scpi_set_DIAGnostic_LOG_WRITe(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  int16_t result = log_write("diag entry %d", given->given.value);

  if (result == 0) {
    return 0;
  }
  if (result == SCPI_DIAGNOSTIC_LOG_FULL) {
    status_report_from(STATUS_EXECUTION_ERROR, return_zero(call->input));
  } else if (result == SCPI_DIAGNOSTIC_LOG_WRITE_FAILED) {
    status_report_from(STATUS_MEMORY_ERROR, return_zero(call->input));
  } else {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero(call->input));
  }
  return 0;
}

/* :DIAGnostic:ME: nothing (0x2b816). */
int32_t scpi_set_DIAGnostic_ME(void* parameters, void* response,
                               scpi_call* call) {
  return 0;
}

/* :DIAGnostic:ME?: 0 (0x2b81a). */
int32_t scpi_query_DIAGnostic_ME(void* parameters, void* response,
                                 scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(0);
  return 0;
}

/* :DIAGnostic:OS:MEMory?: prints the kernel's memory report; an empty
 * response (0x2b830). */
int32_t scpi_query_DIAGnostic_OS_MEMory(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  os_memory_report(return_zero(call->input));
  return 0;
}

/* :DIAGnostic:OS:PROCess?: prints the kernel's tasks; an empty response
 * (0x2b852). */
int32_t scpi_query_DIAGnostic_OS_PROCess(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  os_process_report(return_zero(call->input));
  return 0;
}

/* :DIAGnostic:PTIMe:TINTerval?: a time-interval reading, through pllc,
 * and its tag; -230 when it is not valid (0x2b896). */
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

/* :DIAGnostic:ROSCillator:CURRent?: the oscillator's current
 * (0x2b9fc). */
int32_t scpi_query_DIAGnostic_ROSCillator_CURRent(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = read_adc_osc_curr();
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:ABSolute?: the EFC DAC's value
 * (0x2ba5a). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ABSolute(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(float_identity(pll.efc));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:RELative?: the EFC in percent of
 * half its range, -100 to +100 from mid-scale (0x2ba0c). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_RELative(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  static const float EFC_MID_SCALE = 524288.0f;
  static const double PERCENT = 100.0;
  scpi_diagnostic_float* efc = response;
  float offset = float_add(float_identity(pll.efc), -EFC_MID_SCALE);
  float fraction = float_divide(offset, EFC_MID_SCALE);

  efc->value =
      double_to_float(double_multiply(PERCENT, float_to_double(fraction)));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient, by pllc (0x2ba78). */
int32_t scpi_set_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   return_zero(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient?, or its bound (0x2bab2). */
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

/* :DIAGnostic:QUERy:RESPonse?: from the console port, the responses it
 * has echoed since the last command, scpi_echo_text, copied into
 * scpi_echo_response and NUL-terminated (0x2b91a).  From another port
 * the count, the text and the copy are A3's, A4's and A2's as the
 * caller left them (docs/firmware/quirks.md). */
int32_t scpi_query_DIAGnostic_QUERy_RESPonse(void* parameters, void* response,
                                             scpi_call* call) {
  char* copy = CPU_CALLERS_REGISTER(char*, "a2");
  uint8_t* length = CPU_CALLERS_REGISTER(uint8_t*, "a3");
  char* echo = CPU_CALLERS_REGISTER(char*, "a4");
  char** text = response;
  uint8_t index;

  if (scpi_console_port() == call->input) {
    length = &scpi_echo_length;
    echo = scpi_echo_text;
    copy = scpi_echo_response;
  }
  for (index = 0; index < *length; index++) {
    copy[index] = echo[index];
  }
  copy[index] = 0;
  *text = copy;
  return 0;
}

/* :DIAGnostic:SER1:EGResponse: undefined, -113 (0x2b9e6). */
int32_t scpi_set_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                            scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :DIAGnostic:SER1:EGResponse?: undefined, -113 (0x2b9ea). */
int32_t scpi_query_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                              scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:SER1:RESTricted, by pllc (0x2b99c). */
int32_t scpi_set_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                            scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_SER1_RESTricted, 0, given->given.value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:SER1:RESTricted? (0x2b9cc). */
int32_t scpi_query_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* restricted = response;

  *restricted = return_byte(nv_settings.serial_restricted);
  return 0;
}

/* :DIAGnostic:SLOG:CLEar: empties the reset-cause log; -311 if it fails
 * (0x2b44e). */
int32_t scpi_set_DIAGnostic_SLOG_CLEar(void* parameters, void* response,
                                       scpi_call* call) {
  if (log_clear() != 0) {
    status_report_from(STATUS_MEMORY_ERROR, return_zero(call->input));
  }
  return 0;
}

/* :DIAGnostic:STATus:ERRor code: reports an error condition, 1 to
 * STATUS_INPUT_OVERRUN; -222 for another (0x2bad8). */
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

/* :DIAGnostic:STATus:HAPPening n: the nth happening, the condition n
 * past the last error, 0 to STATUS_USER_REPORTED_CLEARED's; -222 for
 * another (0x2bb2e). */
int32_t scpi_set_DIAGnostic_STATus_HAPPening(void* parameters, void* response,
                                             scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  if (given->value < 0 ||
      given->value > STATUS_USER_REPORTED_CLEARED - STATUS_INPUT_OVERRUN) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE, return_zero(call->input));
  } else {
    status_happening(STATUS_INPUT_OVERRUN + given->value);
  }
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput: the kernel's debug output, at once and by
 * pllc (0x2bb76). */
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

/* :DIAGnostic:SYSTem:DOUTput? (0x2bbca). */
int32_t scpi_query_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.debug_output;
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup: pForth at start-up, by pllc (0x2bbdc). */
int32_t scpi_set_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                            scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_SYSTem_PSTartup, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup? (0x2bc0c). */
int32_t scpi_query_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.pforth_at_startup;
  return 0;
}

/* :DIAGnostic:TEMPerature?: the board's temperature (0x2bc1e). */
int32_t scpi_query_DIAGnostic_TEMPerature(void* parameters, void* response,
                                          scpi_call* call) {
  scpi_diagnostic_float* temperature = response;

  temperature->value = read_adc_temp();
  return 0;
}

/* The self-tests :TEST? names, by its choice's index, ALL to UART
 * (0x53b60) (the jump tables at 0x2bc4c and 0x2bcfe). */
#define SCPI_DIAGNOSTIC_TESTS 14

static const selftest_test_number
    SCPI_DIAGNOSTIC_TEST_OF_CHOICE[SCPI_DIAGNOSTIC_TESTS] = {
        SELFTEST_TEST_ALL,
        SELFTEST_TEST_DISPLAY,
        SELFTEST_TEST_EEPROM,
        SELFTEST_TEST_FPGA,
        SELFTEST_TEST_GPS,
        SELFTEST_TEST_INTERPOLATOR_CALIBRATION, /* IMRC */
        SELFTEST_TEST_INTERPOLATOR,             /* INT */
        SELFTEST_TEST_INTERNAL_REFERENCE,       /* IREF */
        SELFTEST_TEST_POWER,
        SELFTEST_TEST_PROCESSOR,
        SELFTEST_TEST_INTERPOLATOR_SAMPLE, /* QMRC */
        SELFTEST_TEST_QSPI,
        SELFTEST_TEST_RAM,
        SELFTEST_TEST_UART,
};

/* :DIAGnostic:TEST? test: runs it through pllc; its result and two
 * details (0x2bc2e). */
int32_t scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                   scpi_call* call) {
  /* The caller's D2: the test for a choice past the list, which the
   * parser never gives (0x2bc36). */
  selftest_test_number test = CPU_CALLERS_REGISTER(int32_t, "d2");
  const struct {
    int16_t kind;    /* +0x00 */
    uint16_t choice; /* +0x02 */
  }* given = parameters;
  scpi_diagnostic_long* results = response;

  if (given->choice < SCPI_DIAGNOSTIC_TESTS) {
    test = SCPI_DIAGNOSTIC_TEST_OF_CHOICE[given->choice];
  }
  pllc_request_b(&scpi_action_self_test, test, return_zero(call->input));
  results[0].value = selftest.result;
  results[1].value = selftest.details[0];
  results[2].value = selftest.details[1];
  return 0;
}

/* :DIAGnostic:TEST:RESult?: the last test's result and, for one :TEST?
 * names, its choice (0x2bce4). */
int32_t scpi_query_DIAGnostic_TEST_RESult(void* parameters, void* response,
                                          scpi_call* call) {
  struct {
    scpi_diagnostic_long result; /* +0x00 */
    int16_t choice;              /* +0x06 */
  }* last = response;

  last->result.value = selftest.result;
  for (int16_t choice = 0; choice < SCPI_DIAGNOSTIC_TESTS; choice++) {
    if (SCPI_DIAGNOSTIC_TEST_OF_CHOICE[choice] == selftest.test) {
      last->choice = choice;
      break;
    }
  }
  return 0;
}

/* :DIAGnostic:TOFFset: a temporary hold, then the offset by pllc
 * (0x2bd96). */
int32_t scpi_set_DIAGnostic_TOFFset(void* parameters, void* response,
                                    scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  pllc_request_b(&scpi_action_temporary_hold, 0, return_zero(call->input));
  scpi_numeric_set(&scpi_param_DIAGnostic_TOFFset, 0, given->kind, given->bound,
                   given->real_bits, 0, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:TOFFset?, or its bound (0x2bdee). */
int32_t scpi_query_DIAGnostic_TOFFset(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* offset = response;

  offset->value = scpi_numeric_get(&scpi_param_DIAGnostic_TOFFset, 0,
                                   given->kind, given->bound);
  return 0;
}
