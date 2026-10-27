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
