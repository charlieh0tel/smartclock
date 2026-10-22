/* The start-up's path, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): primary_start, its no-op
 * hook, the periodic interrupt's enable and the shutdown; startup.h
 * holds the bodies the installer's copies share.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The reset's path: the exception hook cleared, the watchdog serviced, the
 * QSM and GPT set up, system_init, and the kernel started with trap #6
 * (0x12d6e). */
void primary_start(uint32_t a, uint32_t b, uint32_t c) {
  exception_hook_reset();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  qsm_init();
  gpt_init();
  start_nothing();
  system_init();
  psos_config_init();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  primary_trap_6();
}

/* Sets the QSM up (0x12026). */
void qsm_init(void) { startup_qsm_init(); }

/* Sets the GPT up (0x12050). */
void gpt_init(void) { startup_gpt_init(); }

/* Does nothing; primary_start calls it between gpt_init and system_init
 * (0x12bb2). */
void start_nothing(void) {}

/* Sets the periodic interrupt on or off (0x12004). */
void periodic_interrupt_enable(char enable) {
  startup_periodic_interrupt_enable(enable);
}

/* Stops the interrupts and the serial devices: the periodic interrupt, the
 * GPT's interrupts but its prescaler, the QSPI, whose status is read
 * before it is cleared, the SCI's receiver and transmitter, and the DUART
 * (0x120aa). */
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

/* Enters the installer with trap #11, which does not come back
 * (restart.md); scpi_task calls it for :SYSTem:LANGuage (0x12f2c). */
void enter_installer(void) { __asm__ volatile("trap #11"); }
