/* What pllc and the SCPI task share (pllc.c, each image's own pllc.c,
 * scpi_requests.c): the event pllc signals the SCPI task with when a
 * request is done, a descriptor's flags, and the current descriptor and
 * its flags.  The exported C includes the hand-written headers after its
 * own declarations and before the hand-written .c files, so every one of
 * them can use it. */

/* The event the SCPI task's request_and_wait waits for. */
static const uint32_t PLLC_DONE_EVENT = 0x20;

/* The ticks request_and_wait's send_x_retry pauses between tries
 * (request_and_wait+0x10, request_and_wait+0x32). */
static const uint32_t PLLC_RETRY_TICKS = 5;

/* A descriptor's flags: the request goes to the GPS through
 * pll_scpi_send_to_loop, the value is saved in the NV register, and in
 * the EEPROM record at 0x400000. */
static const uint16_t PLLC_FLAG_SEND_TO_GPS = 1;

static const uint16_t PLLC_FLAG_SAVED_IN_NV = 2;

static const uint16_t PLLC_FLAG_SAVED_IN_RECORD = 4;

/* A descriptor of any type, as pll_scpi_param points at it: its first
 * byte says which. */
typedef union pllc_param {
  scpi_param_type type;
  scpi_numeric_param numeric;
  scpi_setting_param setting;
  scpi_action_param action;
} pllc_param;

static pllc_param* pllc_current(void) { return pll_scpi_param; }

/* The current descriptor's flags, a setting's at +0x14, any other's at
 * a numeric's +0x1e. */
static uint16_t pllc_flags(void) {
  pllc_param* param = pllc_current();

  return param->type == SCPI_PARAM_SETTING ? param->setting.flags
                                           : param->numeric.flags;
}
