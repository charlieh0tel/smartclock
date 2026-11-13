/* The SCPI :OUTPut tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/output`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  No manual lists :OUTPut for the Z3801A
 * (097-z3801-01, table 4-2); this firmware keeps :STATe, which it does
 * nothing with, and each image's own scpi_output.c holds the pins'
 * handlers, which refuse their settings. */

/* Programs the output's state; this firmware does nothing. */
void output_state_program(uint8_t state) {}
