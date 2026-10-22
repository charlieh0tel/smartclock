/* The SCPI :SOURce:ROSCillator tree (docs/scpi/README.md): its handlers,
 * and the hooks and actions pllc runs for its settings, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/source_roscillator`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.
 *
 * The handlers' parameters are each parameter's kind word, then its
 * value; a number's, or a query's optional MINimum or MAXimum, is
 * scpi_engine_long_parameter's.  pll_mode's result, which several read,
 * is coded: 0 another stage, 1 a manual holdover, 2 holdover, 3
 * holdover recovery, 4 power-up, 5 locked, in startup or normal PLL
 * (0x45b70, its jump table at 0x45b8a). */

/* :HOLDover:DURation?'s response: the duration, the resolution word its
 * formatter reads after it, left as it is, and whether in holdover. */
typedef struct scpi_source_roscillator_duration {
  float duration;     /* +0x00: seconds */
  int16_t resolution; /* +0x04 */
  uint16_t holdover;  /* +0x06: 1 in holdover, else 0 */
} scpi_source_roscillator_duration;

_Static_assert(sizeof(scpi_source_roscillator_duration) == 8,
               "scpi_source_roscillator_duration");

/* pll_mode's result, read with interrupts masked. */
static uint8_t scpi_source_roscillator_mode(void) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t mode = pll_mode();

  set_interrupt_mask(sr);
  return mode;
}

/* :HOLDover:DURation?: the time in the present or last holdover, and
 * whether in one (097-z3801-01, table 4-2) (0x2fa12). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  scpi_source_roscillator_duration* answer = response;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  int8_t mode = pll_mode();
  float duration = unsigned_to_float(pll.holdover_duration);

  set_interrupt_mask(sr);
  answer->duration = duration;
  /* a manual holdover, holdover or holdover recovery,
   * PLL_MODE_MANUAL_HOLDOVER to PLL_MODE_RECOVERY */
  answer->holdover = mode >= 1 && mode <= 3;
  return 0;
}

/* :HOLDover:DURation:MEASurement:THReshold (0x2fa82). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero(call->input);

  scpi_numeric_set(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold, 0,
      given->kind, given->bound, 0, given->value, source);
  return 0;
}

/* :HOLDover:DURation:MEASurement:THReshold?, or its minimum or maximum,
 * in whole seconds (0x2fabc). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  int32_t* seconds = response;
  float value = scpi_numeric_get(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold, 0,
      given->kind, given->bound);

  *seconds = float_to_integer(value);
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold, the holdover duration that sets
 * :EXCeeded (097-z3801-01, table 4-2) (0x2fae8). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero(call->input);

  scpi_numeric_set(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold, 0,
      given->kind, given->bound, 0, given->value, source);
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold?, or its minimum or maximum, in
 * whole seconds (0x2fb22). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  int32_t* seconds = response;
  float value = scpi_numeric_get(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold, 0,
      given->kind, given->bound);

  *seconds = float_to_integer(value);
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold:ALARm1 and 3: not in this
 * firmware (0x2fb4e). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:DURation:STATus:THReshold:ALARm1? and 3?: not in this
 * firmware (0x2fb52). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:DURation:STATus:THReshold:EXCeeded?: whether the holdover
 * outlasted the threshold (097-z3801-01, table 4-2) (0x2fb88). */
int32_t
scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_EXCeeded(
    void* parameters, void* response, scpi_call* call) {
  int16_t* exceeded = response;

  *exceeded = (int8_t)holdover_duration_exceeded;
  return 0;
}

/* :HOLDover:HYSTeresis: not in this firmware (0x2fb9a). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:HYSTeresis?: not in this firmware (0x2fb9e). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:INITiate: requests a manual holdover, unless in one
 * (0x2fbc2). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_INITiate(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  /* PLL_MODE_MANUAL_HOLDOVER */
  if (scpi_source_roscillator_mode() != 1) {
    uint8_t source = return_zero(call->input);

    pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_INITiate, 0,
                   source);
  }
  return 0;
}

