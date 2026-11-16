/* From the 58503B 1.01.04's rewritten/scpi_diagnostic.c and the Z3805A
 * 3543B's rewritten/scpi_diagnostic.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those the Z3805A 1.01.03's code carries
 * identical or relocated (docs/firmware/z3805a-1.01.03.md, "The link
 * list").  Its comments' addresses are the Z3805A 1.01.03's, but where they
 * name another image.  Each keeps its generated prototype. */

/* The self-tests :TEST? names, by its choice's index, ALL to UART
 * (0x5837a) (the jump tables at 0x2d288 and 0x2d374). */
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

typedef struct scpi_diagnostic_indexed_numeric {
  int16_t index;                 /* +0x00 */
  scpi_diagnostic_numeric given; /* +0x02 */
} scpi_diagnostic_indexed_numeric;

/* :DIAGnostic:ROSCillator:EFControl:MODE's choices, the node's list
 * (0x58222). */
enum {
  SCPI_DIAGNOSTIC_EFC_NORMAL = 0,
  SCPI_DIAGNOSTIC_EFC_TIMED = 1,
  SCPI_DIAGNOSTIC_EFC_USER = 2,
};

/* :DIAGnostic:GPSystem:ACURrent:STATe?'s choices, its response's list
 * (0x5864a). */
enum {
  SCPI_DIAGNOSTIC_ACURRENT_MAX = 0,
  SCPI_DIAGNOSTIC_ACURRENT_MIN = 1,
  SCPI_DIAGNOSTIC_ACURRENT_NORM = 2,
  SCPI_DIAGNOSTIC_ACURRENT_ERR = 3,
};

/* The results :DIAGnostic:GPSystem:ACURrent:STATe? tests
 * hmon_check_antenna_current's for: 0 and 1, which it returns, and 0xff,
 * which it never does (0x2c766 to 0x2c786). */
static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_NORMAL = 0;

static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_FAULT = 1;

static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_LOW = 0xff;

/* :DIAGnostic:ROSCillator:PTESt:TYPE's choices, its list (0x58276). */
enum {
  SCPI_DIAGNOSTIC_PTEST_MEAS = 0,
  SCPI_DIAGNOSTIC_PTEST_PRED = 1,
};

/* :DIAGnostic:ROSCillator:PTESt:STATe?'s first item's choices, its
 * response's list (0x58202). */
enum {
  SCPI_DIAGNOSTIC_PTEST_ARM = 0,
  SCPI_DIAGNOSTIC_PTEST_FAIL = 1,
  SCPI_DIAGNOSTIC_PTEST_OFF = 2,
  SCPI_DIAGNOSTIC_PTEST_PASS = 3,
  SCPI_DIAGNOSTIC_PTEST_RUN = 4,
};

/* :DIAGnostic:ROSCillator:PTESt:STATe?'s response: the test's state's
 * choice, then its passes. */
typedef struct scpi_diagnostic_ptest_state {
  uint16_t state;  /* +0x00 */
  uint16_t passes; /* +0x02 */
} scpi_diagnostic_ptest_state;

/* :DIAGnostic:GPSystem:UTC? (0x2bf74). */
int32_t scpi_query_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                           scpi_call* call) {
  int16_t* utc = response;

  *utc = (int8_t)nv_settings.gps_utc;
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:ABSolute?: the EFC DAC's value
 * (0x2ca28). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ABSolute(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(float_identity(pll.efc));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:RELative?: the EFC in percent of
 * half its range, -100 to +100 from mid-scale (0x2c9da). */
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

/* :DIAGnostic:SER1:RESTricted? (0x2c9aa). */
int32_t scpi_query_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* restricted = response;

  *restricted = return_byte(nv_settings.serial_restricted);
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput? (0x2d1d2). */
int32_t scpi_query_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.debug_output;
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup? (0x2d214). */
int32_t scpi_query_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.pforth_at_startup;
  return 0;
}

/* :DIAGnostic:TEST:RESult?: the last test's result and, for one :TEST?
 * names, its choice (0x2d35a). */
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

/* :DIAGnostic:GPSystem:ACURrent?: the antenna's current (0x2c748). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = adc_average(ADC_ANTENNA_CURRENT);
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm? (0x2c72e). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* alarm = response;

  *alarm = return_byte(nv_settings.antenna_current_alarm);
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG? (0x2d64a). */
int32_t scpi_query_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  int16_t* enabled = response;

  *enabled = return_byte(nv_settings.tracking_log);
  return 0;
}

