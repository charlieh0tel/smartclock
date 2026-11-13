/* The start-up's path, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): the QSM's setup,
 * primary_start's no-op hook, the periodic interrupt's enable and the
 * shutdown; primary_start is each image's own startup.c's; startup.h
 * holds the bodies the installer's copies share.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Sets the QSM up. */
void qsm_init(void) { startup_qsm_init(); }

/* Does nothing; primary_start calls it between gpt_init and system_init. */
void start_nothing(void) {}

/* Sets the periodic interrupt on or off. */
void periodic_interrupt_enable(char enable) {
  startup_periodic_interrupt_enable(enable);
}

/* Stops the interrupts and the serial devices: the periodic interrupt, the
 * GPT's interrupts but its prescaler, the QSPI, whose status is read
 * before it is cleared, the SCI's receiver and transmitter, and the DUART. */
void shutdown(void) {
  periodic_interrupt_enable(0);
  gpt.tmsk &= TMSK_CPROUT | TMSK_CPR_MASK;
  qsm.spcr1 = 0;
  qsm.spcr2 = 0;
  qsm.spcr3 = 0;
  (void)qsm.spsr;
  qsm.spsr = 0;
  qsm.sccr1 = 0;
  duart_stop();
}
