/* duartb_applied_settings and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Channel B's serial settings as last applied to the DUART, at
 * duartb_applied: copies of duartb_settings', which
 * duartb_baud_from_settings and duartb_mode_from_settings program. */
typedef struct duartb_applied_settings {
  uint8_t baud;
  uint8_t parity;
  uint8_t eight_bits; /* +0x02: else seven, and duartb_isr strips the top bit */
  uint8_t two_stop_bits; /* +0x03: else one */
} duartb_applied_settings;

/* What the code last wrote to the 68681's write-only registers, at
 * duart_shadow, so that it can change some bits of one and write it whole;
 * their bits are mc68681.h's. */
typedef struct duart_shadow_registers {
  uint8_t mr1a;
  uint8_t mr2a;
  uint8_t csra;
  uint8_t acr;
  uint8_t imr; /* +0x04: 0x10 channel B's transmitter, 0x20 its receiver */
  uint8_t ctur;
  uint8_t ctlr;
  uint8_t mr1b;
  uint8_t mr2b;
  uint8_t csrb;
  uint8_t opcr;
  uint8_t output_reset; /* +0x0b: the output bits reset, the failed self-tests'
                           lamps */
  uint8_t field_0c;     /* +0x0c: duart_init zeroes it, and nothing sets it;
                           the Z3805A's duart_output_bit_set keeps the bits in
                           output_reset while it is set, else in
                           selftest.lamp_mask */
} duart_shadow_registers;