/* :DIAGnostic:INPut:DATA?: 0 (0x2d584). */
int32_t scpi_query_DIAGnostic_INPut_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = 0;
  return 0;
}

/* :DIAGnostic:LOG:READ:ALL? and :SLOG's: prints the log to the console,
 * where the Z3805A's prints it only when the query came from it; an
 * empty response (0x2c5a0). */
int32_t scpi_query_DIAGnostic_LOG_READ_ALL(void* parameters, void* response,
                                           scpi_call* call) {
  const int16_t* log = parameters;
  scpi_token_string* text = response;

  text->length = 0;
  log_print(*log, console_write, printf);
  return 0;
}

/* :DIAGnostic:REFerence:EXTernal:QUEStionable:HYSTeresis and its query:
 * an undefined header, each through scpi_query_failed (0x2d5f6,
 * 0x2d608). */
int32_t scpi_set_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TMODe:STATe: test mode, in which the latch's writers write
 * diag_tmode_data (0x2d48a). */
int32_t scpi_set_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                        scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  diag_tmode_state = given->value != 0;
  return 0;
}

/* :DIAGnostic:TMODe:STATe? (0x2d4a8). */
int32_t scpi_query_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                          scpi_call* call) {
  uint16_t* state = response;

  *state = diag_tmode_state;
  return 0;
}

/* :DIAGnostic:TVALid:NOW: marks the time valid now (0x2d664). */
int32_t scpi_set_DIAGnostic_TVALid_NOW(void* parameters, void* response,
                                       scpi_call* call) {
  clock_mark_set();
  return 0;
}

/* The time code's masks, :DIAGnostic:TCODe:ERRor and :STATus's AMASk
 * and OMASk: the low byte of a long (0x2d4e4 to 0x2d572). */
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

/* :DIAGnostic:LOG:COUNt? and :SLOG's, by the path word: the log's
 * entries, or for MINimum 1 and for MAXimum the log's capacity, the
 * event log's 74 pages of three entries, the reset-cause log's records
 * (0x2c286). */
