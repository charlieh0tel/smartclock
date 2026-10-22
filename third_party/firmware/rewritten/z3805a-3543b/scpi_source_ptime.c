/* The SCPI :SOURce:PTIMe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_ptime`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The time zone's changed hook: marks a change pending (0x45a24). */
void time_zone_changed(void) { time_zone_change_pending = 1; }

/* The time zone's done hook: refreshes the clock's record (0x45a2e). */
void time_zone_done(void) {
  clock_record_refresh();
  time_zone_change_pending = 0;
}

/* Requests a sync in power-up or holdover recovery, else reports a
 * settings conflict (0x45b42). */
void sync_immediate_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_POWERUP ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.sync_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, (uint8_t)source);
  }
}

/* Acknowledges a leap second, from holdover to normal PLL (0x47090). */
void leap_ack_request(int32_t index, int32_t source) {
  if (pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL) {
    pll.leap_ack_requested = 1;
  }
}

/* A short parameter as scpi_parse_short leaves it in the call's buffer:
 * its kind, SCPI_NONE for an optional one left out, its value and its
 * unit. */
typedef struct scpi_source_ptime_short_parameter {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
  int16_t unit;  /* +0x04 */
} scpi_source_ptime_short_parameter;

/* A short response item, as scpi_format_short reads it: 4 bytes, the
 * short in the first two. */
typedef struct scpi_source_ptime_short_item {
  int16_t value;  /* +0x00 */
  int16_t unused; /* +0x02 */
} scpi_source_ptime_short_item;

/* A date's or time of day's response items. */
typedef struct scpi_source_ptime_fields {
  scpi_source_ptime_short_item first;  /* +0x00: the year, or the hour */
  scpi_source_ptime_short_item second; /* +0x04: the month, or the minute */
  scpi_source_ptime_short_item third;  /* +0x08: the day, or the second */
} scpi_source_ptime_fields;

/* A packed date, year << 16 | month << 8 | day (calendar_date), as
 * response items. */
static void scpi_source_ptime_date_items(uint32_t date,
                                         scpi_source_ptime_fields* fields) {
  static const uint32_t YEAR_SHIFT = 16;
  static const uint32_t MONTH_SHIFT = 8;
  static const uint32_t BYTE_MASK = 0xff;

  fields->first.value = (uint16_t)(date >> YEAR_SHIFT);
  fields->second.value = (date >> MONTH_SHIFT) & BYTE_MASK;
  fields->third.value = date & BYTE_MASK;
}

