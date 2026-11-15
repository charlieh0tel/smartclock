/* The 58503B 1.01.04's own EFC writes, the 58503A 3704's rewritten/dac.c's
 * in their shape (docs/firmware/58503b-1.01.04.md, "The link list"): the
 * DAC's word goes out through the QSPI, then PE7 is pulsed and the byte at
 * cs7_window read, where the 58503A's writes its strobe; and the dithered
 * write takes the USER mode's EFC while :DIAGnostic:ROSCillator:EFControl:
 * MODE is not NORMal.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The value to the EFC's DAC through qspi_write, the argument's own slot
 * the buffer; then port E's bit 7 pulsed low and the byte at cs7_window
 * read into that slot, which no caller reads (0x2dba0). */
void efc_write(uint16_t value) {
  static const uint8_t PORTE_PE7 = 0x80;

  qspi_write(QSPI_EFC, &value, 1);
  sim.porte0 &= ~PORTE_PE7;
  sim.porte0 |= PORTE_PE7;
  *(volatile uint16_t*)&value = cs7_window;
}

/* Writes pll.efc, rounded, or with :DIAGnostic:ROSCillator:EFControl:MODE
 * not NORMal the USER mode's EFC, plus random_dither, clamped to 0 to
 * 0xffff0, to the EFC's DAC; the 58503A's writes pll.efc in any mode
 * (0x32cd0). */
void efc_write_dithered(void) {
  /* The EFC's range: 20 bits, its DAC taking the top 16. */
  static const int32_t EFC_MOST = 0xffff0;
  static const int32_t EFC_DAC_SHIFT = 4;
  int32_t efc = diag_efc_mode == EFC_MODE_NORMAL
                    ? double_round_to_long(float_to_double(pll.efc))
                    : scpi_value_DIAGnostic_ROSCillator_EFControl_DATA;

  if (efc >= 0 && efc <= EFC_MOST) {
    efc += random_dither();
  }
  if (efc < 0) {
    efc = 0;
  } else if (efc > EFC_MOST) {
    efc = EFC_MOST;
  }
  efc_write(efc >> EFC_DAC_SHIFT);
}

/* The hex DAC: six 6-bit channels, two to each of hdac_shadow's three
 * words, sent to QSPI device 2. */
static const uint8_t DAC_HDAC_CHANNELS = 6;

static const uint8_t DAC_HDAC_BITS = 6;

static const uint8_t DAC_HDAC_MASK = 0x3f;

static const uint8_t DAC_HDAC_WORDS = 3;

/* The hdac_write word's: a value into channel 1 to 6, slot 6 - channel,
 * or slot 3 for any other. */
void hdac_write(uint8_t value, uint8_t channel) {
  static const uint8_t HDAC_DEFAULT_SLOT = 3; /* a channel out of range */
  uint8_t slot = HDAC_DEFAULT_SLOT;

  if (channel >= 1 && channel <= DAC_HDAC_CHANNELS) {
    slot = DAC_HDAC_CHANNELS - channel;
  }
  hdac_set(value, slot);
}

/* A 6-bit value into slot 0 to 5 of hdac_shadow, the high half of
 * word slot / 2 for an even slot, the low for an odd, and the three
 * words to the DAC. */
void hdac_set(uint8_t value, uint8_t slot) {
  if (slot < DAC_HDAC_CHANNELS) {
    uint16_t* word = &hdac_shadow[slot >> 1];

    if (slot % 2 == 0) {
      *word &= DAC_HDAC_MASK;
      *word |= (value & DAC_HDAC_MASK) << DAC_HDAC_BITS;
    } else {
      *word &= DAC_HDAC_MASK << DAC_HDAC_BITS;
      *word |= value & DAC_HDAC_MASK;
    }
  }
  qspi_write(QSPI_HDAC, hdac_shadow, DAC_HDAC_WORDS);
  psos_pause(1);
}

/* Two channels' values as one of hdac_shadow's words. */
static uint16_t dac_hdac_word(uint8_t low, uint8_t high) {
  return (low & DAC_HDAC_MASK) | (high & DAC_HDAC_MASK) << DAC_HDAC_BITS;
}

/* The hdac_all word's: all six channels at once. */
void hdac_write_all(uint8_t channel_1, uint8_t channel_2, uint8_t channel_3,
                    uint8_t channel_4, uint8_t channel_5, uint8_t channel_6) {
  hdac_shadow[0] = dac_hdac_word(channel_5, channel_6);
  hdac_shadow[1] = dac_hdac_word(channel_3, channel_4);
  hdac_shadow[2] = dac_hdac_word(channel_1, channel_2);
  qspi_write(QSPI_HDAC, hdac_shadow, DAC_HDAC_WORDS);
  psos_pause(1);
}

/* Sends hdac_shadow to the DAC by polling. */
void hdac_update(void) { qspi_send(QSPI_HDAC, hdac_shadow, DAC_HDAC_WORDS); }
