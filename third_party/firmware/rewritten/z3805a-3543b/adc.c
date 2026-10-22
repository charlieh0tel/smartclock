/* The ADC on the QSPI, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `devices/adc`).  The exported C includes this file after its own, so
 * the types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is adc_read and the read_adc_*
 * scalings. */

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

  qspi_read(QSPI_ADC, &buffers, 1);
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
