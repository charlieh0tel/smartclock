/* C's wrapper of the status register, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/cpu`): it enters supervisor
 * mode with enter_supervisor, trap #7, which returns the status register
 * as it was, and moves a new one in.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (0x13368). */
uint16_t enter_supervisor_mode(void) {
  const uint16_t sr = enter_supervisor();

  cpu_sr_write(sr | CPU_IPL_7);
  return sr;
}
