/* The SCPI :OUTPut tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/output`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Programs the output's state; this firmware does nothing (0x42b18). */
void output_state_program(uint8_t state) {}

/* :OUTPut:STATe's done hook (0x42b7c). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }
