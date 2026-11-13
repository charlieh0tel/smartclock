/* From the Z3815A 4010's rewritten/scpi_source_pulse.c, the Z3817A 3752's own
 * beside common/'s: of its functions, those the Z3817A's code carries identical
 * or relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* :PULSe's done hook: hands the pulse's settings, the start's time and
 * date each kept as three longs, to pulse_program, a bare rts here
 * (0x47ca4), so that it programs nothing (0x47ca6). */
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

/* :PULSe:CONTinuous:STATe? (0x3e86a). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended (0x3e870). */
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

/* :PULSe:STARt:DATE? (0x3e8fe). */
int32_t scpi_query_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_date, response);
  return 0;
}

/* :PULSe:STARt:TIME? (0x3e9a2). */
int32_t scpi_query_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_time, response);
  return 0;
}