/* :PTIMe:PPS:EDGE: a stub; -113, Undefined header (0x2dc68). */
int32_t scpi_set_SOURce_PTIMe_PPS_EDGE(void* parameters, void* response,
                                       scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :PTIMe:PPS:EDGE?: a stub; -113, Undefined header (0x2dc88). */
int32_t scpi_query_SOURce_PTIMe_PPS_EDGE(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}

/* :PTIMe:DATE?: the clock's year, month and day (0x2dcd0).  It answers
 * when get_date_time gives 0, which by the names is while the clock has
 * no date, and refuses with -230, Data corrupt or stale, when it gives 1
 * (the test at 0x2dcea). */
int32_t scpi_query_SOURce_PTIMe_DATE(void* parameters, void* response,
                                     scpi_call* call) {
  uint32_t date;

  if ((uint8_t)get_date_time(&date, 0) != 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  scpi_source_ptime_date_items(date, response);
  return 0;
}

/* :PTIMe:LEAPsecond:ACCumulated?: GPS less UTC in seconds; -230, Data
 * corrupt or stale, until it is known (0x2dd1e; 097-z3801-01, table
 * 4-2, page 4-8). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_ACCumulated(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  int16_t* accumulated = response;
  int16_t offset;
  uint16_t sr = enter_supervisor_mode();
  uint8_t valid = get_gps_utc_offset(&offset);

  set_interrupt_mask(sr);
  if (valid == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  *accumulated = offset;
  return 0;
}

/* :PTIMe:LEAPsecond:ACCumulated:CALCulate: its action, leap_ack_request,
 * through pllc (0x2dd6e). */
int32_t scpi_set_SOURce_PTIMe_LEAPsecond_ACCumulated_CALCulate(
    void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SOURce_PTIMe_LEAPsecond_ACCumulated_CALCulate, 0,
                 return_zero(call->input));
  return 0;
}

/* :PTIMe:LEAPsecond:DATE?: the pending leap second's year, month and
 * day; -230, Data corrupt or stale, with none pending (0x2dd94). */
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
 * minute; -230, Data corrupt or stale, with none pending (0x2ddfc). */
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

/* :PTIMe:LEAPsecond:GPSTime?: the GPS time of the pending leap second,
 * as #H and eight hex digits; -230, Data corrupt or stale, when it is 0,
 * after writing the text and answering it all the same (0x2de48;
 * 097-z3801-01, table 4-2, page 4-8). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_GPSTime(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  char** answer = response;
  char* text = scpi_response_texts[(uint8_t)return_zero(call->input)];
  int32_t gps_time = leap_gps_time();

  sprintf(text, "#H%08X", gps_time);
  *answer = text;
  if (gps_time == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  return 0;
}

/* :PTIMe:LEAPsecond:STATe? (0x2deb8). */
int32_t scpi_query_SOURce_PTIMe_LEAPsecond_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x2dec6). */
  *state = (int8_t)nv_settings.leap_pending;
  return 0;
}

/* :PTIMe:TCODe?: on the console only, has the time code sent in the
 * current format and waits until DUART channel B's transmit interrupt
 * signals it gone, answering an empty text; -230, Data corrupt or
 * stale, when tcode_request refuses (0x2ded2; 097-z3801-01, table 4-2,
 * page 4-8).  From another port it answers nothing. */
int32_t scpi_query_SOURce_PTIMe_TCODe(void* parameters, void* response,
                                      scpi_call* call) {
  /* The SCPI task's event duartb_transmit_interrupt signals at the end of
   * an asynchronous text (0x1ea10), as pllc does when a
   * request is done. */
  static const uint32_t SENT_EVENT = 0x20;
  scpi_token_string* code = response;
  uint32_t events;

  if (scpi_console_port() != call->input) {
    return 0;
  }
  psos_get_v(SENT_EVENT, 1, &events);
  if (tcode_request(nv_settings.tcode_format) == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  psos_wait_v(SENT_EVENT, 0, 0, &events);
  code->length = 0;
  return 0;
}

/* :PTIMe:TCODe:FORMat's keywords, by their indexes in its list (0x53b74):
 * F1 and F2, the manual's T1 and T2 (097-z3801-01, page 4-12). */
enum {
  SCPI_SOURCE_PTIME_F1 = 0,
  SCPI_SOURCE_PTIME_F2 = 1,
};

/* A keyword parameter as scpi_parse_choice leaves it: its kind, then
 * its index or value in the node's list. */
typedef struct scpi_source_ptime_choice {
  int16_t kind;    /* +0x00 */
  int16_t keyword; /* +0x02 */
} scpi_source_ptime_choice;

/* :PTIMe:TCODe:FORMat F1 or F2: T1 or T2, to pllc (0x2dff6). */
int32_t scpi_set_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_ptime_choice* format = parameters;

  if (format->keyword == SCPI_SOURCE_PTIME_F1) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T1,
                   return_zero(call->input));
  } else if (format->keyword == SCPI_SOURCE_PTIME_F2) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T2,
                   return_zero(call->input));
  }
  return 0;
}

/* :PTIMe:TCODe:FORMat?: F1 for T1, F2 for T2; for T2's test form it
 * writes nothing (0x2e054, its jump table at 0x2e06c). */
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

/* :PTIMe:TIME?: the clock's hour, minute and second; as :PTIMe:DATE?, it
 * answers when get_date_time gives 0 and refuses with -230, Data corrupt
 * or stale, when it gives 1 (0x2e086). */
