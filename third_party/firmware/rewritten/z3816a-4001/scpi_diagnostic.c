/* scpi_diagnostic.c's functions that are this image's own: the rest are
 * common/scpi_diagnostic.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the 58503A 3704's and the
 * Z3815A 4010's rewritten/scpi_diagnostic.c: of its functions, those the
 * Z3816A's code carries (identical or relocated by image_diff, or changed with
 * the same instructions), with the static helpers they use, each the same here
 * but where its comment says otherwise (docs/firmware/z3816a-4001.md, "The
 * link check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

typedef struct scpi_diagnostic_indexed_numeric {
  int16_t index;                 /* +0x00 */
  scpi_diagnostic_numeric given; /* +0x02 */
} scpi_diagnostic_indexed_numeric;

/* STATUS_NO_CONDITION when the receiver takes a descriptor's request;
 * else unknown before it has sent its ID, or -241 Hardware missing.  The
 * image asks of the descriptor's request, GPS_SCRIPT_TRAIM, which
 * gps_message_supported knows nothing of, so that the handlers always
 * find it unknown (0x3a40c, 0x3a4e6). */
static status_condition_code scpi_diagnostic_raim_missing(
    gps_request_code request, status_condition_code unknown) {
  uint8_t supported;

  if (gps_message_supported(request, &supported) == 0) {
    return unknown;
  }
  return supported == 0 ? STATUS_HARDWARE_MISSING : STATUS_NO_CONDITION;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe: the receiver's Time
 * RAIM, by pllc (0x3a3f2). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe(void* parameters,
                                                              void* response,
                                                              scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe.gps_request,
      STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, scpi_input_source(call->input));
    return 0;
  }
  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe,
                 given->index, given->given.value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe? (0x3a46a). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* index = parameters;
  int16_t* state = response;
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe.gps_request,
      STATUS_DATA_CORRUPT_OR_STALE);

  if (missing != STATUS_NO_CONDITION) {
    return scpi_query_failed(missing, call);
  }
  *state = (int8_t)nv_settings.raim_state[*index];
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold: Time RAIM's alarm
 * limit, by pllc (0x3a4c6). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold.gps_request,
      STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, scpi_input_source(call->input));
    return 0;
  }
  scpi_numeric_set(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold,
                   given->index, given->given.kind, given->given.bound, 0,
                   given->given.integer, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold?, or its bound
 * (0x3a54a). */
int32_t scpi_query_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  scpi_diagnostic_long* threshold = response;
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold.gps_request,
      STATUS_DATA_CORRUPT_OR_STALE);

  if (missing != STATUS_NO_CONDITION) {
    return scpi_query_failed(missing, call);
  }
  threshold->value = float_to_integer(scpi_numeric_get(
      &scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold,
      given->index, given->given.kind, given->given.bound));
  return 0;
}

/* :DIAGnostic:GPSystem:UTC? (0x3a8a8). */
int32_t scpi_query_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                           scpi_call* call) {
  int16_t* utc = response;

  *utc = (int8_t)nv_settings.gps_utc;
  return 0;
}

/* :DIAGnostic:IDENtification:DEFault?: the default model and hardware
 * suffix (0x3a8ba). */
int32_t scpi_query_DIAGnostic_IDENtification_DEFault(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  struct {
    const char* model;  /* +0x00 */
    const char* suffix; /* +0x04 */
  }* defaults = response;

  defaults->model = "Z3816A";
  /* The suffix is the tag's own string, as the 58503A's (0x428e7). */
  defaults->suffix = RECORDS_TAG;
  return 0;
}

/* :DIAGnostic:LOG:WRITe number and :SLOG's: logs "diag entry number"
 * to the event log; -200 when it is full, -311 when the EEPROM write
 * fails, else -300 for a failure (0x3aec6). */
int32_t scpi_set_DIAGnostic_LOG_WRITe(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  int16_t result = log_write("diag entry %d", given->given.value);

  if (result == 0) {
    return 0;
  }
  if (result == RECORDS_LOG_FULL) {
    status_report_from(STATUS_EXECUTION_ERROR, scpi_input_source(call->input));
  } else if (result == RECORDS_LOG_WRITE_FAILED) {
    status_report_from(STATUS_MEMORY_ERROR, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR,
                       scpi_input_source(call->input));
  }
  return 0;
}

/* :DIAGnostic:ME: an undefined header, where the Z3805A's does nothing
 * (0x3af44). */
