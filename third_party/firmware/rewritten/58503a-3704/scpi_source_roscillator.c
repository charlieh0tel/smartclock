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
 * whether in one (097-z3801-01, table 4-2) (0x2f756). */
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

/* :HOLDover:LIMit:STATe? (0x2fa48). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_LIMit_STATe(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  uint16_t* state = response;

  *state = nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON;
  return 0;
}

/* :HOLDover:WAITing?'s choices, the node's list (0x56a44). */
enum {
  SCPI_SOURCE_ROSCILLATOR_WAITING_ERR = 0,
  SCPI_SOURCE_ROSCILLATOR_WAITING_GPS = 1,
  SCPI_SOURCE_ROSCILLATOR_WAITING_HARD = 2,
  SCPI_SOURCE_ROSCILLATOR_WAITING_LIM = 3,
  SCPI_SOURCE_ROSCILLATOR_WAITING_NONE = 4,
};

/* :HOLDover:DURation:STATus:THReshold's parameter: the path word of its
 * numeric suffix, THReshold1 0, then the long or bound. */
typedef struct scpi_source_roscillator_threshold {
  int16_t threshold;                /* +0x00 */
  scpi_engine_long_parameter given; /* +0x02 */
} scpi_source_roscillator_threshold;

/* The one threshold the 58503A has, THReshold1's path word. */
static const int16_t SCPI_SOURCE_ROSCILLATOR_THRESHOLD_1 = 0;

/* :HOLDover:DURation:STATus:THReshold1, the holdover duration that sets
 * :EXCeeded; another suffix is an undefined header (0x2f82c). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THR(
    void* parameters, void* response, scpi_call* call) {
  const scpi_source_roscillator_threshold* given = parameters;

  if (given->threshold != SCPI_SOURCE_ROSCILLATOR_THRESHOLD_1) {
    status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
    return 0;
  }
  scpi_numeric_set(&scpi_param_SOURce_ROSCillator_HOLDover_DURation_STATus_THR,
                   0, given->given.kind, given->given.bound, 0,
                   given->given.value, return_zero(call->input));
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold1?, or its minimum or maximum, in
 * whole seconds; another suffix is an undefined header (0x2f88a). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THR(
    void* parameters, void* response, scpi_call* call) {
  const scpi_source_roscillator_threshold* given = parameters;
  int32_t* seconds = response;

  if (given->threshold != SCPI_SOURCE_ROSCILLATOR_THRESHOLD_1) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *seconds = float_to_integer(scpi_numeric_get(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_STATus_THR, 0,
      given->given.kind, given->given.bound));
  return 0;
}

/* :HOLDover:DURation:STATus:THReshold1:ACTion and its query: an
 * undefined header (0x2f8d0, 0x2f8f0). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THR1_ACTion(
    void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THR1_ACTion(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:DURation:STATus:THReshold1:ALARm1: not in this firmware,
 * the engine's undefined header as its result (0x2f902). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_STATus_THR1_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:DURation:STATus:THReshold1:ALARm1?: an undefined header
 * (0x2f906). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THR1_ALAR1(
    void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:DURation:STATus:THReshold1:EXCeeded?: whether the holdover
 * outlasted the threshold; another suffix is an undefined header
 * (0x2f93c). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_DURation_STATus_THR1_EXCeeded(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* threshold = parameters;
  int16_t* exceeded = response;

  if (*threshold != SCPI_SOURCE_ROSCILLATOR_THRESHOLD_1) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *exceeded = (int8_t)holdover_duration_exceeded;
  return 0;
}

/* :HOLDover:WAITing?: why a holdover, not manual, waits to recover:
 * the hardware, GPS or the TI limit; none otherwise (0x2fcaa).  It
 * switches on the cause's low byte alone, so that the 58503A's
 * HOLD_CAUSE_EFC_CONTROL, bit 8, is none. */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_WAITing(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  uint16_t* choice = response;

  if (scpi_source_roscillator_mode() == PLL_MODE_HOLDOVER) {
    uint16_t sr = set_interrupt_mask(CPU_IPL_1);
    uint8_t reason = holdover_waiting_reason_58503a();

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
 * settings conflict (0x45bce). */
void holdover_recovery_initiate(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual != 0) {
    pll.manual = 0;
    pll.hold_causes &= ~HOLD_CAUSE_MANUAL;
    status_happening(STATUS_WAITING_SET);
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* :ROSC:HOLDover:INITiate: a manual holdover, past power-up (0x45c76). */
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
 * reports a settings conflict (0x45ce0). */
void holdover_recovery_limit_ignore(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER && pll.manual == 0 &&
      (pll.hold_causes & ~HOLD_CAUSE_TI_LIMIT_EXCEEDED) == HOLD_CAUSE_NONE) {
    pll.ti_limit_override = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}
