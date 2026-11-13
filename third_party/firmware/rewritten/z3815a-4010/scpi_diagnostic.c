/* scpi_diagnostic.c's functions that are this image's own: the rest are
 * common/scpi_diagnostic.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C"), and the decompiler's.  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* :DIAGnostic:ADC?'s report: the monitored average of each health monitor
 * channel marked for it, by its name, through the printer its query
 * passes, the 58503A 3704's diag_pr_adc_avg over a printer (0x3ea52). */
void adc_averages_print(adc_average_printer* print) {
  static const uint32_t CHANNELS =
      sizeof(hmon_channels) / sizeof(hmon_channels[0]);

  for (uint32_t channel = 0; channel < CHANNELS; channel++) {
    if (hmon_channels[channel].field_28 != 0) {
      print("%s: %.2f\r\n", hmon_channels[channel].name,
            float_to_double(adc_average((adc_channel)channel)));
    }
  }
}

/* The flash's block :DIAGnostic:TEST:FLASh? erases and programs, 0x4000
 * to 0x5fff, its end not in it (docs/firmware/z3815a-4010.md), what an
 * erased word reads, and the two words it programs at its start. */
static const uint32_t FLASH_TEST_BLOCK = 0x4000;

static const uint32_t FLASH_TEST_BLOCK_END = 0x6000;

static const uint16_t FLASH_TEST_ERASED = 0xffff;

static const uint16_t FLASH_TEST_FIRST = 0x5555;

static const uint16_t FLASH_TEST_SECOND = 0xaaaa;

/* flash_test's results: the block erased and programmed, or which of the
 * two failed. */
static const int32_t FLASH_TEST_PASSED = 0;

static const int32_t FLASH_TEST_ERASE_FAILED = 1;

static const int32_t FLASH_TEST_PROGRAM_FAILED = 2;

/* The primary's RAM copies of the flash's routines, which its reset
 * unpacks from its records (primary_unpack_records), called as the image
 * calls them: the address on the stack, then, for a program, the word,
 * pushed as a word, not in gcc's long slot.  Each returns whether the
 * status register showed no error in D0's low byte and changes D0, D1,
 * A0 and the condition codes. */
static bool flash_test_erase_block(uint32_t block) {
  register uint32_t d0 __asm__("d0");

  __asm__ volatile(
      "move.l %1,-(%%sp)\n\t"
      "jsr flash_ram_erase_block\n\t"
      "addq.l #4,%%sp"
      : "=r"(d0)
      : "ri"(block)
      : "d1", "a0", "a1", "cc", "memory");
  return (uint8_t)d0 != 0;
}

static bool flash_test_program_word(uint32_t address, uint16_t word) {
  register uint32_t d0 __asm__("d0");

  __asm__ volatile(
      "move.w %2,-(%%sp)\n\t"
      "move.l %1,-(%%sp)\n\t"
      "jsr flash_ram_program_word\n\t"
      "addq.l #6,%%sp"
      : "=r"(d0)
      : "ri"(address), "di"(word)
      : "d1", "a0", "a1", "cc", "memory");
  return (uint8_t)d0 != 0;
}

/* flash_test's erase: the block erased with every interrupt masked,
 * then read back; whether it erased and every word reads erased, the
 * read stopping at the first that does not. */
bool flash_test_erase(void) {
  const uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);
  bool erased = flash_test_erase_block(FLASH_TEST_BLOCK);

  cpu_sr_write(sr);
  for (volatile uint16_t* at = (volatile uint16_t*)FLASH_TEST_BLOCK;
       erased && at < (volatile uint16_t*)FLASH_TEST_BLOCK_END;) {
    if (*at++ != FLASH_TEST_ERASED) {
      erased = false;
    }
  }
  return erased;
}

/* flash_test's program: the block's first two words programmed with
 * every interrupt masked, the second only once the first is, then read
 * back; whether both programmed and read so. */
bool flash_test_program(void) {
  volatile uint16_t* const first = (volatile uint16_t*)FLASH_TEST_BLOCK;
  volatile uint16_t* const second = first + 1;
  const uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);
  bool programmed = flash_test_program_word(FLASH_TEST_BLOCK, FLASH_TEST_FIRST);

  if (programmed) {
    programmed = flash_test_program_word((uint32_t)second, FLASH_TEST_SECOND);
  }
  cpu_sr_write(sr);
  if (programmed &&
      (*first != FLASH_TEST_FIRST || *second != FLASH_TEST_SECOND)) {
    programmed = false;
  }
  return programmed;
}

