/* The SCPI :SOURce:PTIMe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_ptime`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The time zone's changed hook: marks a change pending (0x45a24). */
void time_zone_changed(void) { time_zone_change_pending = 1; }

/* The time zone's done hook: refreshes the clock's record (0x45a2e). */
void time_zone_done(void) {
  clock_record_refresh();
  time_zone_change_pending = 0;
}

/* Requests a sync in power-up or holdover recovery, else reports a
 * settings conflict (0x45b42). */
void sync_immediate_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.sync_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, (uint8_t)source);
  }
}

/* Acknowledges a leap second, from holdover to normal PLL (0x47090). */
void leap_ack_request(int32_t index, int32_t source) {
  if (pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL) {
    pll.leap_ack_requested = 1;
  }
}
