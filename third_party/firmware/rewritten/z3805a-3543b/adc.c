/* The ADC and the DACs on the QSPI, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): adc_read
 * and the read_adc_* scalings, the HDACs (hdac_*), the EFC's DAC
 * (efc_write), and the EFC's dither and clamp.  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype.  The
 * EFC's clamp state is docs/firmware/structures.md's "The health
 * monitor's supplies and EFC". */

/* The QSPI's devices, by the minor number of I/O device 1, the QSPI
 * (qspi_queue_setup's cases). */
enum {
  ADC_QSPI_CONVERTER = 1,
  ADC_QSPI_HDAC = 2,
  ADC_QSPI_EFC = 7,
};

/* Reads an ADC channel through the QSPI (0x1f2c0). */
int32_t adc_read(adc_channel channel) {
  /* The ADC's command word, as adc_read builds it (0x1f2ce), and its
   * reply, the reading in bits 9 to 2. */
  static const uint16_t COMMAND = 0xc000;
  static const uint16_t CHANNEL_SHIFT = 11;
  static const uint16_t REPLY_SHIFT = 2;
  uint16_t command = channel << CHANNEL_SHIFT | COMMAND;
  uint16_t reply;
  qspi_buffers buffers = {&command, &reply};

  qspi_read(ADC_QSPI_CONVERTER, &buffers, 1);
  return reply >> REPLY_SHIFT;
}

/* A channel's reading times a scale, of its low byte. */
static float adc_scaled(adc_channel channel, float scale) {
  return float_multiply(unsigned_to_float((uint8_t)adc_read(channel)), scale);
}

/* A channel's reading times a scale, corrected by the 5 V supply's
 * reading times another: both channels read first. */
static float adc_against_5v(adc_channel channel, float scale, float scale_5v) {
  uint8_t reading = adc_read(channel);
  uint8_t reading_5v = adc_read(ADC_5V);
  float value = float_multiply(unsigned_to_float(reading), scale);

  return float_add(value,
                   float_multiply(unsigned_to_float(reading_5v), scale_5v));
}

/* The ADC's scales, the image's own constants; the ovens' here, the
 * others in the functions that read them. */
static const float ADC_OVEN_SCALE = 0.0743f;
static const float ADC_OVEN_5V_SCALE = -0.0472f;

/* The temperature, "Temperature: %f" (0x1f308). */
float read_adc_temp(void) {
  static const float TEMPERATURE_SCALE = 0.273f;

  return adc_scaled(ADC_TEMPERATURE, TEMPERATURE_SCALE);
}

/* The 5 V supply, "5V reading: %f" (0x1f32e). */
float read_adc_5v(void) {
  static const float ADC_5V_SCALE = 0.0242f;

  return adc_scaled(ADC_5V, ADC_5V_SCALE);
}

/* The double oven, "Double oven: %f" (0x1f356). */
float read_adc_doven(void) {
  return adc_against_5v(ADC_DOUBLE_OVEN, ADC_OVEN_SCALE, ADC_OVEN_5V_SCALE);
}

/* The oven, "Oven: %f" (0x1f3ae). */
float read_adc_oven(void) {
  return adc_against_5v(ADC_OVEN, ADC_OVEN_SCALE, ADC_OVEN_5V_SCALE);
}

/* The +15 V supply, "+15V reading: %f" (0x1f406). */
float read_adc_p15v(void) {
  static const float PLUS_15V_SCALE = 0.0763f;

  return adc_scaled(ADC_PLUS_15V, PLUS_15V_SCALE);
}

/* The -15 V supply, "-15V reading: %f" (0x1f42e). */
float read_adc_m15v(void) {
  static const float MINUS_15V_SCALE = 0.0438f;
  static const float MINUS_15V_5V_SCALE = -0.084f;

  return adc_against_5v(ADC_MINUS_15V, MINUS_15V_SCALE, MINUS_15V_5V_SCALE);
}

/* Channel 3's current, 250/255 a count. */
static float adc_current(void) {
  static const float CURRENT_FULL_SCALE = 250.0f;
  static const float FULL_SCALE = 255.0f;

  return float_divide(adc_scaled(ADC_CURRENT, CURRENT_FULL_SCALE), FULL_SCALE);
}

/* :DIAG:ROSC:CURR?'s reading, channel 3's current (0x1f486). */
float read_adc_osc_curr(void) { return adc_current(); }

/* The antenna current, "Antenna current: %f", the same channel
 * (0x1f4ba). */
float read_adc_ant_curr(void) { return adc_current(); }

