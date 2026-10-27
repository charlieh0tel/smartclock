/* scpi_source_pulse.c's functions that are this image's own: the rest are
 * common/scpi_source_pulse.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* :PULSe's done hook: programs the pulse from its settings, the start's
 * time and date each kept as three longs (0x47faa). */
void pulse_settings_done(void) {
  time_of_day start_time = {
      .reserved = 0,
      .hour = (uint8_t)nv_settings.pulse_start_time[0],
      .minute = (uint8_t)nv_settings.pulse_start_time[1],
      .second = (uint8_t)nv_settings.pulse_start_time[2],
  };
  calendar_date start_date = {
      .year = (uint16_t)nv_settings.pulse_start_date[0],
      .month = (uint8_t)nv_settings.pulse_start_date[1],
      .day = (uint8_t)nv_settings.pulse_start_date[2],
  };

  pulse_program(start_time, start_date, nv_settings.pulse_state,
                nv_settings.pulse_period);
}

/* :PULSe:CONTinuous:STATe? (0x2daa0). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended (0x2daa6). */
  *state = (int8_t)nv_settings.pulse_state;
  return 0;
}

/* The start's date or time as response items, each long truncated to a
 * short. */
static void scpi_source_pulse_start_get(const int32_t* start,
                                        scpi_short_item* items) {
  for (uint8_t field = 0; field < SCPI_SOURCE_PULSE_FIELDS; field++) {
    items[field].value = (int16_t)start[field];
  }
}

/* :PULSe:STARt:DATE? (0x2db34). */
int32_t scpi_query_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_date, response);
  return 0;
}

/* :PULSe:STARt:TIME? (0x2dbd8). */
int32_t scpi_query_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_time, response);
  return 0;
}
