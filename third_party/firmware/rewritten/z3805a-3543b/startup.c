/* The start-up's path, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): primary_start, its no-op
 * hook, the periodic interrupt's enable and the shutdown.  The
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

/* Sets the QSM up (MC68331UM D.4): its interrupt arbitration, the
 * QSPI's and the SCI's interrupt levels and vector, and port QS, every
 * pin but SCK the QSPI's, all outputs but MISO, high (0x12026). */
void qsm_init(void) {
  static const uint16_t QSMCR_ARBITRATION = 8;
  /* QILR, the high byte: ILQSPI 4 in bits 13 to 11, ILSCI 3 in bits 10
   * to 8; QIVR, the low: vector 64, the SCI's, the QSPI's 65 (D.4.3). */
  static const uint16_t QILR_QIVR = 0x2340;
  static const uint8_t PORTQS_HIGH = 0xff;
  static const uint8_t DDRQS_ALL_BUT_MISO = 0xfe;
  /* PQSPA0, 1 and 3 to 6: MISO, MOSI and PCS0 to PCS3 (Table D-14). */
  static const uint8_t PQSPAR_QSPI_PINS = 0x7b;

  qsm.qsmcr = QSMCR_ARBITRATION;
  /* The image writes QILR and QIVR as one word. */
  *(volatile uint16_t*)&qsm.qilr = QILR_QIVR;
  qsm.portqs = PORTQS_HIGH;
  qsm.ddrqs = DDRQS_ALL_BUT_MISO;
  qsm.pqspar = PQSPAR_QSPI_PINS;
}

/* Sets the GPT up (MC68331UM D.2): its interrupt arbitration, level and
 * vectors, PWMA forced, port GP, the output compares off, input capture
 * 4, captures 1 and 2 on rising edges, the prescaler and its
 * interrupts, and the flags cleared (0x12050). */
void gpt_init(void) {
  static const uint16_t GPTMCR_ARBITRATION = 0xc;
  /* IPL 6 in bits 10 to 8, IVBA 5, vectors 0x50 to 0x5f (D.2.3). */
  static const uint16_t ICR_LEVEL_6_VECTORS_0X50 = 0x650;
  static const uint8_t DDRGP_OUTPUTS = 0xbc;
  static const uint8_t PORTGP_INITIAL = 0x80;
  /* I4/O5, bit 10 of the PACTL and PACNT word (D.2.7). */
  static const uint8_t PACTL_INPUT_CAPTURE_4 = 0x04;
  /* EDGE2 and EDGE1 01, rising edges (D.2.11). */
  static const uint8_t TCTL2_CAPTURES_1_2_RISING = 0x05;
  /* CPR 3, TMSK2's prescaler select (D.2.12). */
  static const uint16_t TMSK_PRESCALER = 3;

  gpt.gptmcr = GPTMCR_ARBITRATION;
  gpt.icr = ICR_LEVEL_6_VECTORS_0X50;
  gpt.cforc |= CFORC_FPWMA;
  gpt.ddrgp = DDRGP_OUTPUTS;
  gpt.portgp = PORTGP_INITIAL;
  gpt.oc1m = 0;
  gpt.tctl1 = 0;
  gpt.pactl = PACTL_INPUT_CAPTURE_4;
  gpt.tctl2 = TCTL2_CAPTURES_1_2_RISING;
  gpt.tmsk = TMSK_PRESCALER;
  (void)gpt.tflg;
  gpt.tflg = 0;
}

/* Does nothing; primary_start calls it between gpt_init and system_init
 * (0x12bb2). */
void start_nothing(void) {}

/* Sets the SIM's PICR: the periodic interrupt at level 5, or at level 0,
 * off (0x12004). */
void periodic_interrupt_enable(char enable) {
  /* The periodic interrupt, primary_user_66, 1024 a second: its
   * vector, 66, and its level (docs/firmware/structures.md). */
  static const uint16_t PERIODIC_VECTOR = 0x42;
  static const uint16_t PERIODIC_LEVEL = 5;

  if (enable) {
    sim.picr = PERIODIC_LEVEL << PICR_PIRQL_SHIFT | PERIODIC_VECTOR;
  } else {
    sim.picr = PERIODIC_VECTOR;
  }
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
