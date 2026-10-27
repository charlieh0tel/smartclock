/* The functions of duart.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/duart.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* An input port bit as 0 or 1, read until ten reads in a row after the
 * first agree (MC68681 4.2.12, IP). */
static uint8_t duart_input_debounced(uint8_t bit) {
  static const int32_t AGREEING_READS = 10;
  uint8_t level = duart.ip_opcr & bit;

  for (int32_t agreeing = 0; agreeing < AGREEING_READS;) {
    const uint8_t read = duart.ip_opcr & bit;

    if (read == level) {
      agreeing++;
    } else {
      agreeing = 0;
      level = read;
    }
  }
  return level != 0;
}

/* The input port's bit 0, debounced; nothing calls it. */
uint8_t duart_input_0_debounced(void) {
  /* The input port's bit 0. */
  static const uint8_t IP0 = 0x01;

  return duart_input_debounced(IP0);
}

/* The input port's bit 1, debounced; nothing calls it. */
uint8_t duart_input_1_debounced(void) {
  /* The input port's bit 1. */
  static const uint8_t IP1 = 0x02;

  return duart_input_debounced(IP1);
}

/* The input port's bit 2, debounced; nothing calls it. */
uint8_t duart_input_2_debounced(void) {
  /* The input port's bit 2. */
  static const uint8_t IP2 = 0x04;

  return duart_input_debounced(IP2);
}

/* Stops both DUART channels and masks its interrupts. */
void duart_stop(void) {
  duart.a.cr = CR_RX_DISABLE;
  duart.a.cr = CR_TX_DISABLE;
  duart.b.cr = CR_RX_DISABLE;
  duart.b.cr = CR_TX_DISABLE;
  duart.isr_imr = 0;
}
