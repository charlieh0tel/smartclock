/* The SCPI :SOURce:GPSystem tree (docs/scpi/README.md): its handlers,
 * and the hooks and actions pllc runs for its settings, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/source_gpsystem`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  097-z3801-01, table 4-2, pages 4-6 and 4-7, gives what
 * the commands mean. */

/* Requests the survey finished, and the source to save with. */
void douglas_finish_request(int32_t index, int32_t source) {
  douglas_finish_requested = 1;
  douglas_finish_nv_argument = source;
}

/* :GPS:ADELay?: the antenna delay, or its MIN or MAX, in seconds. */
int32_t scpi_query_SOURce_GPSystem_ADELay(void* parameters, void* response,
                                          scpi_call* call) {
  scpi_word_parameter* bound = parameters;
  float* seconds = response;

  *seconds = float_multiply(scpi_numeric_get(&scpi_param_SOURce_GPSystem_ADELay,
                                             0, bound->kind, bound->value),
                            SCPI_SOURCE_GPSYSTEM_NANOSECOND);
  return 0;
}

/* :GPS:EMANgle: the elevation mask in degrees, or MIN or MAX, to pllc. */
int32_t scpi_set_SOURce_GPSystem_EMANgle(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_short_parameter* angle = parameters;

  scpi_numeric_set(&scpi_param_SOURce_GPSystem_EMANgle, 0, angle->kind,
                   angle->value, 0, angle->value,
                   scpi_input_source(call->input));
  return 0;
}

/* :GPS:EMANgle?. */
int32_t scpi_query_SOURce_GPSystem_EMANgle(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_word_parameter* bound = parameters;
  int16_t* angle = response;

  *angle = (int16_t)float_to_integer(scpi_numeric_get(
      &scpi_param_SOURce_GPSystem_EMANgle, 0, bound->kind, bound->value));
  return 0;
}

/* :GPS:INITial:DATE's or :TIME's three fields to pllc, through the
 * source's staged longs, while time_valid gives 0; -221, Settings
 * conflict, else. */
static void scpi_source_gpsystem_initial_set(scpi_numeric_param* param,
                                             scpi_short_parameter* fields,
                                             scpi_port* input) {
  request_source source = scpi_input_source(input);

  if (time_valid() != 0) {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
    return;
  }
  int32_t* staged = scpi_staged[source];

  for (uint8_t field = 0; field < SCPI_SOURCE_GPSYSTEM_POSITION_LONGS;
       field++) {
    staged[field] = fields[field].value;
  }
  pllc_request_d(param, SCPI_SOURCE_GPSYSTEM_POSITION_LONGS - 1,
                 (uint32_t)staged, source);
}

/* :GPS:INITial:DATE year, month, day. */
int32_t scpi_set_SOURce_GPSystem_INITial_DATE(void* parameters, void* response,
                                              scpi_call* call) {
  scpi_source_gpsystem_initial_set(&scpi_param_SOURce_GPSystem_INITial_DATE,
                                   parameters, call->input);
  return 0;
}

/* :GPS:INITial:TIME hour, minute, second. */
int32_t scpi_set_SOURce_GPSystem_INITial_TIME(void* parameters, void* response,
                                              scpi_call* call) {
  scpi_source_gpsystem_initial_set(&scpi_param_SOURce_GPSystem_INITial_TIME,
                                   parameters, call->input);
  return 0;
}

/* :GPS:POSition:ACTual?: the position the receiver reports; -230, Data
 * corrupt or stale, before it has.  It gives mas_to_dms each
 * angle as it is, not its magnitude. */
int32_t scpi_query_SOURce_GPSystem_POSition_ACTual(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  bool reported = gps_position_get(&position[0], &position[1], &position[2]);

  set_interrupt_mask(sr);
  if (!reported) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  scpi_source_gpsystem_location_answer(position, false, response);
  return 0;
}

/* 1 for a satellite number from 1 to 32. */
uint32_t satellite_number_valid(uint8_t number) {
  return number >= 1 && number <= SCPI_SOURCE_GPSYSTEM_SATELLITES;
}

/* A satellite's bit in a mask: its number less one's (satellite_in_mask+0xa).
 */
static uint32_t scpi_source_gpsystem_bit(uint8_t number) {
  return cpu_long_bit((uint32_t)(number - 1));
}

/* Whether a satellite's bit is set in a mask. */
bool satellite_in_mask(char number, uint32_t mask) {
  return (mask & scpi_source_gpsystem_bit((uint8_t)number)) != 0;
}

/* Sets a valid satellite's bit in a mask; whether it was valid. */
bool satellite_mask_add(char number, uint32_t* mask) {
  if ((uint8_t)satellite_number_valid(number) == 0) {
    return false;
  }
  *mask |= scpi_source_gpsystem_bit((uint8_t)number);
  return true;
}

