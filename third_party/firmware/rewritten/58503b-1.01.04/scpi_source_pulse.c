/* From the Z3816A 4001's rewritten/scpi_source_pulse.c, the 58503B 1.01.04's
 * own beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* :PULSe's done hook: programs the pulse from its settings, the start's
 * time and date each kept as three longs (0x4d128). */
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

/* :PULSe:CONTinuous:STATe? (0x3dc1a). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended (0x3dc20). */
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

/* :PULSe:STARt:DATE? (0x3dcae). */
int32_t scpi_query_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_date, response);
  return 0;
}

/* :PULSe:STARt:TIME? (0x3dd52). */
int32_t scpi_query_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_time, response);
  return 0;
}
