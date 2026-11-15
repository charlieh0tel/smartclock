/* scpi_source_ptime.c's functions that are this image's own: the rest are
 * common/scpi_source_ptime.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the 58503A 3704's and the
 * Z3805A 3543B's rewritten/scpi_source_ptime.c, of its functions, those the
 * Z3816A's code carries (identical or relocated by image_diff, or changed with
 * the same instructions), with the static helpers they use, each the same here
 * but where its comment says otherwise (docs/firmware/z3816a-4001.md, "The
 * link check").  Its comments' addresses are the Z3816A's.  Each keeps its
 * generated prototype. */

/* :PTIMe:LEAPsecond:DATE?: the pending leap second's year, month and
 * day; -230, Data corrupt or stale, with none pending (0x3d804). */
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
 * minute; -230, Data corrupt or stale, with none pending (0x3d86c). */
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

/* :PTIMe:LEAPsecond:STATe? (0x3d928). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x3d936). */
  *state = (int8_t)nv_settings.leap_pending;
  return 0;
}

/* :PTIMe:TCODe:FORMat?: F1 for T1, F2 for T2; for T2's test form it
 * writes nothing (0x3daee, its jump table at 0x3db06). */
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
 * of Greenwich (0x3dd70). */
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
 * (0x3f8a2; 097-z3801-01, table 4-2, page 4-6). */
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
  /* tfom_code takes the float's bits (0x3f8de). */
  union {
    float real;
    uint32_t bits;
  } tfom = {.real = pll.tfom};

  *merit = tfom_code(tfom.bits);
  return 0;
}

/* :PTIMe:TCODe?: the time code, sent in the current format, for a source
 * that may ask for it; else, or when tcode_request refuses, -221,
 * Settings conflict (0x3d95a; 097-59551-02, 5-93). */
int32_t scpi_query_SOURce_PTIMe_TCODe(void* parameters, void* response,
                                      scpi_call* call) {
  if (tcode_query_allowed(scpi_input_source(call->input)) == 0 ||
      !scpi_tcode_send(nv_settings.tcode_format, response, call)) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  return 0;
}

/* :PTIMe:TCODe:CONTinuous: whether the time code is sent each second, by
 * pllc (0x3db20). */
int32_t scpi_set_SOURce_PTIMe_TCODe_CONTinuous(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_CONTinuous, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :PTIMe:TCODe:CONTinuous? (0x3db50). */
int32_t scpi_query_SOURce_PTIMe_TCODe_CONTinuous(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* continuous = response;

  *continuous = return_byte(tcode_continuous);
  return 0;
}

/* Requests a sync in power-up or holdover recovery, else reports a
 * settings conflict (0x4957a). */
void sync_immediate_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.sync_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* :PTIMe:TCODe:FORMat F1 or F2: T1 or T2, to pllc. */
int32_t scpi_set_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_word_parameter* format = parameters;

  if (format->value == SCPI_SOURCE_PTIME_F1) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T1,
                   scpi_input_source(call->input));
  } else if (format->value == SCPI_SOURCE_PTIME_F2) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T2,
                   scpi_input_source(call->input));
  }
  return 0;
}

/* Acknowledges a leap second, from holdover to normal PLL (0x4abd2). */
void leap_ack_request(int32_t index, int32_t source) {
  if (pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL) {
    pll.leap_ack_requested = 1;
  }
}

/* Whether a source may ask for a time code: always while it is not sent
 * continuously, else from source 0 alone, where the 58503A's takes 1 too
 * (0x3d942). */
uint8_t tcode_query_allowed(request_source source) {
  return tcode_continuous == 0 || source == REQUEST_SOURCE_SCPI;
}
