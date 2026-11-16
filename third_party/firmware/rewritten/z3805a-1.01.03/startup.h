/* From the 58503B 1.01.04's rewritten/startup.h, copied for the Z3805A 1.01.03:
 * what its copied functions use.  Its comments' addresses are the
 * Z3805A 1.01.03's, but where they name another image. */

/* gpt_init's and its installer copy's body: sets the GPT up (MC68331UM
 * D.2): its interrupt arbitration, level and vectors, PWMA and PWMB
 * forced, port GP's directions and levels as given, the output compares
 * off, input capture 4, captures 1 and 2 on rising edges, the prescaler
 * and its interrupts, and the flags cleared. */
static void startup_gpt_init_both_pwm_ports(uint8_t directions,
                                            uint8_t levels) {
  static const uint16_t GPTMCR_ARBITRATION = 0xc;
  /* IPL 6 in bits 10 to 8, IVBA 5, vectors 0x50 to 0x5f (D.2.3). */
  static const uint16_t ICR_LEVEL_6_VECTORS_0X50 = 0x650;
  /* I4/O5, bit 10 of the PACTL and PACNT word (D.2.7). */
  static const uint8_t PACTL_INPUT_CAPTURE_4 = 0x04;
  /* EDGE2 and EDGE1 01, rising edges (D.2.11). */
  static const uint8_t TCTL2_CAPTURES_1_2_RISING = 0x05;
  /* CPR 3, TMSK2's prescaler select (D.2.12). */
  static const uint16_t TMSK_PRESCALER = 3;

  gpt.gptmcr = GPTMCR_ARBITRATION;
  gpt.icr = ICR_LEVEL_6_VECTORS_0X50;
  gpt.cforc |= CFORC_FPWMA | CFORC_FPWMB;
  gpt.ddrgp = directions;
  gpt.portgp = levels;
  gpt.oc1m = 0;
  gpt.tctl1 = 0;
  gpt.pactl = PACTL_INPUT_CAPTURE_4;
  gpt.tctl2 = TCTL2_CAPTURES_1_2_RISING;
  gpt.tmsk = TMSK_PRESCALER;
  (void)gpt.tflg;
  gpt.tflg = 0;
}

/* gpt_init's body: some of port GP outputs, the 58503A's. */
static void startup_gpt_init_both_pwm(void) {
  static const uint8_t DDRGP_OUTPUTS = 0xbc;
  static const uint8_t PORTGP_INITIAL = 0x80;

  startup_gpt_init_both_pwm_ports(DDRGP_OUTPUTS, PORTGP_INITIAL);
}
