/* What the start-up shares with the installer's copy of it (startup.c,
 * installer_startup.c): the bodies of the routines that set the
 * MC68331's modules up, which both copies call.  The exported C
 * includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* qsm_init's and its installer copy's body: sets the QSM up (MC68331UM D.4):
 * its interrupt arbitration, the QSPI's and the SCI's interrupt levels and
 * vector, and port QS, every pin but SCK the QSPI's, all outputs but MISO,
 * high. */
static void startup_qsm_init(void) {
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

/* gpt_init's and its installer copy's body: sets the GPT up (MC68331UM D.2):
 * its interrupt arbitration, level and vectors, PWMA forced, port GP, the
 * output compares off, input capture 4, captures 1 and 2 on rising edges, the
 * prescaler and its interrupts, and the flags cleared. */
static void startup_gpt_init(void) {
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

/* periodic_interrupt_enable's and its installer copy's body: sets the
 * SIM's PICR: the periodic interrupt at level 5, or at level 0,
 * off. */
static void startup_periodic_interrupt_enable(char enable) {
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