int32_t scpi_query_DIAGnostic_LOG_COUNt(void* parameters, void* response,
                                        scpi_call* call) {
  /* The event log's pages, log.c's LOG_PAGES (0x2c2a8). */
  static const int16_t EVENT_LOG_PAGES = 74;
  static const int16_t EVENT_LOG_ENTRIES =
      EVENT_LOG_PAGES * RECORDS_LOG_PAGE_ENTRIES;
  static const int16_t FEWEST_ENTRIES = 1;
  const scpi_diagnostic_indexed_word* given = parameters;
  scpi_short_item* count = response;
  int16_t entries;

  if (given->given.kind == SCPI_VALUE_BOUND) {
    if (given->given.value == SCPI_BOUND_MINIMUM) {
      entries = FEWEST_ENTRIES;
    } else if (given->index == SCPI_DIAGNOSTIC_EVENT_LOG) {
      entries = EVENT_LOG_ENTRIES;
    } else {
      entries = RECORDS_RESET_RECORDS;
    }
  } else {
    entries = scpi_diagnostic_log_entries(given->index);
  }
  if (entries < 0) {
    return scpi_query_failed(STATUS_DEVICE_SPECIFIC_ERROR, call);
  }
  count->value = entries;
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA?, or its bound: a signed long,
 * where the 58503A's answers an unsigned word (0x2caf8). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_DATA(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_DATA, 0,
                       given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:DURation?: the test's duration in hours
 * (0x2cfee). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_DURation(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  uint32_t* hours = response;

  *hours = ptest_duration_hours();
  return 0;
}

/* :DIAGnostic:LOG:WRAP?, sign-extended (0x2c674). */
int32_t scpi_query_DIAGnostic_LOG_WRAP(void* parameters, void* response,
                                       scpi_call* call) {
  int16_t* wrap = response;

  *wrap = (int8_t)nv_settings.log_wrap;
  return 0;
}

int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_BSLOPe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* slope = response;

  *slope = scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                            0, given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:MODE?, sign-extended (0x2cde6). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_MODE(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  int16_t* mode = response;

  *mode = (int8_t)scpi_value_DIAGnostic_ROSCillator_PTESt_MODE;
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:COUNt?, or its bound, as a long
 * (0x2ce8e). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_COUNt(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* count = response;

  count->value = float_to_integer(
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_PTESt_COUNt, 0,
                       given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:SYSTem:PDEBug: the loop's debug output on or off
 * (0x2d160). */
int32_t scpi_set_DIAGnostic_SYSTem_PDEBug(void* parameters, void* response,
                                          scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pll_debug_enabled = given->value != 0;
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:ASLOPe, :BSLOPe and :DADC: the OCXO
 * calibration's a and b and the EFC's DAC counts per ADC count, each a
 * float or MIN or MAX, through its descriptor (0x2cc96, 0x2ccf6,
 * 0x2cd56). */
int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_ASLOPe(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_ASLOPe, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_BSLOPe(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_BSLOPe, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_DADC(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_DADC, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

/* Their queries, each value or its bound (0x2ccd0, 0x2cd30, 0x2cd90). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ASLOPe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* slope = response;

  *slope = scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_ASLOPe,
                            0, given->kind, given->bound);
  return 0;
}

int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_DADC(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* ratio = response;

  *ratio = scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_DADC,
                            0, given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:ADC?: an empty response, and the health monitor's averages
 * printed to the console, the one session (0x2c6e8). */
int32_t scpi_query_DIAGnostic_ADC(void* parameters, void* response,
                                  scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  adc_averages_print(printf);
  return 0;
}

/* :DIAGnostic:ROSCillator:LTIMe:DATA?: the loop's time constant
 * (0x2cb8c). */
int32_t scpi_query_DIAGnostic_ROSCillator_LTIMe_DATA(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  float* time_constant = response;

  *time_constant = pll.loop_time_constant;
  return 0;
}

/* :DIAGnostic:TCODe:ASCii: whether the time code's digits go as ASCII,
 * es_ascii, as the es_ascii console word sets it (0x2d226). */
int32_t scpi_set_DIAGnostic_TCODe_ASCii(void* parameters, void* response,
                                        scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  es_ascii = given->value != 0;
  return 0;
}

/* :DIAGnostic:TCODe:ASCii? (0x2d244). */
int32_t scpi_query_DIAGnostic_TCODe_ASCii(void* parameters, void* response,
                                          scpi_call* call) {
  uint16_t* ascii = response;

  *ascii = es_ascii;
  return 0;
}

/* :DIAGnostic:LOG:WRITe number and :SLOG's: logs "diag entry number"
 * to the event log; -200 when it is full, -311 when the EEPROM write
 * fails, else -300 for a failure (0x2c5c6). */
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

/* :DIAGnostic:TEST? test: runs it through pllc; its result and two
 * details (0x2d26a). */
int32_t scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                   scpi_call* call) {
  /* The caller's D2: the test for a choice past the list, which the
   * parser never gives (0x2d272). */
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

/* :DIAGnostic:GPSystem:ACURrent:ALARm: whether the antenna's current
 * alarms, by pllc (0x2c6fe). */
int32_t scpi_set_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_ACURrent_ALARm, 0,
                 given->value, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG: the receiver's tracking log, by
 * pllc (0x2d61a). */
int32_t scpi_set_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_TRACking_LOG, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis: the GPS
 * reference's questionable hysteresis, or MIN or MAX (0x2d590). */
int32_t scpi_set_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound, 0, given->integer,
      scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:STATus:HAPPening n: the nth happening, the condition n
 * past the last error, 0 to STATUS_USER_REPORTED_CLEARED's; -222 for
 * another (0x2d118). */
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

/* :DIAGnostic:ROSCillator:EFControl:MODE NORM, TIM or USER, as
 * diag_efc_mode's EFC_MODE_NORMAL, _TIMED and _USER, by pllc
 * (0x2ca46). */
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
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:MODE? (0x2ca94). */
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

/* :DIAGnostic:TMODe:DATA: the latch's byte in test mode, a long's low
 * byte (0x2d4ba). */
int32_t scpi_set_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  diag_tmode_data = given->integer;
  return 0;
}

/* :DIAGnostic:TMODe:DATA? (0x2d4d2). */
int32_t scpi_query_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = diag_tmode_data;
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA: the EFC that the USER mode
 * holds, a long, or MIN or MAX, where the 58503A's takes a word
 * (0x2cabe). */
int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_DATA(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_DATA, 0,
                   given->kind, given->bound, 0, given->integer,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:LOG:WRAP and :SLOG's: whether the event log wraps, by pllc,
 * the log's path word unread (0x2c644). */
int32_t scpi_set_DIAGnostic_LOG_WRAP(void* parameters, void* response,
                                     scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_LOG_WRAP, 0, given->given.value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:STATe?: NORM while
 * hmon_check_antenna_current finds no fault, MAX for a fault; MIN for
 * 0xff and ERR for any other result, which it never gives (0x2c75e). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent_STATe(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  uint16_t* state = response;
  const uint8_t result = hmon_check_antenna_current();

  if (result == SCPI_DIAGNOSTIC_ANTENNA_NORMAL) {
    *state = SCPI_DIAGNOSTIC_ACURRENT_NORM;
  } else if (result == SCPI_DIAGNOSTIC_ANTENNA_FAULT) {
    *state = SCPI_DIAGNOSTIC_ACURRENT_MAX;
  } else if (result == SCPI_DIAGNOSTIC_ANTENNA_LOW) {
    *state = SCPI_DIAGNOSTIC_ACURRENT_MIN;
  } else {
    *state = SCPI_DIAGNOSTIC_ACURRENT_ERR;
  }
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:MODE: the time uncertainty test's mode,
 * by pllc (0x2cdb6). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_MODE(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_ROSCillator_PTESt_MODE, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:TYPE MEAS or PRED: the uncertainty the
 * test takes, PTEST_TYPE_MEASURED for any choice but PRED, by pllc
 * (0x2cdf8). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_TYPE(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;
  const ptest_uncertainty_type type = given->value != SCPI_DIAGNOSTIC_PTEST_PRED
                                          ? PTEST_TYPE_MEASURED
                                          : PTEST_TYPE_PREDICTED;

  pllc_request_a(&scpi_param_DIAGnostic_ROSCillator_PTESt_TYPE, 0, type,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:TYPE? (0x2ce36). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_TYPE(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  uint16_t* type = response;

  *type = scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE == PTEST_TYPE_MEASURED
              ? SCPI_DIAGNOSTIC_PTEST_MEAS
              : SCPI_DIAGNOSTIC_PTEST_PRED;
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:COUNt: the test's count, a long or MIN or
 * MAX, through its descriptor (0x2ce54). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_COUNt(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_PTESt_COUNt, 0,
                   given->kind, given->bound, 0, given->integer,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:PTHR: one of the test's two float
 * thresholds, by the path word, through its descriptor (0x2ceba). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_PTHR(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_PTESt_PTHR, given->index,
                   given->given.kind, given->given.bound,
                   given->given.real_bits, 0, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:PTHR?, or its bound (0x2cef8). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_PTHR(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  float* threshold = response;

  *threshold =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_PTESt_PTHR,
                       given->index, given->given.kind, given->given.bound);
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:TTHR: one of the test's two long
 * thresholds, by the path word, through its descriptor (0x2cf22). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_TTHR(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_PTESt_TTHR, given->index,
                   given->given.kind, given->given.bound, 0,
                   given->given.integer, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:TTHR?, or its bound, as a long
 * (0x2cf60). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_TTHR(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  scpi_diagnostic_long* threshold = response;

  threshold->value = float_to_integer(
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_PTESt_TTHR,
                       given->index, given->given.kind, given->given.bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:STATe?: the test's state, OFF for any
 * past PTEST_STATE_FAILED, and its passes (0x2cf90, its jump table at
 * 0x2cfb2). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_STATe(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_diagnostic_ptest_state* answer = response;

  switch (ptest_state_get()) {
    case PTEST_STATE_ARMED:
      answer->state = SCPI_DIAGNOSTIC_PTEST_ARM;
      break;
    case PTEST_STATE_RUNNING:
      answer->state = SCPI_DIAGNOSTIC_PTEST_RUN;
      break;
    case PTEST_STATE_PASSED:
      answer->state = SCPI_DIAGNOSTIC_PTEST_PASS;
      break;
    case PTEST_STATE_FAILED:
      answer->state = SCPI_DIAGNOSTIC_PTEST_FAIL;
      break;
    default:
      answer->state = SCPI_DIAGNOSTIC_PTEST_OFF;
      break;
  }
  answer->passes = ptest_passes();
  return 0;
}

/* :DIAGnostic:ROSCillator:CURRent?: the oscillator's current, the health
 * monitor's channel (0x2c9c4). */
int32_t scpi_query_DIAGnostic_ROSCillator_CURRent(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = hmon_channel_read(ADC_OSCILLATOR_CURRENT);
  return 0;
}

/* :DIAGnostic:TEMPerature?: the board's temperature, the health
 * monitor's channel (0x2d256). */
int32_t scpi_query_DIAGnostic_TEMPerature(void* parameters, void* response,
                                          scpi_call* call) {
  scpi_diagnostic_float* temperature = response;

  temperature->value = hmon_channel_read(ADC_TEMPERATURE);
  return 0;
}

/* :DIAGnostic:ROSCillator:TCO, either of its two values by the path word,
 * a float or MIN or MAX, through its descriptor (0x2cb24). */
int32_t scpi_set_DIAGnostic_ROSCillator_TCO(void* parameters, void* response,
                                            scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_TCOefficient,
                   given->index, given->given.kind, given->given.bound,
                   given->given.real_bits, 0, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCO?, or its bound (0x2cb62). */
int32_t scpi_query_DIAGnostic_ROSCillator_TCO(void* parameters, void* response,
                                              scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  float* coefficient = response;

  *coefficient =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_TCOefficient,
                       given->index, given->given.kind, given->given.bound);
  return 0;
}

int32_t scpi_set_DIAGnostic_ROSCillator_LTIMe_MAX(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  pllc_request_b(&scpi_action_temporary_hold, 0,
                 scpi_input_source(call->input));
  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_LTIMe_MAX, 0, given->kind,
                   given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

/* Their queries, each value or its bound (0x2cbf2, 0x2cc70). */
int32_t scpi_query_DIAGnostic_ROSCillator_LTIMe_INIT(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* time_constant = response;

  *time_constant =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_LTIMe_INIT, 0,
                       given->kind, given->bound);
  return 0;
}

int32_t scpi_query_DIAGnostic_ROSCillator_LTIMe_MAX(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* time_constant = response;

  *time_constant =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_LTIMe_MAX, 0,
                       given->kind, given->bound);
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

/* :DIAGnostic:IDENtification:DEFault?: the default model and hardware
 * suffix (0x2bf86). */
int32_t scpi_query_DIAGnostic_IDENtification_DEFault(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  struct {
    const char* model;  /* +0x00 */
    const char* suffix; /* +0x04 */
  }* defaults = response;

  defaults->model = RECORDS_MODEL;
  /* The suffix is the tag's own string (0x34f6b). */
  defaults->suffix = RECORDS_TAG;
  return 0;
}

/* What keeps a Time RAIM setting from the receiver: the condition given
 * when gps_message_supported cannot say, hardware missing when the
 * receiver lacks the request, else none. */
static status_condition_code scpi_diagnostic_raim_missing(
    gps_request_code request, status_condition_code unknown) {
  uint8_t supported;
  /* The code goes as a word, its byte's sign extended (0x2bbb2). */
  const uint8_t known = gps_message_supported((int8_t)request, &supported);

  if (known == 0) {
    return unknown;
  }
  return supported == 0 ? STATUS_HARDWARE_MISSING : STATUS_NO_CONDITION;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold: the Time RAIM
 * alarm's limit (0x2bb8c). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;
  const request_source source = scpi_input_source(call->input);
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold.gps_request,
      STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, source);
    return 0;
  }
  scpi_numeric_set(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_THReshold,
                   given->index, given->given.kind, given->given.bound, 0,
                   given->given.integer, source);
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold?, or its bound
 * (0x2bc10). */
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

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis?, or its bound
 * (0x2d5ca). */
int32_t scpi_query_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* hysteresis = response;

  hysteresis->value = float_to_unsigned(scpi_numeric_get(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:IDENtification:GPSystem?: the receiver's ten ID lines,
 * -230 before it has sent them (0x2bfa0). */
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

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe? (0x2bb30). */
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

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe: the receiver's Time
 * RAIM, by pllc (0x2bab8). */
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

/* :DIAGnostic:ROSCillator:TYPE?: oscillator_type, sign-extended; with
 * MIN 0, with MAX oscillator_type_most (0x2d088). */
int32_t scpi_query_DIAGnostic_ROSCillator_TYPE(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  uint16_t* type = response;

  if (given->kind != SCPI_VALUE_BOUND) {
    *type = (int8_t)oscillator_type;
  } else if (given->bound == SCPI_BOUND_MINIMUM) {
    *type = 0;
  } else {
    *type = oscillator_type_most();
  }
  return 0;
}

/* :DIAGnostic:ROSCillator:LTIMe:INIT and :MAX: the loop's initial and
 * greatest time constants, each a float or MIN or MAX, through its
 * descriptor, after the action each requests first by pllc: :INIT's
 * scpi_action_loop_time_init, :MAX's the temporary hold (0x2cb9a,
 * 0x2cc18). */
int32_t scpi_set_DIAGnostic_ROSCillator_LTIMe_INIT(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  pllc_request_b(&scpi_action_loop_time_init, 0,
                 scpi_input_source(call->input));
  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_LTIMe_INIT, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:TYPE: the oscillator type, 0 to
 * oscillator_type_most, by pllc, then :SYSTem:PON's action; another, -222,
 * Data out of range (0x2d004). */
int32_t scpi_set_DIAGnostic_ROSCillator_TYPE(void* parameters, void* response,
                                             scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;
  const request_source source = scpi_input_source(call->input);

  if (given->value < 0 || given->value > oscillator_type_most()) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE,
                       scpi_input_source(call->input));
    return 0;
  }
  pllc_request_a(&scpi_param_DIAGnostic_ROSCillator_TYPE, 0, given->value,
                 source);
  pllc_request_b(&scpi_action_SYSTem_PON, 0, source);
  return 0;
}

/* :DIAGnostic:GPSystem:TIME?: the receiver's local date and time, and
 * the fraction of a second gps_time_local_get gives, -230 before it has
 * reported them (0x2beae). */
int32_t scpi_query_DIAGnostic_GPSystem_TIME(void* parameters, void* response,
                                            scpi_call* call) {
  struct {
    scpi_short_item year;   /* +0x00 */
    scpi_short_item month;  /* +0x04 */
    scpi_short_item day;    /* +0x08 */
    scpi_short_item hour;   /* +0x0c */
    scpi_short_item minute; /* +0x10 */
    scpi_short_item second; /* +0x14 */
    uint32_t fraction;      /* +0x18 */
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
  now->year.value = date.parts.year;
  now->month.value = date.parts.month;
  now->day.value = date.parts.day;
  now->hour.value = time.hour;
  now->minute.value = time.minute;
  now->second.value = time.second;
  now->fraction = fraction;
  return 0;
}

/* :DIAGnostic:TEST:FLASh?: an undefined header (0x2d320). */
int32_t scpi_query_DIAGnostic_TEST_FLASh(void* parameters, void* response,
                                         scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:GPSystem:DCXO?: hardware missing (0x2c828). */
int32_t scpi_query_DIAGnostic_GPSystem_DCXO(void* parameters, void* response,
                                            scpi_call* call) {
  return scpi_query_failed(STATUS_HARDWARE_MISSING, call);
}

/* :DIAGnostic:GPSystem:DCXO with its three numbers: staged for its
 * source and sent pllc as the parameter's three values while
 * field_592's bit 0 is set, else hardware missing (0x2c798). */
int32_t scpi_set_DIAGnostic_GPSystem_DCXO(void* parameters, void* response,
                                          scpi_call* call) {
  static const uint8_t VALUES = 3;
  const scpi_long_parameter* given = parameters;

  if ((gps.field_592 & GPS_FIELD_592_BIT_0) == 0) {
    return scpi_query_failed(STATUS_HARDWARE_MISSING, call);
  }
  const request_source source = scpi_input_source(call->input);
  int32_t* staged = scpi_staged[source];

  for (uint8_t index = 0; index < VALUES; index++) {
    staged[index] = given[index].value;
  }
  pllc_request_d(&scpi_param_DIAGnostic_GPSystem_DCXO, VALUES - 1,
                 (uint32_t)staged, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:CALibration:ROSCillator:GPS requests self-test 15 of
 * pllc, as :DIAGnostic:TEST? requests the test it is given (0x2d332). */
int32_t scpi_set_DIAGnostic_CALibration_ROSCillator_GPS(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  pllc_request_b(&scpi_action_self_test, SELFTEST_TEST_15,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:QUERy:RESPonse?: from the SCPI source, the responses it
 * has echoed since the last command, scpi_echo_text, copied into
 * scpi_echo_response and NUL-terminated (0x2c924).  From another source
 * the count, the text and the copy are A3's, A4's and A2's as the
 * caller left them (docs/firmware/quirks.md). */
int32_t scpi_query_DIAGnostic_QUERy_RESPonse(void* parameters, void* response,
                                             scpi_call* call) {
  char* copy = CPU_CALLERS_REGISTER(char*, "a2");
  uint8_t* length = CPU_CALLERS_REGISTER(uint8_t*, "a3");
  char* echo = CPU_CALLERS_REGISTER(char*, "a4");
  char** text = response;
  uint8_t index;

  if ((uint8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCPI) {
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

/* :DIAGnostic:GPSystem:UTC, by pllc, the value as given, as the Z3805A
 * 3543B's (0x2bf44); the 58503B 1.01.04's takes a number out of range
 * as 1. */
int32_t scpi_set_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                         scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}
