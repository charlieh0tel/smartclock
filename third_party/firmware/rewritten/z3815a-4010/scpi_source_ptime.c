/* scpi_source_ptime.c's functions that are this image's own: the rest are
 * common/scpi_source_ptime.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* :PTIMe:TDATe:DEFault?'s path words: its GPS and UTC nodes' (the
 * DEFault node's is the clock's own timescale). */
enum {
  SCPI_SOURCE_PTIME_TDATE_GPS = 1,
  SCPI_SOURCE_PTIME_TDATE_UTC = 2,
};

/* :PTIMe:TDATe:DEFault?, :TDATe:GPS? and :TDATe:UTC?: the clock's year,
 * month, day, hour, minute and second, in the clock's timescale, or
 * converted to GPS or UTC, by the node's path word, from the one the
 * clock keeps, UTC while nv_settings.gps_utc is set; as :PTIMe:DATE?,
 * -230, Data corrupt or stale, while get_date_time gives 1 (0x41c08). */
int32_t scpi_query_SOURce_PTIMe_TDATe_DEFault(void* parameters, void* response,
                                              scpi_call* call) {
  const int16_t* path = parameters;
  scpi_source_ptime_fields* fields = response;
  uint32_t date;
  time_of_day time;

  if ((uint8_t)get_date_time(&date, &time) != 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  if (*path == SCPI_SOURCE_PTIME_TDATE_GPS && nv_settings.gps_utc != 0) {
    utc_to_gps(&date, &time);
  } else if (*path == SCPI_SOURCE_PTIME_TDATE_UTC && nv_settings.gps_utc == 0) {
    gps_to_utc(&date, &time);
  }
  scpi_source_ptime_date_items(date, &fields[0]);
  fields[1].first.value = time.hour;
  fields[1].second.value = time.minute;
  fields[1].third.value = time.second;
  return 0;
}

/* scpi_source_ptime.c's functions that are this image's own: the rest are
 * common/scpi_source_ptime.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* :PTIMe:LEAPsecond:DATE?: the pending leap second's year, month and
 * day; -230, Data corrupt or stale, with none pending (0x41d80). */
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
 * minute; -230, Data corrupt or stale, with none pending (0x41de8). */
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

/* :PTIMe:LEAPsecond:STATe? (0x41ea4). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x41eb2). */
  *state = (int8_t)nv_settings.leap_pending;
  return 0;
}

/* :PTIMe:TZONe?: the time zone's hours and minutes, both negative west
 * of Greenwich (0x4236c). */
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
 * (0x440ba; 097-z3801-01, table 4-2, page 4-6). */
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
  /* tfom_code takes the float's bits (0x440f6). */
  union {
    float real;
    uint32_t bits;
  } tfom = {.real = pll.tfom};

  *merit = tfom_code(tfom.bits);
  return 0;
}

/* :PTIMe:TCODe:CONTinuous: whether the time code is sent each second, by
 * pllc (0x4211c). */
int32_t scpi_set_SOURce_PTIMe_TCODe_CONTinuous(void* parameters, void* response,
                                               scpi_call* call) {
  const scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_CONTinuous, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* :PTIMe:TCODe:CONTinuous? (0x4214c). */
int32_t scpi_query_SOURce_PTIMe_TCODe_CONTinuous(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* continuous = response;

  *continuous = return_byte(tcode_continuous);
  return 0;
}

/* :PTIMe:UTC: whether the time given is UTC or GPS time, by pllc, then
 * :SYSTem:PON's action (0x423de). */
int32_t scpi_set_SOURce_PTIMe_UTC(void* parameters, void* response,
                                  scpi_call* call) {
  const scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_DIAGnostic_GPSystem_UTC, 0, given->value,
                 scpi_input_source(call->input));
  pllc_request_b(&scpi_action_SYSTem_PON, 0, scpi_input_source(call->input));
  return 0;
}