int32_t scpi_query_SOURce_PTIMe_TIME(void* parameters, void* response,
                                     scpi_call* call) {
  scpi_source_ptime_fields* fields = response;
  time_of_day time;

  if ((uint8_t)get_date_time(0, &time) != 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  fields->first.value = time.hour;
  fields->second.value = time.minute;
  fields->third.value = time.second;
  return 0;
}

/* :PTIMe:TIME:STRing?: the clock's time as hh:mm:ss text; as
 * :PTIMe:TIME?, -230, Data corrupt or stale, when get_date_time gives 1,
 * and -300, Device-specific error, when format_time fails (0x2e164). */
int32_t scpi_query_SOURce_PTIMe_TIME_STRing(void* parameters, void* response,
                                            scpi_call* call) {
  char** answer = response;
  char* text = scpi_response_texts[(uint8_t)return_zero(call->input)];
  time_of_day time;

  if ((uint8_t)get_date_time(0, &time) != 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  if ((uint8_t)format_time(time, text) == 0) {
    return scpi_query_failed(STATUS_DEVICE_SPECIFIC_ERROR, call);
  }
  *answer = text;
  return 0;
}

/* :PTIMe:TZONe hours[, minutes]: the time zone, minutes 0 when left out,
 * to pllc through the source's staged longs, marking a change pending
 * first (0x2e1f2). */
int32_t scpi_set_SOURce_PTIMe_TZONe(void* parameters, void* response,
                                    scpi_call* call) {
  /* The values pllc takes: hours and minutes. */
  static const uint8_t LAST_VALUE = 1;
  scpi_source_ptime_short_parameter* zone = parameters;
  int32_t* staged = scpi_staged[(uint8_t)return_zero(call->input)];

  staged[0] = zone[0].value;
  staged[1] = zone[1].kind == SCPI_NONE ? 0 : zone[1].value;
  time_zone_changed();
  pllc_request_d(&scpi_param_SOURce_PTIMe_TZONe, LAST_VALUE, (uint32_t)staged,
                 return_zero(call->input));
  return 0;
}

/* :PTIMe:TZONe?: the time zone's hours and minutes, both negative west
 * of Greenwich (0x2e28c). */
int32_t scpi_query_SOURce_PTIMe_TZONe(void* parameters, void* response,
                                      scpi_call* call) {
  scpi_source_ptime_short_item* items = response;
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
 * (0x3005a; 097-z3801-01, table 4-2, page 4-6). */
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
  /* tfom_code takes the float's bits (0x30096). */
  union {
    float real;
    uint32_t bits;
  } tfom = {.real = pll.tfom};

  *merit = tfom_code(tfom.bits);
  return 0;
}

/* :PTIMe:TINTerval?: the filtered GPS 1 PPS to internal 1 PPS interval
 * in seconds, to 1E-10 s; -230, Data corrupt or stale, while there is
 * none (0x300b2; 097-z3801-01, table 4-2, page 4-8). */
int32_t scpi_query_SOURce_PTIMe_TINTerval(void* parameters, void* response,
                                          scpi_call* call) {
  /* The power of ten of the resolution (0x300dc). */
  static const int16_t RESOLUTION = -10;
  scpi_float_resolution* interval = response;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (interval_mean_flag == 0) {
    set_interrupt_mask(sr);
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  interval->value = interval_mean;
  interval->resolution = RESOLUTION;
  set_interrupt_mask(sr);
  return 0;
}

/* :PTIMe:SYNChronization:IMMediate: its action, sync_immediate_request,
 * through pllc (0x30108; 097-z3801-01, table 4-2, page 4-8). */
int32_t scpi_set_SOURce_PTIMe_SYNChronization_IMMediate(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  pllc_request_b(&scpi_action_SOURce_PTIMe_SYNChronization_IMMediate, 0,
                 return_zero(call->input));
  return 0;
}