int32_t scpi_set_DIAGnostic_ME(void* parameters, void* response,
                               scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:ROSCillator:EFControl:ABSolute?: the EFC DAC's value
 * (0x3b1f0). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ABSolute(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(float_identity(pll.efc));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:RELative?: the EFC in percent of
 * half its range, -100 to +100 from mid-scale (0x3b1a2). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_RELative(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  static const double PERCENT = 100.0;
  scpi_diagnostic_float* efc = response;
  float offset = float_add(float_identity(pll.efc), -PLL_EFC_MID_SCALE);
  float fraction = float_divide(offset, PLL_EFC_MID_SCALE);

  efc->value =
      double_to_float(double_multiply(PERCENT, float_to_double(fraction)));
  return 0;
}

/* :DIAGnostic:SER1:RESTricted? (0x3b15c). */
int32_t scpi_query_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* restricted = response;

  *restricted = return_byte(nv_settings.serial_restricted);
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput? (0x3b360). */
int32_t scpi_query_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.debug_output;
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup? (0x3b3a2). */
int32_t scpi_query_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.pforth_at_startup;
  return 0;
}

/* The self-tests :TEST? names, by its choice's index, ALL to UART
 * (0x5abfc) (the jump tables at 0x3b3e4 and 0x3b496). */
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
 * details (0x3b3c6). */
int32_t scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                   scpi_call* call) {
  /* The caller's D2: the test for a choice past the list, which the
   * parser never gives (0x3b3ce). */
  selftest_test_number test = CPU_CALLERS_REGISTER(int32_t, "d2");
  const struct {
    int16_t kind;    /* +0x00 */
    uint16_t choice; /* +0x02 */
  }* given = parameters;
  scpi_diagnostic_long* results = response;

  if (given->choice < SCPI_DIAGNOSTIC_TESTS) {
    test = SCPI_DIAGNOSTIC_TEST_OF_CHOICE[given->choice];
  }
  pllc_request_b(&scpi_action_self_test, test, scpi_input_source(call->input));
  results[0].value = selftest.result;
  results[1].value = selftest.details[0];
  results[2].value = selftest.details[1];
  return 0;
}

/* :DIAGnostic:TEST:RESult?: the last test's result and, for one :TEST?
 * names, its choice (0x3b47c). */
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

/* :DIAGnostic:GPSystem:ACURrent?: the antenna's current (0x3afb2). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = adc_average(ADC_ANTENNA_CURRENT);
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm: whether the antenna's current
 * alarms, by pllc (0x3af68). */
int32_t scpi_set_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_ACURrent_ALARm, 0,
                 given->value, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm? (0x3af98). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* alarm = response;

  *alarm = return_byte(nv_settings.antenna_current_alarm);
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG: the receiver's tracking log, by
 * pllc (0x3b69a). */
int32_t scpi_set_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_TRACking_LOG, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG? (0x3b6ca). */
int32_t scpi_query_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  int16_t* enabled = response;

  *enabled = return_byte(nv_settings.tracking_log);
  return 0;
}

/* :DIAGnostic:IDENtification:GPSystem?: the receiver's ten ID lines,
 * -230 before it has sent them (0x3a8d4). */
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

/* :DIAGnostic:INPut:DATA?: 0 (0x3b604). */
int32_t scpi_query_DIAGnostic_INPut_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = 0;
  return 0;
}

/* :DIAGnostic:LOG:READ:ALL? and :SLOG's: prints the log to the console,
 * where the Z3805A's prints it only when the query came from it; an
 * empty response (0x3aea0). */
int32_t scpi_query_DIAGnostic_LOG_READ_ALL(void* parameters, void* response,
                                           scpi_call* call) {
  const int16_t* log = parameters;
  scpi_token_string* text = response;

  text->length = 0;
  log_print(*log, console_write, printf);
  return 0;
}

/* :DIAGnostic:ME?: an undefined header, where the Z3805A's answers 0
 * (0x3af56). */
