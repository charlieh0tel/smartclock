/* scpi_diagnostic.c's functions that are this image's own: the rest are
 * common/scpi_diagnostic.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

typedef struct scpi_diagnostic_indexed_numeric {
  int16_t index;                 /* +0x00 */
  scpi_diagnostic_numeric given; /* +0x02 */
} scpi_diagnostic_indexed_numeric;

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

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe: the receiver's Time
 * RAIM, by pllc (0x2a790). */
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

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe? (0x2a808). */
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
 * limit, by pllc (0x2a864). */
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
 * (0x2a8e8). */
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

/* :DIAGnostic:GPSystem:UTC? (0x2ac42). */
int32_t scpi_query_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                           scpi_call* call) {
  int16_t* utc = response;

  *utc = (int8_t)nv_settings.gps_utc;
  return 0;
}

/* :DIAGnostic:IDENtification:DEFault?: the default model and hardware
 * suffix (0x2ac54). */
int32_t scpi_query_DIAGnostic_IDENtification_DEFault(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  struct {
    const char* model;  /* +0x00 */
    const char* suffix; /* +0x04 */
  }* defaults = response;

  defaults->model = "58503A";
  /* The 58503A's suffix is the tag's own string (0x33001). */
  defaults->suffix = RECORDS_TAG;
  return 0;
}

/* :DIAGnostic:LOG:WRITe number and :SLOG's: logs "diag entry number"
 * to the event log; -200 when it is full, -311 when the EEPROM write
 * fails, else -300 for a failure (0x2b260). */
int32_t scpi_set_DIAGnostic_LOG_WRITe(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  int16_t result = log_write("diag entry %d", given->given.value);

  if (result == 0) {
    return 0;
  }
  if (result == RECORDS_LOG_FULL) {
    status_report_from(STATUS_EXECUTION_ERROR, return_zero(call->input));
  } else if (result == RECORDS_LOG_WRITE_FAILED) {
    status_report_from(STATUS_MEMORY_ERROR, return_zero(call->input));
  } else {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero(call->input));
  }
  return 0;
}

/* :DIAGnostic:ME: an undefined header, where the Z3805A's does nothing
 * (0x2b2de). */
int32_t scpi_set_DIAGnostic_ME(void* parameters, void* response,
                               scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:ROSCillator:EFControl:ABSolute?: the EFC DAC's value
 * (0x2b58a). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ABSolute(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(float_identity(pll.efc));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:RELative?: the EFC in percent of
 * half its range, -100 to +100 from mid-scale (0x2b53c). */
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

/* :DIAGnostic:SER1:RESTricted? (0x2b4f6). */
int32_t scpi_query_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* restricted = response;

  *restricted = return_byte(nv_settings.serial_restricted);
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput? (0x2b7dc). */
int32_t scpi_query_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.debug_output;
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup? (0x2b81e). */
int32_t scpi_query_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.pforth_at_startup;
  return 0;
}

/* The self-tests :TEST? names, by its choice's index, ALL to UART
 * (0x56b2c) (the jump tables at 0x2b862 and 0x2b914). */
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
 * details (0x2b844). */
int32_t scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                   scpi_call* call) {
  /* The caller's D2: the test for a choice past the list, which the
   * parser never gives (0x2b84c). */
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
 * names, its choice (0x2b8fa). */
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

/* :DIAGnostic:GPSystem:ACURrent?: the antenna's current (0x2b34c). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = adc_average(ADC_ANTENNA_CURRENT);
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm: whether the antenna's current
 * alarms, by pllc (0x2b302). */
int32_t scpi_set_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_ACURrent_ALARm, 0,
                 given->value, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm? (0x2b332). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* alarm = response;

  *alarm = return_byte(nv_settings.antenna_current_alarm);
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG: the receiver's tracking log, by
 * pllc (0x2bbba). */
int32_t scpi_set_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_TRACking_LOG, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG? (0x2bbea). */
int32_t scpi_query_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  int16_t* enabled = response;

  *enabled = return_byte(nv_settings.tracking_log);
  return 0;
}

