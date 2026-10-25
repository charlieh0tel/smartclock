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
 * (0x45d44, its jump table at 0x45d5e). */

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
static pll_screen_mode scpi_source_roscillator_mode(void) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  pll_screen_mode mode = pll_mode();

  set_interrupt_mask(sr);
  return mode;
}

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

/* :HOLDover:DURation:MEASurement:THReshold (0x2f7c6). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  request_source source = return_zero(call->input);

  scpi_numeric_set(
      &scpi_param_SOURce_ROSCillator_HOLDover_DURation_MEASurement_THReshold, 0,
      given->kind, given->bound, 0, given->value, source);
  return 0;
}

/* :HOLDover:DURation:MEASurement:THReshold?, or its minimum or maximum,
 * in whole seconds (0x2f800). */
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

/* :HOLDover:HYSTeresis: not in this firmware (0x2f96a). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:HYSTeresis?: not in this firmware (0x2f96e). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :HOLDover:INITiate: requests a manual holdover, unless in one
 * (0x2f992). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_INITiate(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  if (scpi_source_roscillator_mode() != PLL_MODE_HOLDOVER_MANUAL) {
    request_source source = return_zero(call->input);

    pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_INITiate, 0,
                   source);
  }
  return 0;
}

/* :HOLDover:LIMit:STATe: whether the TI limit holds over; the setting
 * keeps the reverse (0x2f9f6). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_LIMit_STATe(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  scpi_word_parameter* given = parameters;
  holdover_limit_setting setting =
      given->value == 0 ? HOLDOVER_LIMIT_OFF : HOLDOVER_LIMIT_ON;
  request_source source = return_zero(call->input);

  pllc_request_a(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_STATe, 0,
                 setting, source);
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

/* :HOLDover:LIMit:THReshold; the value's long is the float's bits
 * (0x2fa72). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_LIMit_THReshold(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  request_source source = return_zero(call->input);

  scpi_numeric_set(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_THReshold, 0,
                   given->kind, given->bound, (uint32_t)given->value, 0,
                   source);
  return 0;
}

/* :HOLDover:LIMit:THReshold?, or its minimum or maximum (0x2faac). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_LIMit_THReshold(
    void* parameters, void* response, scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  float* threshold = response;

  *threshold =
      scpi_numeric_get(&scpi_param_SOURce_ROSCillator_HOLDover_LIMit_THReshold,
                       0, given->kind, given->bound);
  return 0;
}

/* :HOLDover:RECovery:AUTO (0x2fad2). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_AUTO(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  scpi_word_parameter* given = parameters;
  request_source source = return_zero(call->input);

  pllc_request_a(&scpi_param_SOURce_ROSCillator_HOLDover_RECovery_AUTO, 0,
                 (uint8_t)given->value, source);
  return 0;
}

/* :HOLDover:RECovery:AUTO? (0x2fb02). */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_RECovery_AUTO(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  int16_t* state = response;

  *state = (int8_t)holdover_recovery_auto;
  return 0;
}

/* :HOLDover:RECovery:INITiate: requests the end of a manual holdover
 * (097-z3801-01, table 4-2) (0x2fb1c). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_INITiate(
    void* parameters, void* response, scpi_call* call) {
  request_source source = return_zero(call->input);

  pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_RECovery_INITiate, 0,
                 source);
  return 0;
}

/* :HOLDover:RECovery:LIMit:IGNore: requests recovery despite the TI
 * limit (097-z3801-01, table 4-2) (0x2fb54). */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_RECovery_LIMit_IGNore(
    void* parameters, void* response, scpi_call* call) {
  request_source source = return_zero(call->input);

  pllc_request_b(&scpi_action_SOURce_ROSCillator_HOLDover_RECovery_LIMit_IGNore,
                 0, source);
  return 0;
}

/* :LIMit:THReshold, the TI limit that holds over (097-z3801-01, table
 * 4-2); the value's long is the float's bits (0x2fd48). */
int32_t scpi_set_SOURce_ROSCillator_LIMit_THReshold(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  request_source source = return_zero(call->input);

  scpi_numeric_set(&scpi_param_SOURce_ROSCillator_LIMit_THReshold, 0,
                   given->kind, given->bound, (uint32_t)given->value, 0,
                   source);
  return 0;
}

/* :LIMit:THReshold?, or its minimum or maximum (0x2fd82). */
int32_t scpi_query_SOURce_ROSCillator_LIMit_THReshold(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  float* threshold = response;

  *threshold = scpi_numeric_get(&scpi_param_SOURce_ROSCillator_LIMit_THReshold,
                                0, given->kind, given->bound);
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

/* [:STATe]?'s choices, the node's list (0x56ab4). */
enum {
  SCPI_SOURCE_ROSCILLATOR_STATE_HOLD = 0,
  SCPI_SOURCE_ROSCILLATOR_STATE_LOCK = 1,
  SCPI_SOURCE_ROSCILLATOR_STATE_OTH = 2,
  SCPI_SOURCE_ROSCILLATOR_STATE_POW = 3,
  SCPI_SOURCE_ROSCILLATOR_STATE_REC = 4,
  SCPI_SOURCE_ROSCILLATOR_STATE_WAIT = 5,
};

/* [:STATe]?: the loop's state, by pll_mode (097-z3801-01, table 4-2)
 * (0x2fda8). */
int32_t scpi_query_SOURce_ROSCillator_STATe(void* parameters, void* response,
                                            scpi_call* call) {
  uint16_t* choice = response;

  switch ((int8_t)scpi_source_roscillator_mode()) {
    case PLL_MODE_HOLDOVER_MANUAL:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_HOLD;
      break;
    case PLL_MODE_HOLDOVER:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_WAIT;
      break;
    case PLL_MODE_RECOVERY:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_REC;
      break;
    case PLL_MODE_POWERUP:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_POW;
      break;
    case PLL_MODE_LOCKED:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_LOCK;
      break;
    default:
      *choice = SCPI_SOURCE_ROSCILLATOR_STATE_OTH;
      break;
  }
  return 0;
}