/* Whether a source may ask for a time code: always while it is not sent
 * continuously, else from the SCI's alone, where the 58503A's takes
 * sources 0 and 1 (0x41ebe). */
uint8_t tcode_query_allowed(request_source source) {
  return tcode_continuous == 0 || source == REQUEST_SOURCE_SCI;
}

/* Requests a sync in power-up or holdover recovery, else reports a
 * settings conflict (0x4f3a8). */
void sync_immediate_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.sync_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* Acknowledges a leap second, from holdover to normal PLL (0x507ce). */
void leap_ack_request(int32_t index, int32_t source) {
  if (pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL) {
    pll.leap_ack_requested = 1;
  }
}

/* :PTIMe:TCODe:FORMat's keywords past the shared F1 and F2, by their
 * indexes in its list (scpi_keyword_FL1): this image's FL1 and FL2. */
enum {
  SCPI_SOURCE_PTIME_FL1 = 2,
  SCPI_SOURCE_PTIME_FL2 = 3,
};

/* :PTIMe:TCODe:FORMat F1, F2, FL1 or FL2: T1, T2, FL1 or FL2, to pllc,
 * where the 58503A's takes F1 and F2 alone (0x42052). */
int32_t scpi_set_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_word_parameter* format = parameters;
  timecode_format chosen;

  switch (format->value) {
    case SCPI_SOURCE_PTIME_F1:
      chosen = TIMECODE_T1;
      break;
    case SCPI_SOURCE_PTIME_F2:
      chosen = TIMECODE_T2;
      break;
    case SCPI_SOURCE_PTIME_FL1:
      chosen = TIMECODE_FL1;
      break;
    case SCPI_SOURCE_PTIME_FL2:
      chosen = TIMECODE_FL2;
      break;
    default:
      return 0;
  }
  pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, chosen,
                 scpi_input_source(call->input));
  return 0;
}

/* :PTIMe:TCODe:FORMat?: F1 for T1, and for T2 with its test prefix or a
 * format past FL2; F2, FL1 or FL2 for theirs.  The 58503A's leaves the
 * response for any but T1 and T2 (0x420de). */
int32_t scpi_query_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                             scpi_call* call) {
  int16_t* format = response;

  switch (nv_settings.tcode_format) {
    case TIMECODE_T2:
      *format = SCPI_SOURCE_PTIME_F2;
      break;
    case TIMECODE_FL1:
      *format = SCPI_SOURCE_PTIME_FL1;
      break;
    case TIMECODE_FL2:
      *format = SCPI_SOURCE_PTIME_FL2;
      break;
    default:
      *format = SCPI_SOURCE_PTIME_F1;
      break;
  }
  return 0;
}

/* :PTIMe:TCODe? [F1|F2|FL1|FL2]: the time code, sent in the format given,
 * T1, T2, FL1 or FL2, T2 for a choice past FL2, or with none in
 * nv_settings.tcode_format, for a source that may ask for it; else, or
 * when tcode_request refuses, -221, Settings conflict.  The 58503A's takes
 * no format (0x41ed8, its jump table at 0x41f20). */
int32_t scpi_query_SOURce_PTIMe_TCODe(void* parameters, void* response,
                                      scpi_call* call) {
  const scpi_word_parameter* given = parameters;
  const request_source source = scpi_input_source(call->input);
  timecode_format format;

  if (given->kind == SCPI_NONE) {
    format = nv_settings.tcode_format;
  } else {
    switch (given->value) {
      case SCPI_SOURCE_PTIME_F1:
        format = TIMECODE_T1;
        break;
      case SCPI_SOURCE_PTIME_FL1:
        format = TIMECODE_FL1;
        break;
      case SCPI_SOURCE_PTIME_FL2:
        format = TIMECODE_FL2;
        break;
      default:
        format = TIMECODE_T2;
        break;
    }
  }
  if (tcode_query_allowed(source) == 0 ||
      !scpi_tcode_send(format, source, response, call)) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  return 0;
}