int32_t scpi_query_DIAGnostic_ME(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:REFerence:EXTernal:QUEStionable:HYSTeresis and its query:
 * an undefined header, each through scpi_query_failed (0x3b676,
 * 0x3b688). */
int32_t scpi_set_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis: the GPS
 * reference's questionable hysteresis, or MIN or MAX (0x3b610). */
int32_t scpi_set_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound, 0, given->integer,
      scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis?, or its bound
 * (0x3b64a). */
int32_t scpi_query_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* hysteresis = response;

  hysteresis->value = float_to_unsigned(scpi_numeric_get(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:CURRent?: the oven's current (0x3b18c). */
int32_t scpi_query_DIAGnostic_ROSCillator_CURRent(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = hmon_channel_read(ADC_OVEN_CURRENT);
  return 0;
}

/* :DIAGnostic:STATus:HAPPening n: the nth happening, the condition n
 * past the last error, 0 to STATUS_USER_REPORTED_CLEARED's; -222 for
 * another (0x3b2c4). */
int32_t scpi_set_DIAGnostic_STATus_HAPPening(void* parameters, void* response,
                                             scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  if (given->value < 0 ||
      given->value > STATUS_USER_REPORTED_CLEARED - STATUS_INPUT_OVERRUN) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE,
                       scpi_input_source(call->input));
  } else {
    status_happening(STATUS_INPUT_OVERRUN + given->value);
  }
  return 0;
}

/* :DIAGnostic:TMODe:STATe: test mode, in which the latch's writers write
 * diag_tmode_data (0x3b5ac). */
int32_t scpi_set_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                        scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  diag_tmode_state = given->value != 0;
  return 0;
}

/* :DIAGnostic:TMODe:STATe? (0x3b5ca). */
int32_t scpi_query_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                          scpi_call* call) {
  uint16_t* state = response;

  *state = diag_tmode_state;
  return 0;
}

/* :DIAGnostic:TSET and its query: an undefined header, each through
 * scpi_query_failed (0x3afc8, 0x3afda). */
int32_t scpi_set_DIAGnostic_TSET(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_TSET(void* parameters, void* response,
                                   scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TVALid:NOW: marks the time valid now (0x3b6e4). */
int32_t scpi_set_DIAGnostic_TVALid_NOW(void* parameters, void* response,
                                       scpi_call* call) {
  clock_mark_set();
  return 0;
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
/* :DIAGnostic:TOFFset?, or its bound. */
int32_t scpi_query_DIAGnostic_TOFFset(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* offset = response;

  offset->value = scpi_numeric_get(&scpi_param_DIAGnostic_TOFFset, 0,
                                   given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:TEMPerature?: an undefined header (0x3b3b4). */
int32_t scpi_query_DIAGnostic_TEMPerature(void* parameters, void* response,
                                          scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TMODe:DATA? (0x3b5f2). */
int32_t scpi_query_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = diag_tmode_data;
  return 0;
}

/* :DIAGnostic:TMODe:DATA: the latch's word in test mode, a long's low
 * word, where the 58503A's keeps a byte (0x3b5dc). */
int32_t scpi_set_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  diag_tmode_data = given->integer;
  return 0;
}

/* :DIAGnostic:GPSystem:TIME?: the receiver's local time and date, -230
 * before it has reported them; common/'s but for the fraction
 * gps_time_local_get gives here, which it leaves unread (0x3a7e8). */
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
  uint32_t fraction;
  /* The date as a long, which gps_time_local_get fills by its parts. */
  union {
    uint32_t packed;
    calendar_date parts;
  } date;

  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  bool known = gps_time_local_get(&time, &fraction, &date.packed);

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

/* :DIAGnostic:QUERy:RESPonse?: from a source other than 0, the SCI's,
 * which scpi_input_source always gives, the responses it has echoed
 * since the last command, scpi_echo_text, copied into scpi_echo_response
 * and NUL-terminated (0x3b0d6).  From source 0 the count, the text and
 * the copy are A3's, A4's and A2's as the caller left them
 * (docs/firmware/quirks.md).  The 58503A 3704's but for the test's
 * sense (0x3b0ec): the 58503A's copies for source 0 alone. */
int32_t scpi_query_DIAGnostic_QUERy_RESPonse(void* parameters, void* response,
                                             scpi_call* call) {
  char* copy = CPU_CALLERS_REGISTER(char*, "a2");
  uint8_t* length = CPU_CALLERS_REGISTER(uint8_t*, "a3");
  char* echo = CPU_CALLERS_REGISTER(char*, "a4");
  char** text = response;
  uint8_t index;

  if ((int8_t)scpi_input_source(call->input) != REQUEST_SOURCE_SCPI) {
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

/* :DIAGnostic:GPSystem:UTC, by pllc. */
int32_t scpi_set_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}
