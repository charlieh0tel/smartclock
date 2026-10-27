/* scpi_source_ptime.c's functions that are this image's own: the rest are
 * common/scpi_source_ptime.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* :PTIMe:LEAPsecond:DATE?: the pending leap second's year, month and
 * day; -230, Data corrupt or stale, with none pending (0x2dd26). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_DATE(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  uint16_t sr = enter_supervisor_mode();
  uint8_t pending = nv_settings.leap_pending;
  uint32_t date = nv_settings.leap_date;

  set_interrupt_mask(sr);
  if (pending == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  scpi_source_ptime_date_items(date, response);
  return 0;
}

/* :PTIMe:LEAPsecond:DURation?: the seconds in the pending leap second's
 * minute; -230, Data corrupt or stale, with none pending (0x2dd8e). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_DURation(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  int16_t* duration = response;
  uint16_t sr = enter_supervisor_mode();
  uint8_t pending = nv_settings.leap_pending;
  int16_t seconds = (int16_t)nv_settings.leap_minute_length;

  set_interrupt_mask(sr);
  if (pending == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  *duration = seconds;
  return 0;
}

/* :PTIMe:LEAPsecond:STATe? (0x2de4a). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x2de58). */
  *state = (int8_t)nv_settings.leap_pending;
  return 0;
}

/* :PTIMe:TCODe:FORMat?: F1 for T1, F2 for T2; for T2's test form it
 * writes nothing (0x2e018, its jump table at 0x2e030). */
int32_t scpi_query_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* format = response;

  if (nv_settings.tcode_format == TIMECODE_T1) {
    *format = SCPI_SOURCE_PTIME_F1;
  } else if (nv_settings.tcode_format == TIMECODE_T2) {
    *format = SCPI_SOURCE_PTIME_F2;
  }
  return 0;
}

/* :PTIMe:TZONe?: the time zone's hours and minutes, both negative west
 * of Greenwich (0x2e29a). */
int32_t scpi_query_SOURce_PTIMe_TZONe(void* parameters, void* response,
                                      scpi_call* call) {
  scpi_short_item* items = response;
  int8_t hours;
  int8_t minutes;
  int8_t sign =
      time_zone_split((int8_t)nv_settings.time_zone[0],
                      (int8_t)nv_settings.time_zone[1], &hours, &minutes);

  if (sign > 0) {
    items[0].value = hours;
    items[1].value = minutes;
  } else {
    items[0].value = -hours;
    items[1].value = -minutes;
  }
  return 0;
}

/* :PTIMe:FFOMerit? and :TFOMerit?, by the path word their nodes add, 0
 * and 1: the frequency figure of merit, -230, Data corrupt or stale,
 * while it is unknown; or the time figure of merit from pll.tfom
 * (0x2fe2a; 097-z3801-01, table 4-2, page 4-6). */
int32_t scpi_query_SOURce_PTIMe_FFOMerit(void* parameters, void* response,
                                         scpi_call* call) {
  static const int16_t FREQUENCY = 0;
  int16_t* path_word = parameters;
  uint16_t* merit = response;

  if (*path_word == FREQUENCY) {
    frequency_figure_of_merit ffom;

    if ((uint8_t)get_ffom(&ffom) == 0) {
      return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
    }
    *merit = ffom;
    return 0;
  }
  /* tfom_code takes the float's bits (0x2fe66). */
  union {
    float real;
    uint32_t bits;
  } tfom = {.real = pll.tfom};

  *merit = tfom_code(tfom.bits);
  return 0;
}

/* Whether a source may ask for a time code: always while it is not sent
 * continuously, else from sources 0 and 1 alone (0x2de64). */
uint8_t tcode_query_allowed(request_source source) {
  return tcode_continuous == 0 || source == REQUEST_SOURCE_SCPI ||
         source == REQUEST_SOURCE_SILENT_1;
}

/* :PTIMe:TCODe?: the time code, sent in the current format, for a source
 * that may ask for it; else, or when tcode_request refuses, -221,
 * Settings conflict (0x2de84; 097-59551-02, 5-93). */
int32_t scpi_query_SOURce_PTIMe_TCODe(void* parameters, void* response,
                                      scpi_call* call) {
  if (tcode_query_allowed(return_zero(call->input)) == 0 ||
      !scpi_tcode_send(nv_settings.tcode_format, response, call)) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  return 0;
}

/* :PTIMe:TCODe:CONTinuous: whether the time code is sent each second, by
 * pllc (0x2e04a). */
int32_t scpi_set_SOURce_PTIMe_TCODe_CONTinuous(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_CONTinuous, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :PTIMe:TCODe:CONTinuous? (0x2e07a). */
int32_t scpi_query_SOURce_PTIMe_TCODe_CONTinuous(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* continuous = response;

  *continuous = return_byte(tcode_continuous);
  return 0;
}

/* :PTIMe:UTC: whether the time given is UTC or GPS time, by pllc, then
 * :SYSTem:PON's action (0x2e30c). */
int32_t scpi_set_SOURce_PTIMe_UTC(void* parameters, void* response,
                                  scpi_call* call) {
  const scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 return_zero(call->input));
  pllc_request_b(&scpi_action_SYSTem_PON, 0, return_zero(call->input));
  return 0;
}

/* Requests a sync in power-up or holdover recovery, else reports a
 * settings conflict (0x45d16). */
void sync_immediate_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.sync_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* Acknowledges a leap second, from holdover to normal PLL (0x4725a). */
void leap_ack_request(int32_t index, int32_t source) {
  if (pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL) {
    pll.leap_ack_requested = 1;
  }
}
