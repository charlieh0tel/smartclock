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

/* :PTIMe:PPS:EDGE: a stub; -113, Undefined header. */
int32_t scpi_set_SOURce_PTIMe_PPS_EDGE(void* parameters, void* response,
                                       scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
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
                 scpi_input_source(call->input));
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
  char* text = scpi_response_texts[(uint8_t)scpi_input_source(call->input)];
  int32_t gps_time = leap_gps_time();

  sprintf(text, "#H%08X", gps_time);
  *answer = text;
  if (gps_time == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
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
  char* text = scpi_response_texts[(uint8_t)scpi_input_source(call->input)];
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
  int32_t* staged = scpi_staged[(uint8_t)scpi_input_source(call->input)];

  staged[0] = zone[0].value;
  staged[1] = zone[1].kind == SCPI_NONE ? 0 : zone[1].value;
  time_zone_changed();
  pllc_request_d(&scpi_param_SOURce_PTIMe_TZONe, LAST_VALUE, (uint32_t)staged,
                 scpi_input_source(call->input));
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
                 scpi_input_source(call->input));
  return 0;
}
