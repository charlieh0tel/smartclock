/* The ADC on the QSPI, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `devices/adc`).  The exported C includes this file after its own, so
 * the types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  The health monitor's convert routines
 * scale its readings (monitors.c). */

/* Reads an ADC channel through the QSPI: the channel's input is the
 * channel shifted right one, plus 4 for an odd channel (smartclock.h's
 * adc_channel) (0x1f0ec). */
int32_t adc_read(adc_channel channel) {
  /* The ADC's command word, as adc_read builds it, and its reply, the
   * reading in bits 9 to 2. */
  static const uint16_t COMMAND = 0xc000;
  static const uint16_t INPUT_SHIFT = 11;
  static const uint16_t REPLY_SHIFT = 2;
  static const uint8_t CHANNELS = 7;
  static const uint8_t ODD_INPUT = 4;
  uint8_t input = (channel & CHANNELS) >> 1;

  if (channel & 1) {
    input |= ODD_INPUT;
  }
  uint16_t command = (uint16_t)(input << INPUT_SHIFT) | COMMAND;
  uint16_t reply;
  qspi_buffers buffers = {&command, &reply};

  qspi_read(QSPI_ADC, &buffers, 1);
  return reply >> REPLY_SHIFT;
}
