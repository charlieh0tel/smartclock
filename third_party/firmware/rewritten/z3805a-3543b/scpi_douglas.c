/* The SCPI :DOUGlas tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/douglas`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* :DOUGlas:DURation's changed hook: a duration set is not infinite
 * (0x308ae). */
void douglas_duration_changed(void) {
  nv_settings.douglas_duration_infinite = 0;
}

/* :DOUGlas:STATe's done hook: requests the survey stopped or started
 * (0x46340). */
void douglas_state_done(void) {
  douglas_stop_requested = nv_settings.douglas_state != 0;
  douglas_start_requested = nv_settings.douglas_state == 0;
}