/* :DIAGnostic:IDENtification:GPSystem?: the receiver's ten ID lines,
 * -230 before it has sent them (0x2ac6e). */
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

/* :DIAGnostic:INPut:DATA?: 0 (0x2bb24). */
int32_t scpi_query_DIAGnostic_INPut_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = 0;
  return 0;
}

/* :DIAGnostic:LOG:READ:ALL? and :SLOG's: prints the log to the console,
 * where the Z3805A's prints it only when the query came from it; an
 * empty response (0x2b23a). */
int32_t scpi_query_DIAGnostic_LOG_READ_ALL(void* parameters, void* response,
                                           scpi_call* call) {
  const int16_t* log = parameters;
  scpi_token_string* text = response;

  text->length = 0;
  log_print(*log, console_write, printf);
  return 0;
}

/* :DIAGnostic:ME?: an undefined header, where the Z3805A's answers 0
 * (0x2b2f0). */
int32_t scpi_query_DIAGnostic_ME(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:QUERy:RESPonse?: from source 0, the console's, the
 * responses it has echoed since the last command, scpi_echo_text,
 * copied into scpi_echo_response and NUL-terminated (0x2b470).  From
 * another source, which return_zero never gives, the count, the text
 * and the copy would be A3's, A4's and A2's as the caller left them
 * (docs/firmware/quirks.md). */
int32_t scpi_query_DIAGnostic_QUERy_RESPonse(void* parameters, void* response,
                                             scpi_call* call) {
  char* copy = CPU_CALLERS_REGISTER(char*, "a2");
  uint8_t* length = CPU_CALLERS_REGISTER(uint8_t*, "a3");
  char* echo = CPU_CALLERS_REGISTER(char*, "a4");
  char** text = response;
  uint8_t index;

  if ((int8_t)return_zero(call->input) == REQUEST_SOURCE_SCPI) {
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

/* :DIAGnostic:REFerence:EXTernal:QUEStionable:HYSTeresis and its query:
 * an undefined header, each through scpi_query_failed (0x2bb96,
 * 0x2bba8). */
int32_t scpi_set_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis: the GPS
 * reference's questionable hysteresis, or MIN or MAX (0x2bb30). */
int32_t scpi_set_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound, 0, given->integer, return_zero(call->input));
  return 0;
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis?, or its bound
 * (0x2bb6a). */
int32_t scpi_query_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* hysteresis = response;

  hysteresis->value = float_to_unsigned(scpi_numeric_get(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:CURRent?: the oven's current (0x2b526). */
int32_t scpi_query_DIAGnostic_ROSCillator_CURRent(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = hmon_channel_read(ADC_OVEN_CURRENT);
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA: the EFC that the USER mode
 * holds, a word, or MIN or MAX (0x2b620). */
int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_DATA(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_DATA, 0,
                   given->kind, given->value, 0, (uint16_t)given->value,
                   return_zero(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA?, or its bound (0x2b65e). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_DATA(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;
  int16_t* efc = response;

  *efc = float_to_unsigned(
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_DATA, 0,
                       given->kind, given->value));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:MODE's choices, the node's list
 * (0x569f8). */
enum {
  SCPI_DIAGNOSTIC_EFC_NORMAL = 0,
  SCPI_DIAGNOSTIC_EFC_TIMED = 1,
  SCPI_DIAGNOSTIC_EFC_USER = 2,
};

/* :DIAGnostic:ROSCillator:EFControl:MODE NORM, TIM or USER, as
 * diag_efc_mode's EFC_MODE_NORMAL, _TIMED and _USER, by pllc
 * (0x2b5a8). */
int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_MODE(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;
  efc_mode mode;

  if (given->value == SCPI_DIAGNOSTIC_EFC_USER) {
    mode = EFC_MODE_USER;
  } else if (given->value == SCPI_DIAGNOSTIC_EFC_TIMED) {
    mode = EFC_MODE_TIMED;
  } else {
    mode = EFC_MODE_NORMAL;
  }
  pllc_request_a(&scpi_param_DIAGnostic_ROSCillator_EFControl_MODE, 0, mode,
                 return_zero(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:MODE? (0x2b5f6). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_MODE(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  uint16_t* choice = response;

  if (diag_efc_mode == EFC_MODE_USER) {
    *choice = SCPI_DIAGNOSTIC_EFC_USER;
  } else if (diag_efc_mode == EFC_MODE_TIMED) {
    *choice = SCPI_DIAGNOSTIC_EFC_TIMED;
  } else {
    *choice = SCPI_DIAGNOSTIC_EFC_NORMAL;
  }
  return 0;
}

/* :DIAGnostic:STATus:HAPPening n: the nth happening, the condition n
 * past the last error, 0 to STATUS_USER_REPORTED_CLEARED's; -222 for
 * another (0x2b740). */
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

/* The time code's masks, :DIAGnostic:TCODe:ERRor and :STATus's AMASk
 * and OMASk: the low byte of a long (0x2ba84 to 0x2bb12). */
int32_t scpi_set_DIAGnostic_TCODe_ERRor_AMASk(void* parameters, void* response,
                                              scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  tcode_error_and_mask = given->integer;
  return 0;
}

int32_t scpi_query_DIAGnostic_TCODe_ERRor_AMASk(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_long* mask = response;

  mask->value = tcode_error_and_mask;
  return 0;
}

int32_t scpi_set_DIAGnostic_TCODe_ERRor_OMASk(void* parameters, void* response,
                                              scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  tcode_error_or_mask = given->integer;
  return 0;
}

int32_t scpi_query_DIAGnostic_TCODe_ERRor_OMASk(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_long* mask = response;

  mask->value = tcode_error_or_mask;
  return 0;
}

int32_t scpi_set_DIAGnostic_TCODe_STATus_AMASk(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  tcode_status_and_mask = given->integer;
  return 0;
}

int32_t scpi_query_DIAGnostic_TCODe_STATus_AMASk(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  scpi_diagnostic_long* mask = response;

  mask->value = tcode_status_and_mask;
  return 0;
}

int32_t scpi_set_DIAGnostic_TCODe_STATus_OMASk(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  tcode_status_or_mask = given->integer;
  return 0;
}

int32_t scpi_query_DIAGnostic_TCODe_STATus_OMASk(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  scpi_diagnostic_long* mask = response;

  mask->value = tcode_status_or_mask;
  return 0;
}

/* :DIAGnostic:TEMPerature?: the board's temperature (0x2b830). */
int32_t scpi_query_DIAGnostic_TEMPerature(void* parameters, void* response,
                                          scpi_call* call) {
  scpi_diagnostic_float* temperature = response;

  temperature->value = hmon_channel_read(ADC_TEMPERATURE);
  return 0;
}

/* :DIAGnostic:TMODe:STATe: test mode, in which the latch's writers write
 * diag_tmode_data (0x2ba2a). */
int32_t scpi_set_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                        scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  diag_tmode_state = given->value != 0;
  return 0;
}

/* :DIAGnostic:TMODe:STATe? (0x2ba48). */
int32_t scpi_query_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                          scpi_call* call) {
  uint16_t* state = response;

  *state = diag_tmode_state;
  return 0;
}

/* :DIAGnostic:TMODe:DATA: the latch's byte in test mode, a long's low
 * byte (0x2ba5a). */
int32_t scpi_set_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  diag_tmode_data = given->integer;
  return 0;
}

/* :DIAGnostic:TMODe:DATA? (0x2ba72). */
int32_t scpi_query_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = diag_tmode_data;
  return 0;
}

/* :DIAGnostic:TSET and its query: an undefined header, each through
 * scpi_query_failed (0x2b362, 0x2b374). */
int32_t scpi_set_DIAGnostic_TSET(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_TSET(void* parameters, void* response,
                                   scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TVALid:NOW: marks the time valid now (0x2bc04). */
int32_t scpi_set_DIAGnostic_TVALid_NOW(void* parameters, void* response,
                                       scpi_call* call) {
  clock_mark_set();
  return 0;
}
