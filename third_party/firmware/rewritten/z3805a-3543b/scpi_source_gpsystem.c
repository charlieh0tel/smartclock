/* The SCPI :SOURce:GPSystem tree (docs/scpi/README.md): its handlers,
 * and the hooks and actions pllc runs for its settings, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/source_gpsystem`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* The antenna delay's done hook: clears the temporary-mode hold cause
 * (0x45b02). */
void antenna_delay_done(void) { pll.hold_causes &= ~HOLD_CAUSE_TEMPORARY_MODE; }

/* Requests the survey finished, and the source to save with (0x46368). */
void douglas_finish_request(int32_t index, int32_t source) {
  douglas_finish_requested = 1;
  douglas_finish_nv_argument = (uint8_t)source;
}