/* :HOLDover:LIMit:STATe: whether the TI limit holds over; the setting
 * keeps the reverse (0x2fc26). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_LIMit_STATe(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  scpi_word_parameter* given = parameters;
  uint8_t disabled = given->value == 0;
  uint8_t source = return_zero(call->input);

  pllc_request_a(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_STATe, 0,
                 disabled, source);
  return 0;
}

/* :HOLDover:LIMit:STATe? (0x2fc78). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_LIMit_STATe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  uint16_t* state = response;

  *state = nv_settings.holdover_limit_state == 0;
  return 0;
}

/* :HOLDover:LIMit:THReshold; the value's long is the float's bits
 * (0x2fca2). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_LIMit_THReshold(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero(call->input);

  scpi_numeric_set(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_THReshold, 0,
                   given->kind, given->bound, (uint32_t)given->value, 0,
                   source);
  return 0;
}

/* :HOLDover:LIMit:THReshold?, or its minimum or maximum (0x2fcdc). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_LIMit_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  float* threshold = response;

  *threshold =
      scpi_numeric_get(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_THReshold,
                       0, given->kind, given->bound);
  return 0;
}

/* :HOLDover:RECovery:AUTO (0x2fd02). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_AUTO(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  scpi_word_parameter* given = parameters;
  uint8_t source = return_zero(call->input);

  pllc_request_a(&scpi_param_SOURce_ROSCillator_HOLDover_RECovery_AUTO, 0,
                 (uint8_t)given->value, source);
  return 0;
}

/* :HOLDover:RECovery:AUTO? (0x2fd32). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_RECovery_AUTO(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  int16_t* state = response;

  *state = (int8_t)holdover_recovery_auto;
  return 0;
}

/* :HOLDover:RECovery:INITiate: requests the end of a manual holdover
 * (097-z3801-01, table 4-2) (0x2fd4c). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_INITiate(
    void* parameters, void* response, scpi_call* call) {
  uint8_t source = return_zero(call->input);

  pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_RECovery_INITiate, 0,
                 source);
  return 0;
}

/* :HOLDover:RECovery:LIMit:IGNore: requests recovery despite the TI
 * limit (097-z3801-01, table 4-2) (0x2fd84). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_LIMit_IGNore(
    void* parameters, void* response, scpi_call* call) {
  uint8_t source = return_zero(call->input);

  pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_RECovery_LIMit_IGNore,
                 0, source);
  return 0;
}

/* :HOLDover:WAITing?'s choices, the node's list (0x53ad0). */
enum {
  SCPI_SOURCE_ROSCILLATOR_WAITING_ERR = 0,
  SCPI_SOURCE_ROSCILLATOR_WAITING_GPS = 1,
  SCPI_SOURCE_ROSCILLATOR_WAITING_HARD = 2,
  SCPI_SOURCE_ROSCILLATOR_WAITING_LIM = 3,
  SCPI_SOURCE_ROSCILLATOR_WAITING_NONE = 4,
};

/* :HOLDover:WAITing?: why a holdover, not manual, waits to recover:
 * the hardware, GPS or the TI limit (097-z3801-01, table 4-2); none
 * otherwise (0x2feda). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_WAITing(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  uint16_t* choice = response;

  /* PLL_MODE_HOLDOVER */
  if (scpi_source_roscillator_mode() == 2) {
    uint16_t sr = set_interrupt_mask(CPU_IPL_1);
    uint8_t reason = holdover_waiting_reason();

    set_interrupt_mask(sr);
    switch (reason) {
      case HOLD_CAUSE_MANUAL:
        *choice = SCPI_SOURCE_ROSCILLATOR_WAITING_ERR;
        return 0;
      case HOLD_CAUSE_GPS:
        *choice = SCPI_SOURCE_ROSCILLATOR_WAITING_GPS;
        return 0;
      case HOLD_CAUSE_TI_ERROR:
      case HOLD_CAUSE_HARDWARE_FAILURE:
        *choice = SCPI_SOURCE_ROSCILLATOR_WAITING_HARD;
        return 0;
      case HOLD_CAUSE_TI_LIMIT_EXCEEDED:
        *choice = SCPI_SOURCE_ROSCILLATOR_WAITING_LIM;
        return 0;
      default:
        break;
    }
  }
  *choice = SCPI_SOURCE_ROSCILLATOR_WAITING_NONE;
  return 0;
}