/* Clears a valid satellite's bit in a mask; whether it was valid. */
bool satellite_mask_remove(char number, uint32_t* mask) {
  if ((uint8_t)satellite_number_valid(number) == 0) {
    return false;
  }
  *mask &= ~scpi_source_gpsystem_bit((uint8_t)number);
  return true;
}

/* Lists, to a NULL list just counts, the numbers plus base of the
 * satellites 1 to count whose bits are set, up to the most given; the
 * count listed. */
uint8_t satellites_in_mask(uint32_t mask, uint8_t count, int16_t* list,
                           uint8_t most, uint8_t base) {
  uint8_t listed = 0;

  for (uint8_t number = 1; number <= count && listed < most; number++) {
    if (satellite_in_mask(number, mask)) {
      if (list != 0) {
        *list++ = base + number;
      }
      listed++;
    }
  }
  return listed;
}

/* :GPS:SATellite:TRACking?: the GPS satellites and then the pseudolites
 * the channels use, as many of each as are in use; -230, Data corrupt or
 * stale, while unknown.  The image tests the result's low
 * byte. */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)scpi_input_source(call->input)];
  uint8_t count;
  uint8_t pseudolite_count;
  uint32_t set;
  uint8_t pseudolite_set;
  uint8_t id_count;
  uint8_t max_listed;
  uint8_t id_base;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t known =
      satellites_in_use(&count, &pseudolite_count, &set, &pseudolite_set);

  set_interrupt_mask(sr);
  if (known == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  /* Each list is limited by the count in use, not max_listed. */
  satellite_list_limits(SATELLITE_LIST_TRACKING, &id_count, &max_listed,
                        &id_base);
  uint8_t listed = satellites_in_mask(set, id_count, list, count, id_base);

  satellite_list_limits(SATELLITE_LIST_TRACKING_PSEUDOLITES, &id_count,
                        &max_listed, &id_base);
  uint8_t pseudolites_listed = satellites_in_mask(
      pseudolite_set, id_count, list + listed, pseudolite_count, id_base);

  scpi_source_gpsystem_list_answer(list, listed + pseudolites_listed, response);
  return 0;
}

/* :GPS:SATellite:TRACking:COUNt?: the satellites and pseudolites in use;
 * -230, Data corrupt or stale, while unknown. */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_COUNt(void* parameters,
                                                            void* response,
                                                            scpi_call* call) {
  uint16_t* tracked = response;
  uint8_t count;
  uint8_t pseudolite_count;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t known = satellites_in_use(&count, &pseudolite_count, 0, 0);

  set_interrupt_mask(sr);
  if (known == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  *tracked = count + pseudolite_count;
  return 0;
}

/* Every satellite's bit. */
static const uint32_t SCPI_SOURCE_GPSYSTEM_EVERY_SATELLITE = 0xffffffff;

/* :GPS:SATellite:TRACking:IGNore:ALL, and :INCLude:NONE: ignores every
 * satellite. */
int32_t scpi_set_SOURce_GPSystem_SATellite_TRACking_IGNore_ALL(
    void* parameters, void* response, scpi_call* call) {
  pllc_request_c(&scpi_param_SOURce_GPSystem_SATellite_TRACking_IGNore, 0,
                 SCPI_SOURCE_GPSYSTEM_EVERY_SATELLITE, 0,
                 scpi_input_source(call->input));
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore:NONE, and :INCLude:ALL: ignores none. */
int32_t scpi_set_SOURce_GPSystem_SATellite_TRACking_IGNore_NONE(
    void* parameters, void* response, scpi_call* call) {
  pllc_request_c(&scpi_param_SOURce_GPSystem_SATellite_TRACking_IGNore, 0, 0, 0,
                 scpi_input_source(call->input));
  return 0;
}

/* :GPS:SATellite:VISible:PREDicted?: the satellites the almanac predicts
 * visible, as many as it counts; -230, Data corrupt or stale, while
 * unknown.  The image tests the result's low byte. */
int32_t scpi_query_SOURce_GPSystem_SATellite_VISible_PREDicted(
    void* parameters, void* response, scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)scpi_input_source(call->input)];
  uint8_t count;
  uint32_t set;
  uint8_t id_count;
  uint8_t max_listed;
  uint8_t id_base;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t known = visible_satellites(&count, &set);

  set_interrupt_mask(sr);
  if (known == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  satellite_list_limits(SATELLITE_LIST_VISIBLE, &id_count, &max_listed,
                        &id_base);
  scpi_source_gpsystem_list_answer(
      list, satellites_in_mask(set, id_count, list, count, id_base), response);
  return 0;
}

/* :GPS:SATellite:VISible:PREDicted:COUNt?; -230, Data corrupt or stale,
 * while unknown. */
int32_t scpi_query_SOURce_GPSystem_SATellite_VISible_PREDicted_COUNt(
    void* parameters, void* response, scpi_call* call) {
  uint16_t* visible = response;
  uint8_t count;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t known = visible_satellites(&count, 0);

  set_interrupt_mask(sr);
  if (known == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  *visible = count;
  return 0;
}
