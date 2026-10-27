/* The SCPI :SOURce:PTIMe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_ptime`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The time zone's changed hook: marks a change pending. */
void time_zone_changed(void) { time_zone_change_pending = 1; }

/* The time zone's done hook: refreshes the clock's record. */
void time_zone_done(void) {
  clock_record_refresh();
  time_zone_change_pending = 0;
}

/* A date's or time of day's response items. */
typedef struct scpi_source_ptime_fields {
  scpi_short_item first;  /* +0x00: the year, or the hour */
  scpi_short_item second; /* +0x04: the month, or the minute */
  scpi_short_item third;  /* +0x08: the day, or the second */
} scpi_source_ptime_fields;

/* A packed date, year << 16 | month << 8 | day (calendar_date), as
 * response items. */
static void scpi_source_ptime_date_items(uint32_t date,
                                         scpi_source_ptime_fields* fields) {
  static const uint32_t YEAR_SHIFT = 16;
  static const uint32_t MONTH_SHIFT = 8;

  fields->first.value = (uint16_t)(date >> YEAR_SHIFT);
  fields->second.value = (date >> MONTH_SHIFT) & CPU_BYTE_MASK;
  fields->third.value = date & CPU_BYTE_MASK;
}

/* :PTIMe:PPS:EDGE: a stub; -113, Undefined header. */
int32_t scpi_set_SOURce_PTIMe_PPS_EDGE(void* parameters, void* response,
                                       scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :PTIMe:PPS:EDGE?: a stub; -113, Undefined header. */
int32_t scpi_query_SOURce_PTIMe_PPS_EDGE(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}

/* :PTIMe:DATE?: the clock's year, month and day.  It answers
 * when get_date_time gives 0, once clock.set is set, and refuses with
 * -230, Data corrupt or stale, when it gives 1, before (get_date_time+0x32 to
 * get_date_time+0x38; the test at scpi_query_SOURce_PTIMe_DATE+0x1a). */
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
 * corrupt or stale, until it is known
 * (scpi_query_SOURce_PTIMe_LEAPsecond_ACCumulated; 097-z3801-01, table 4-2,
 * page 4-8). */
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
 * through pllc. */
int32_t scpi_set_SOURce_PTIMe_LEAPsecond_ACCumulated_CALCulate(
    void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SOURce_PTIMe_LEAPsecond_ACCumulated_CALCulate, 0,
                 return_zero(call->input));
  return 0;
}

/* :PTIMe:LEAPsecond:GPSTime?: the GPS time of the pending leap second,
 * as #H and eight hex digits; -230, Data corrupt or stale, when it is 0,
 * after writing the text and answering it all the same
 * (scpi_query_SOURce_PTIMe_LEAPsecond_GPSTime; 097-z3801-01, table 4-2, page
 * 4-8). */
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

/* :PTIMe:TCODe:FORMat's keywords, by their indexes in its list
 * (scpi_choices_set_SOURce_PTIMe_TCODe_FORMat_1): F1 and F2, the manual's T1
 * and T2 (097-z3801-01, page 4-12). */
enum {
  SCPI_SOURCE_PTIME_F1 = 0,
  SCPI_SOURCE_PTIME_F2 = 1,
};

/* :PTIMe:TCODe:FORMat F1 or F2: T1 or T2, to pllc. */
int32_t scpi_set_SOURce_PTIMe_TCODe_FORMat(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_word_parameter* format = parameters;

  if (format->value == SCPI_SOURCE_PTIME_F1) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T1,
                   return_zero(call->input));
  } else if (format->value == SCPI_SOURCE_PTIME_F2) {
    pllc_request_a(&scpi_param_SOURce_PTIMe_TCODe_FORMat, 0, TIMECODE_T2,
                   return_zero(call->input));
  }
  return 0;
}

/* :PTIMe:TIME?: the clock's hour, minute and second; as :PTIMe:DATE?, it
 * answers when get_date_time gives 0 and refuses with -230, Data corrupt
 * or stale, when it gives 1. */
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
 * and -300, Device-specific error, when format_time fails. */
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
 * first. */
int32_t scpi_set_SOURce_PTIMe_TZONe(void* parameters, void* response,
                                    scpi_call* call) {
  /* The values pllc takes: hours and minutes. */
  static const uint8_t LAST_VALUE = 1;
  scpi_short_parameter* zone = parameters;
  int32_t* staged = scpi_staged[(uint8_t)return_zero(call->input)];

  staged[0] = zone[0].value;
  staged[1] = zone[1].kind == SCPI_NONE ? 0 : zone[1].value;
  time_zone_changed();
  pllc_request_d(&scpi_param_SOURce_PTIMe_TZONe, LAST_VALUE, (uint32_t)staged,
                 return_zero(call->input));
  return 0;
}

/* :PTIMe:TINTerval?: the filtered GPS 1 PPS to internal 1 PPS interval
 * in seconds, to 1E-10 s; -230, Data corrupt or stale, while there is
 * none (scpi_query_SOURce_PTIMe_TINTerval; 097-z3801-01, table 4-2, page 4-8).
 */
int32_t scpi_query_SOURce_PTIMe_TINTerval(void* parameters, void* response,
                                          scpi_call* call) {
  /* The power of ten of the resolution
   * (scpi_query_SOURce_PTIMe_TINTerval+0x2a). */
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
 * through pllc (scpi_set_SOURce_PTIMe_SYNChronization_IMMediate; 097-z3801-01,
 * table 4-2, page 4-8). */
int32_t scpi_set_SOURce_PTIMe_SYNChronization_IMMediate(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  pllc_request_b(&scpi_action_SOURce_PTIMe_SYNChronization_IMMediate, 0,
                 return_zero(call->input));
  return 0;
}