/* :LIMit:THReshold, the TI limit that holds over (097-z3801-01, table
 * 4-2); the value's long is the float's bits (0x2ff78). */
int32_t scpi_set_SOURce_ROSCillator_LIMit_THReshold(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero(call->input);

  scpi_numeric_set(&scpi_param_SOURce_ROSCillator_LIMit_THReshold, 0,
                   given->kind, given->bound, (uint32_t)given->value, 0,
                   source);
  return 0;
}

/* :LIMit:THReshold?, or its minimum or maximum (0x2ffb2). */
int32_t scpi_query_SOURce_ROSCillator_LIMit_THReshold(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  float* threshold = response;

  *threshold = scpi_numeric_get(&scpi_param_SOURce_ROSCillator_LIMit_THReshold,
                                0, given->kind, given->bound);
  return 0;
}

/* [:STATe]?'s choices, the node's list (0x53b08). */
enum {
  SCPI_SOURCE_ROSCILLATOR_STATE_HOLD = 0,
  SCPI_SOURCE_ROSCILLATOR_STATE_LOCK = 1,
  SCPI_SOURCE_ROSCILLATOR_STATE_OTH = 2,
  SCPI_SOURCE_ROSCILLATOR_STATE_POW = 3,
  SCPI_SOURCE_ROSCILLATOR_STATE_REC = 4,
  SCPI_SOURCE_ROSCILLATOR_STATE_WAIT = 5,
};

/* [:STATe]?: the loop's state, by pll_mode (097-z3801-01, table 4-2)
 * (0x2ffd8). */
int32_t scpi_query_SOURce_ROSCillator_STATe(void* parameters, void* response,
                                            scpi_call* call) {
  uint16_t* choice = response;

  switch ((int8_t)scpi_source_roscillator_mode()) {
    case 1: /* PLL_MODE_MANUAL_HOLDOVER */
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_HOLD;
      break;
    case 2: /* PLL_MODE_HOLDOVER */
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_WAIT;
      break;
    case 3: /* PLL_MODE_RECOVERY */
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_REC;
      break;
    case 4: /* PLL_MODE_POWERUP */
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_POW;
      break;
    case 5: /* PLL_MODE_LOCKED */
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_LOCK;
      break;
    default:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_OTH;
      break;
  }
  return 0;
}

/* :ROSC:HOLDover:RECovery:INITiate: ends a manual holdover, else reports a
 * settings conflict (0x45a5c). */
void holdover_recovery_initiate(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual != 0) {
    pll.manual = 0;
    pll.hold_causes &= ~HOLD_CAUSE_MANUAL;
    status_happening(STATUS_WAITING_SET);
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, (uint8_t)source);
  }
}

/* :ROSC:HOLDover:INITiate: a manual holdover, past power-up (0x45aaa). */
void holdover_initiate(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP) {
    status_report_from(STATUS_SETTINGS_CONFLICT, (uint8_t)source);
    return;
  }
  pll.hold_causes |= HOLD_CAUSE_MANUAL;
  pll.manual = 1;
  pll_scpi_signal_delay = 1;
  if (pll.stage == PLL_STAGE_HOLDOVER) {
    status_happening(STATUS_HOLDING_SET);
  }
}

/* Ignores the TI limit in a holdover it alone caused, not manual, else
 * reports a settings conflict (0x45b0c). */
void holdover_recovery_limit_ignore(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual == 0 &&
      (pll.hold_causes & ~HOLD_CAUSE_TI_LIMIT_EXCEEDED) == HOLD_CAUSE_NONE) {
    pll.ti_limit_override = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, (uint8_t)source);
  }
}
