/* pllc's side of the SCPI settings, the firmware's own "pll_scpi"
 * (finish_scpi_cmd's message): the server that takes a setter's request
 * from the pllc exchange, validates it by its descriptor, sends what the
 * GPS must hear and stores and saves the rest, and the hooks and actions that
 * no one command owns (docs/firmware/structures.md, "Numeric SCPI parameters"
 * and "The pllc task's SCPI requests").  A command's own hooks and actions are
 * in its tree's file (scpi_source_ptime.c and the others), and the descriptors'
 * validators scpi_validators.c's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); the exported C includes this
 * file after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

/* A descriptor's flags: the request goes to the GPS through
 * pll_scpi_send_to_loop, the value is saved in the NV register, and in
 * the EEPROM record at 0x400000. */
static const uint16_t PLLC_FLAG_SEND_TO_GPS = 1;

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

typedef void pllc_setting_done_hook(int32_t index, int32_t source);

typedef void pllc_numeric_done_hook(void);

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe 0 from 0 instead logs position hold (0x42db0). */
uint32_t pll_scpi_needs_loop(void) {
  if ((pllc_flags() & PLLC_FLAG_SEND_TO_GPS) != 0) {
    if (pll_scpi_setting == 0 || pll_scpi_param != &scpi_param_DOUGlas_STATe) {
      return 1;
    }
    if (nv_settings.douglas_state == 0) {
      douglas_log_event = LOG_POSITION_HOLD_STARTED;
    }
  }
  return 0;
}

/* nv_settings.field_be's done hook, which does nothing (0x45d42). */
void nv_param_field_b8_done(void) {}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause, with the TI limit overridden, which the Z3805A's does not
 * set (0x45cbc). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll.ti_limit_override = 1;
  pll_scpi_signal_delay = 1;
}