/* :DIAGnostic:TEST:FLASh?'s test, in supervisor mode: erases and
 * programs the flash's test block (flash_test_erase, flash_test_program),
 * then moves the status register back as enter_supervisor gave it. */
int32_t flash_test(void) {
  int32_t result = FLASH_TEST_PASSED;
  const uint16_t sr = cpu_enter_supervisor();

  if (!flash_test_erase()) {
    result = FLASH_TEST_ERASE_FAILED;
  } else if (!flash_test_program()) {
    result = FLASH_TEST_PROGRAM_FAILED;
  }
  cpu_sr_write(sr);
  return result;
}

/* :DIAGnostic:GPSystem:UTC? (0x3e304). */
int32_t scpi_query_DIAGnostic_GPSystem_UTC(void* parameters, void* response,
                                           scpi_call* call) {
  int16_t* utc = response;

  *utc = (int8_t)nv_settings.gps_utc;
  return 0;
}

/* :DIAGnostic:LOG:WRITe number and :SLOG's: logs "diag entry number"
 * to the event log; -200 when it is full, -311 when the EEPROM write
 * fails, else -300 for a failure (0x3e96e). */
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
 * (0x3ea2e). */
int32_t scpi_set_DIAGnostic_ME(void* parameters, void* response,
                               scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:ROSCillator:EFControl:ABSolute?: the EFC DAC's value
 * (0x3edba). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ABSolute(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_diagnostic_long* efc = response;

  efc->value = float_to_integer(float_identity(pll.efc));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:RELative?: the EFC in percent of
 * half its range, -100 to +100 from mid-scale (0x3ed6c). */
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

/* :DIAGnostic:SER1:RESTricted? (0x3ed26). */
int32_t scpi_query_DIAGnostic_SER1_RESTricted(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* restricted = response;

  *restricted = return_byte(nv_settings.serial_restricted);
  return 0;
}

/* :DIAGnostic:SYSTem:DOUTput? (0x3f394). */
int32_t scpi_query_DIAGnostic_SYSTem_DOUTput(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.debug_output;
  return 0;
}

/* :DIAGnostic:SYSTem:PSTartup? (0x3f3d6). */
int32_t scpi_query_DIAGnostic_SYSTem_PSTartup(void* parameters, void* response,
                                              scpi_call* call) {
  int16_t* enabled = response;

  *enabled = (int8_t)nv_settings.pforth_at_startup;
  return 0;
}

/* The self-tests :TEST? names, by its choice's index, ALL to UART
 * (0x61bee) (the jump tables at 0x3f418 and 0x3f626). */
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
 * details (0x3f3fa). */
int32_t scpi_query_DIAGnostic_TEST(void* parameters, void* response,
                                   scpi_call* call) {
  /* The caller's D2: the test for a choice past the list, which the
   * parser never gives (0x3f402). */
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
 * names, its choice (0x3f60c). */
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

/* :DIAGnostic:GPSystem:ACURrent?: the antenna's current (0x3eb30). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = adc_average(ADC_ANTENNA_CURRENT);
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm: whether the antenna's current
 * alarms, by pllc (0x3eae6). */
int32_t scpi_set_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_ACURrent_ALARm, 0,
                 given->value, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:ALARm? (0x3eb16). */
int32_t scpi_query_DIAGnostic_GPSystem_ACURrent_ALARm(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* alarm = response;

  *alarm = return_byte(nv_settings.antenna_current_alarm);
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG: the receiver's tracking log, by
 * pllc (0x3f922). */
int32_t scpi_set_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_TRACking_LOG, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:GPSystem:TRACking:LOG? (0x3f952). */
int32_t scpi_query_DIAGnostic_GPSystem_TRACking_LOG(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  int16_t* enabled = response;

  *enabled = return_byte(nv_settings.tracking_log);
  return 0;
}

/* :DIAGnostic:ME?: an undefined header, where the Z3805A's answers 0
 * (0x3ea40). */
int32_t scpi_query_DIAGnostic_ME(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis: the GPS
 * reference's questionable hysteresis, or MIN or MAX (0x3f852). */
int32_t scpi_set_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound, 0, given->integer,
      scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:CURRent?: the oven's current (0x3ed56). */
int32_t scpi_query_DIAGnostic_ROSCillator_CURRent(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_diagnostic_float* current = response;

  current->value = hmon_channel_read(ADC_OVEN_CURRENT);
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:MODE's choices, the node's list
 * (0x61aa6). */
enum {
  SCPI_DIAGNOSTIC_EFC_NORMAL = 0,
  SCPI_DIAGNOSTIC_EFC_TIMED = 1,
  SCPI_DIAGNOSTIC_EFC_USER = 2,
};

/* :DIAGnostic:ROSCillator:EFControl:MODE NORM, TIM or USER, as
 * diag_efc_mode's EFC_MODE_NORMAL, _TIMED and _USER, by pllc
 * (0x3edd8). */
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

/* :DIAGnostic:ROSCillator:EFControl:MODE? (0x3ee26). */
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
 * another (0x3f2da). */
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

/* The time code's masks, :DIAGnostic:TCODe:ERRor and :STATus's AMASk
 * and OMASk: the low byte of a long (0x3f798 to 0x3f826). */
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

/* :DIAGnostic:TMODe:STATe: test mode, in which the latch's writers write
 * diag_tmode_data (0x3f740). */
int32_t scpi_set_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                        scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  diag_tmode_state = given->value != 0;
  return 0;
}

/* :DIAGnostic:TMODe:STATe? (0x3f75e). */
int32_t scpi_query_DIAGnostic_TMODe_STATe(void* parameters, void* response,
                                          scpi_call* call) {
  uint16_t* state = response;

  *state = diag_tmode_state;
  return 0;
}

/* :DIAGnostic:TSET and its query: an undefined header, each through
 * scpi_query_failed (0x3eb80, 0x3eb92). */
int32_t scpi_set_DIAGnostic_TSET(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

int32_t scpi_query_DIAGnostic_TSET(void* parameters, void* response,
                                   scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TVALid:NOW: marks the time valid now (0x3f96c). */
int32_t scpi_set_DIAGnostic_TVALid_NOW(void* parameters, void* response,
                                       scpi_call* call) {
  clock_mark_set();
  return 0;
}

/* A numeric parameter after one path word, a RAIM setting's index. */
typedef struct scpi_diagnostic_indexed_numeric {
  int16_t index;                 /* +0x00 */
  scpi_diagnostic_numeric given; /* +0x02 */
} scpi_diagnostic_indexed_numeric;

/* :DIAGnostic:IDENtification:DEFault?: the default model and hardware
 * suffix, the tag's text (0x3e316). */
int32_t scpi_query_DIAGnostic_IDENtification_DEFault(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  struct {
    const char* model;  /* +0x00 */
    const char* suffix; /* +0x04 */
  }* defaults = response;

  defaults->model = "Z3815A";
  defaults->suffix = RECORDS_TAG;
  return 0;
}

/* :DIAGnostic:IDENtification:GPSystem?: the receiver's ten ID lines,
 * -230 before it has sent them (0x3e330). */
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

/* STATUS_NO_CONDITION when the receiver takes a descriptor's request;
 * else unknown before it has sent its ID, or -241 Hardware missing.  The
 * image asks gps_message_supported of the descriptor's request,
 * GPS_SCRIPT_TRAIM, which it decides by the engine's Time RAIM option,
 * where the 58503A's gps_message_supported knows nothing of it
 * (0x3de6c, 0x3df46). */
static status_condition_code scpi_diagnostic_raim_missing(
    gps_request_code request, status_condition_code unknown) {
  uint8_t supported;

  if (gps_message_supported(request, &supported) == 0) {
    return unknown;
  }
  return supported == 0 ? STATUS_HARDWARE_MISSING : STATUS_NO_CONDITION;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe: the receiver's Time
 * RAIM, by pllc (0x3de52). */
int32_t scpi_set_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe(void* parameters,
                                                              void* response,
                                                              scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;
  const request_source source = scpi_input_source(call->input);
  status_condition_code missing = scpi_diagnostic_raim_missing(
      scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe.gps_request,
      STATUS_EXECUTION_ERROR);

  if (missing != STATUS_NO_CONDITION) {
    status_report_from(missing, source);
    return 0;
  }
  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_POSition_HOLD_RAIM_STATe,
                 given->index, given->given.value, source);
  return 0;
}

/* :DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe? (0x3deca). */
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
 * limit, by pllc (0x3df26). */
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
 * (0x3dfaa). */
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

/* :DIAGnostic:LOG:COUNt? and :SLOG's, by the path word: the log's
 * entries, or for MINimum 1 and for MAXimum the log's capacity, the
 * event log's 74 pages of three entries, the reset-cause log's records
 * (0x3e616). */
int32_t scpi_query_DIAGnostic_LOG_COUNt(void* parameters, void* response,
                                        scpi_call* call) {
  /* The event log's pages, log.c's LOG_PAGES (0x3e638). */
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

/* :DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis?, or its bound
 * (0x3f88c). */
int32_t scpi_query_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* hysteresis = response;

  hysteresis->value = float_to_unsigned(scpi_numeric_get(
      &scpi_param_DIAGnostic_REFerence_GPSystem_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA: the EFC that the USER mode
 * holds, a long, or MIN or MAX, where the 58503A's takes a word
 * (0x3ee50). */
int32_t scpi_set_DIAGnostic_ROSCillator_EFControl_DATA(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_EFControl_DATA, 0,
                   given->kind, given->bound, 0, given->integer,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:EFControl:DATA?, or its bound: a signed long,
 * where the 58503A's answers an unsigned word (0x3ee8a). */
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

/* :DIAGnostic:TMODe:DATA: the latch's word in test mode, a long's low
 * word, where the 58503A's keeps a byte (0x3f770). */
int32_t scpi_set_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  diag_tmode_data = given->integer;
  return 0;
}

/* :DIAGnostic:TMODe:DATA? (0x3f786). */
int32_t scpi_query_DIAGnostic_TMODe_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = diag_tmode_data;
  return 0;
}

/* :DIAGnostic:TOFFset?, or its bound (0x3f716). */
int32_t scpi_query_DIAGnostic_TOFFset(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* offset = response;

  offset->value = scpi_numeric_get(&scpi_param_DIAGnostic_TOFFset, 0,
                                   given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:QUERy:RESPonse?: the responses the session has echoed
 * since the last command, its echo text copied into its response buffer
 * and NUL-terminated: from source 0 the SCPI port's, from any other the
 * SCI session's (0x3ec8e).  A count of 255 puts the NUL past the buffer,
 * on the next one's first byte. */
int32_t scpi_query_DIAGnostic_QUERy_RESPonse(void* parameters, void* response,
                                             scpi_call* call) {
  char** text = response;
  const uint8_t* length;
  const char* echo;
  char* copy;
  uint8_t index;

  if ((int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCPI) {
    length = &scpi_echo_length;
    echo = scpi_echo_text;
    copy = scpi_echo_response;
  } else {
    length = &sci_scpi_echo_length;
    echo = sci_scpi_echo_text;
    copy = sci_scpi_echo_response;
  }
  for (index = 0; index < *length; index++) {
    copy[index] = echo[index];
  }
  copy[index] = 0;
  *text = copy;
  return 0;
}

/* :DIAGnostic:OUTPut: in power-up, the output's state through 0x340d4,
 * set for any nonzero; past it, -221 Settings conflict (0x3fd7e). */
int32_t scpi_set_DIAGnostic_OUTPut(void* parameters, void* response,
                                   scpi_call* call) {
  scpi_word_parameter* state = parameters;

  if (pll.stage == PLL_STAGE_POWERUP) {
    output_diagnostic_set(state->value != 0);
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT,
                       scpi_input_source(call->input));
  }
  return 0;
}

/* :DIAGnostic:OUTPut?: always 0 (0x3fdcc). */
int32_t scpi_query_DIAGnostic_OUTPut(void* parameters, void* response,
                                     scpi_call* call) {
  int16_t* state = response;

  *state = 0;
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:DURation?: the test's duration in hours
 * (0x3f26e). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_DURation(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  uint32_t* hours = response;

  *hours = ptest_duration_hours();
  return 0;
}

/* :DIAGnostic:LOG:WRAP and :SLOG's: whether the event log wraps, by pllc,
 * the log's path word unread (0x3e9ec). */
int32_t scpi_set_DIAGnostic_LOG_WRAP(void* parameters, void* response,
                                     scpi_call* call) {
  const scpi_diagnostic_indexed_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_LOG_WRAP, 0, given->given.value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:LOG:WRAP?, sign-extended (0x3ea1c). */
int32_t scpi_query_DIAGnostic_LOG_WRAP(void* parameters, void* response,
                                       scpi_call* call) {
  int16_t* wrap = response;

  *wrap = (int8_t)nv_settings.log_wrap;
  return 0;
}

/* :DIAGnostic:LOG:READ:ALL? and :SLOG's: an empty response, and the log
 * printed to the session asking, DUART channel B's for source 0, else the
 * console, where the 58503A's prints to the console alone (0x3e920). */
int32_t scpi_query_DIAGnostic_LOG_READ_ALL(void* parameters, void* response,
                                           scpi_call* call) {
  const int16_t* log = parameters;
  scpi_token_string* text = response;

  text->length = 0;
  if ((int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCPI) {
    log_print(*log, (write_function*)duartb_port_write,
              (printf_function*)duartb_printf);
  } else {
    log_print(*log, console_write, (printf_function*)printf);
  }
  return 0;
}

/* :DIAGnostic:ADC?: an empty response, and the health monitor's averages
 * printed to the session asking, DUART channel B's for source 0, else the
 * console (0x3eab4). */
int32_t scpi_query_DIAGnostic_ADC(void* parameters, void* response,
                                  scpi_call* call) {
  scpi_token_string* text = response;

  text->length = 0;
  if ((int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCPI) {
    adc_averages_print(duartb_printf);
  } else {
    adc_averages_print((adc_average_printer*)printf);
  }
  return 0;
}

/* :DIAGnostic:GPSystem:ACURrent:STATe?'s choices, its response's list
 * (0x61e8a). */
enum {
  SCPI_DIAGNOSTIC_ACURRENT_MAX = 0,
  SCPI_DIAGNOSTIC_ACURRENT_MIN = 1,
  SCPI_DIAGNOSTIC_ACURRENT_NORM = 2,
  SCPI_DIAGNOSTIC_ACURRENT_ERR = 3,
};

/* The results :DIAGnostic:GPSystem:ACURrent:STATe? tests
 * hmon_check_antenna_current's for: 0 and 1, which it returns, and 0xff,
 * which it never does (0x3eb4e to 0x3eb6e). */
static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_NORMAL = 0;

static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_FAULT = 1;

static const uint8_t SCPI_DIAGNOSTIC_ANTENNA_LOW = 0xff;

/* :DIAGnostic:GPSystem:ACURrent:STATe?: NORM while
 * hmon_check_antenna_current finds no fault, MAX for a fault; MIN for
 * 0xff and ERR for any other result, which it never gives (0x3eb46). */
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

/* :DIAGnostic:ROSCillator:EFControl:ASLOPe, :BSLOPe and :DADC: the OCXO
 * calibration's a and b and the EFC's DAC counts per ADC count, each a
 * float or MIN or MAX, through its descriptor (0x3ef16, 0x3ef76,
 * 0x3efd6). */
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

/* Their queries, each value or its bound (0x3ef50, 0x3efb0, 0x3f010). */
int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_ASLOPe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* slope = response;

  *slope = scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_ASLOPe,
                            0, given->kind, given->bound);
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

int32_t scpi_query_DIAGnostic_ROSCillator_EFControl_DADC(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  float* ratio = response;

  *ratio = scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_EFControl_DADC,
                            0, given->kind, given->bound);
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:MODE: the time uncertainty test's mode,
 * by pllc (0x3f036). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_MODE(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_ROSCillator_PTESt_MODE, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:MODE?, sign-extended (0x3f066). */
int32_t scpi_query_DIAGnostic_ROSCillator_PTESt_MODE(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  int16_t* mode = response;

  *mode = (int8_t)scpi_value_DIAGnostic_ROSCillator_PTESt_MODE;
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:TYPE's choices, its list (0x61afa). */
enum {
  SCPI_DIAGNOSTIC_PTEST_MEAS = 0,
  SCPI_DIAGNOSTIC_PTEST_PRED = 1,
};

/* :DIAGnostic:ROSCillator:PTESt:TYPE MEAS or PRED: the uncertainty the
 * test takes, PTEST_TYPE_MEASURED for any choice but PRED, by pllc
 * (0x3f078). */
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

/* :DIAGnostic:ROSCillator:PTESt:TYPE? (0x3f0b6). */
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
 * MAX, through its descriptor (0x3f0d4). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_COUNt(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_PTESt_COUNt, 0,
                   given->kind, given->bound, 0, given->integer,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:COUNt?, or its bound, as a long
 * (0x3f10e). */
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

/* :DIAGnostic:ROSCillator:PTESt:PTHR: one of the test's two float
 * thresholds, by the path word, through its descriptor (0x3f13a). */
int32_t scpi_set_DIAGnostic_ROSCillator_PTESt_PTHR(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_diagnostic_indexed_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_PTESt_PTHR, given->index,
                   given->given.kind, given->given.bound,
                   given->given.real_bits, 0, scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:PTESt:PTHR?, or its bound (0x3f178). */
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
 * thresholds, by the path word, through its descriptor (0x3f1a2). */
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
 * (0x3f1e0). */
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

/* :DIAGnostic:ROSCillator:PTESt:STATe?'s first item's choices, its
 * response's list (0x61a86). */
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

/* :DIAGnostic:ROSCillator:PTESt:STATe?: the test's state, OFF for any
 * past PTEST_STATE_FAILED, and its passes (0x3f210, its jump table at
 * 0x3f232). */
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

/* :DIAGnostic:SYSTem:PDEBug: the loop's debug output on or off
 * (0x3f322). */
int32_t scpi_set_DIAGnostic_SYSTem_PDEBug(void* parameters, void* response,
                                          scpi_call* call) {
  const scpi_diagnostic_word* given = parameters;

  pll_debug_enabled = given->value != 0;
  return 0;
}

/* :DIAGnostic:TEMPerature?: an undefined header (0x3f3e8). */
int32_t scpi_query_DIAGnostic_TEMPerature(void* parameters, void* response,
                                          scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DIAGnostic:TEST:FLASh?: flash_test's result printed to the session
 * asking, DUART channel B's for source 0, else the console, then
 * :SYSTem:PON's action, by pllc; an empty response (0x3f582). */
int32_t scpi_query_DIAGnostic_TEST_FLASh(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_token_string* text = response;
  const request_source source = scpi_input_source(call->input);
  const int32_t result = flash_test();

  if ((int8_t)source == REQUEST_SOURCE_SCPI) {
    duartb_printf("%d\r\n", result);
  } else {
    printf("%d\r\n", result);
  }
  pllc_request_b(&scpi_action_SYSTem_PON, 0, source);
  text->length = 0;
  return 0;
}

/* :DIAGnostic:CALibration:ROSCillator: self-test 15, past selftest_table's,
 * this image's own, which gps_diag_sequence runs by stepping
 * efc_calibration_measure, by pllc (0x3f5e4). */
int32_t scpi_set_DIAGnostic_CALibration_ROSCillator(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  pllc_request_b(&scpi_action_self_test, SELFTEST_TEST_15,
                 scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:INPut:DATA?: the switch word's bits input_data_get gathers,
 * as an unsigned long (0x3f838). */
int32_t scpi_query_DIAGnostic_INPut_DATA(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_diagnostic_long* data = response;

  data->value = input_data_get();
  return 0;
}

/* :DIAGnostic:REFerence:EXTernal:QUEStionable:HYSTeresis: the external
 * reference's questionable hysteresis, a long or MIN or MAX, through its
 * descriptor, where the 58503A's is an undefined header (0x3f8bc). */
int32_t scpi_set_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(
      &scpi_param_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound, 0, given->integer,
      scpi_input_source(call->input));
  return 0;
}

/* Its query, or its bound, as an unsigned long (0x3f8f6). */
int32_t scpi_query_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis(
    void* parameters, void* response, scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_long* hysteresis = response;

  hysteresis->value = float_to_unsigned(scpi_numeric_get(
      &scpi_param_DIAGnostic_REFerence_EXTernal_QUEStionable_HYSTeresis, 0,
      given->kind, given->bound));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient, by pllc. */
int32_t scpi_set_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
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