/* The hex DAC: six 6-bit channels, two to each of hdac_shadow's three
 * words, sent to QSPI device 2. */
static const uint8_t ADC_HDAC_CHANNELS = 6;
static const uint8_t ADC_HDAC_BITS = 6;
static const uint8_t ADC_HDAC_MASK = 0x3f;
static const uint8_t ADC_HDAC_WORDS = 3;

/* The hdac_write word's: a value into channel 1 to 6, slot 6 - channel,
 * or slot 3 for any other (0x1f4ee). */
void hdac_write(uint8_t value, uint8_t channel) {
  static const uint8_t HDAC_DEFAULT_SLOT = 3; /* a channel out of range */
  uint8_t slot = HDAC_DEFAULT_SLOT;

  if (channel >= 1 && channel <= ADC_HDAC_CHANNELS) {
    slot = ADC_HDAC_CHANNELS - channel;
  }
  hdac_set(value, slot);
}

/* A 6-bit value into slot 0 to 5 of hdac_shadow, the high half of
 * word slot / 2 for an even slot, the low for an odd, and the three
 * words to the DAC (0x1f53a). */
void hdac_set(uint8_t value, uint8_t slot) {
  if (slot < ADC_HDAC_CHANNELS) {
    uint16_t* word = &hdac_shadow[slot >> 1];

    if (slot % 2 == 0) {
      *word &= ADC_HDAC_MASK;
      *word |= (value & ADC_HDAC_MASK) << ADC_HDAC_BITS;
    } else {
      *word &= ADC_HDAC_MASK << ADC_HDAC_BITS;
      *word |= value & ADC_HDAC_MASK;
    }
  }
  qspi_write(ADC_QSPI_HDAC, hdac_shadow, ADC_HDAC_WORDS);
  psos_pause(1);
}

/* Two channels' values as one of hdac_shadow's words. */
static uint16_t adc_hdac_word(uint8_t low, uint8_t high) {
  return (low & ADC_HDAC_MASK) | (high & ADC_HDAC_MASK) << ADC_HDAC_BITS;
}

/* The hdac_all word's: all six channels at once (0x1f5bc). */
void hdac_write_all(uint8_t channel_1, uint8_t channel_2, uint8_t channel_3,
                    uint8_t channel_4, uint8_t channel_5, uint8_t channel_6) {
  hdac_shadow[0] = adc_hdac_word(channel_5, channel_6);
  hdac_shadow[1] = adc_hdac_word(channel_3, channel_4);
  hdac_shadow[2] = adc_hdac_word(channel_1, channel_2);
  qspi_write(ADC_QSPI_HDAC, hdac_shadow, ADC_HDAC_WORDS);
  psos_pause(1);
}

/* Sends hdac_shadow to the DAC by polling (0x1f62e). */
void hdac_update(void) {
  qspi_send(ADC_QSPI_HDAC, hdac_shadow, ADC_HDAC_WORDS);
}

/* The efc_write word's: the value to the EFC's DAC, then its strobe
 * (0x1f646). */
void efc_write(uint16_t value) {
  qspi_write(ADC_QSPI_EFC, &value, 1);
  efc_strobe = 0;
}

/* Writes pll.efc, rounded, plus random_dither, clamped to 0 to
 * 0xffff0, to the EFC's DAC (0x2421c). */
void efc_write_dithered(void) {
  /* The EFC's range: 20 bits, its DAC taking the top 16. */
  static const int32_t EFC_MOST = 0xffff0;
  static const int32_t EFC_DAC_SHIFT = 4;
  int32_t efc = double_round_to_long(float_to_double(pll.efc));

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

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x2426e). */
float efc_clamp(float efc) {
  /* The EFC's clamp, the top of its 20 bits. */
  static const float EFC_MOST = 1048560.0f;

  if (float_compare(0.0f, efc) > 0) {
    efc = 0.0f;
  } else if (float_compare(efc, EFC_MOST) > 0) {
    efc = EFC_MOST;
  } else {
    if (efc_range.clamped) {
      status_happening(STATUS_EFC_AT_LIMIT_CLEARED);
      efc_range.clamped = 0;
    }
    return efc;
  }
  status_happening(STATUS_EFC_AT_LIMIT_SET);
  efc_range.clamped = 1;
  return efc;
}

/* Clears efc_clamp's and hmon_check_efc's state (0x242de). */
void efc_clamp_reset(void) {
  efc_range.clamped = 0;
  efc_range.near_end = 0;
  efc_range.near_end_checks = 0;
}
