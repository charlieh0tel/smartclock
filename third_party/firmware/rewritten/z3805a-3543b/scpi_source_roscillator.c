/* The SCPI :SOURce:ROSCillator tree (docs/scpi/README.md): its handlers,
 * and the hooks and actions pllc runs for its settings, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/source_roscillator`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

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
