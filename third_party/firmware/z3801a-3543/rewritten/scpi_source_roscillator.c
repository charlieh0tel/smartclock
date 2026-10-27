/* scpi_source_roscillator.c's functions that are this image's own: the rest are
 * common/scpi_source_roscillator.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* :HOLDover:DURation?'s response: the duration, the resolution word its
 * formatter reads after it, left as it is, and whether in holdover. */
typedef struct scpi_source_roscillator_duration {
  float duration;     /* +0x00: seconds */
  int16_t resolution; /* +0x04 */
  uint16_t holdover;  /* +0x06: 1 in holdover, else 0 */
} scpi_source_roscillator_duration;

_Static_assert(sizeof(scpi_source_roscillator_duration) == 8,
               "scpi_source_roscillator_duration");

/* :HOLDover:DURation?: the time in the present or last holdover, and
 * whether in one (097-z3801-01, table 4-2) (0x2e918). */
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

/* :HOLDover:DURation:STATus:THReshold, the holdover duration that sets
 * :EXCeeded (097-z3801-01, table 4-2) (0x2e9ee). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  request_source source = return_zero(call->input);

  scpi_numeric_set(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold, 0,
      given->kind, given->bound, 0, given->value, source);
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold?, or its minimum or maximum, in
 * whole seconds (0x2ea28). */
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
 * firmware (0x2ea54). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:DURation:STATus:THReshold:ALARm1? and 3?: not in this
 * firmware (0x2ea58). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:DURation:STATus:THReshold:EXCeeded?: whether the holdover
 * outlasted the threshold (097-z3801-01, table 4-2) (0x2ea8e). */
int32_t
scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THReshold_EXCeeded(
    void* parameters, void* response, scpi_call* call) {
  int16_t* exceeded = response;

  *exceeded = (int8_t)holdover_duration_exceeded;
  return 0;
}

/* :HOLDover:LIMit:STATe? (0x2eb7e). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_LIMit_STATe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  uint16_t* state = response;

  *state = nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON;
  return 0;
}

/* :HOLDover:WAITing?'s choices, the node's list (0x53a6a). */
enum {
  SCPI_SOURCE_ROSCILLATOR_WAITING_ERR = 0,
  SCPI_SOURCE_ROSCILLATOR_WAITING_GPS = 1,
  SCPI_SOURCE_ROSCILLATOR_WAITING_HARD = 2,
  SCPI_SOURCE_ROSCILLATOR_WAITING_LIM = 3,
  SCPI_SOURCE_ROSCILLATOR_WAITING_NONE = 4,
};

/* :HOLDover:WAITing?: why a holdover, not manual, waits to recover:
 * the hardware, GPS or the TI limit (097-z3801-01, table 4-2); none
 * otherwise (0x2ede0). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_WAITing(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  uint16_t* choice = response;

  if (scpi_source_roscillator_mode() == PLL_MODE_HOLDOVER) {
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

/* :ROSC:HOLDover:RECovery:INITiate: ends a manual holdover, else reports a
 * settings conflict (0x45a94). */
void holdover_recovery_initiate(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual != 0) {
    pll.manual = 0;
    pll.hold_causes &= ~HOLD_CAUSE_MANUAL;
    status_happening(STATUS_WAITING_SET);
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* :ROSC:HOLDover:INITiate: a manual holdover, past power-up (0x45ae2). */
void holdover_initiate(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP) {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
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
 * reports a settings conflict (0x45b44). */
void holdover_recovery_limit_ignore(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual == 0 &&
      (pll.hold_causes & ~HOLD_CAUSE_TI_LIMIT_EXCEEDED) == HOLD_CAUSE_NONE) {
    pll.ti_limit_override = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}
