/* From the 58503B 1.01.04's rewritten/scpi_source_gpsystem.c, the Z3805A
 * 1.01.03's own beside common/'s: of its functions, those the Z3805A
 * 1.01.03's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list").  Its comments'
 * addresses are the Z3805A 1.01.03's, but where they name another image.
 * Each keeps its generated prototype.  With them scpi_query_CHOE, this
 * image's own. */

/* Lists a mask's satellites, to a NULL list just counts them. */
static uint8_t scpi_source_gpsystem_mask_list(uint32_t mask, int16_t* list) {
  uint8_t id_count;
  uint8_t max_listed;
  uint8_t id_base;

  satellite_list_limits(SATELLITE_LIST_ALL, &id_count, &max_listed, &id_base);
  return satellites_in_mask(mask, id_count, list, max_listed, id_base);
}

/* :IGNore:STATe?'s and :INCLude:STATe?'s parameters: the path word
 * their nodes add, then the satellite. */
typedef struct scpi_source_gpsystem_state_parameters {
  uint16_t path_word;             /* +0x00: :INCLude's 0, :IGNore's 1 */
  scpi_short_parameter satellite; /* +0x02 */
} scpi_source_gpsystem_state_parameters;

/* The antenna delay in feet, a foot 1.2 ns of cable, :GPS:ADELay's
 * nanoseconds over it (0x2dc50, 0x2dc06). */
static const float SCPI_SOURCE_GPSYSTEM_FOOT_NANOSECONDS = 1.2f;

static const float SCPI_SOURCE_GPSYSTEM_FOOT = 1.2e-9f;

/* MIN's value in a numeric node's list (0x58678). */
static const int16_t SCPI_SOURCE_GPSYSTEM_MINIMUM = 1;

/* The parameters of a <location> after its first. */
#define SCPI_SOURCE_GPSYSTEM_LOCATION_REST 8

/* How many of a <location>'s parameters after the first are left out. */
static uint8_t scpi_source_gpsystem_left_out(
    scpi_source_gpsystem_location_parameters* location) {
  const int16_t kinds[SCPI_SOURCE_GPSYSTEM_LOCATION_REST] = {
      location->latitude.degrees.kind,  location->latitude.minutes.kind,
      location->latitude.seconds.kind,  location->longitude.hemisphere.kind,
      location->longitude.degrees.kind, location->longitude.minutes.kind,
      location->longitude.seconds.kind, location->height.kind,
  };
  uint8_t left_out = 0;

  for (uint8_t index = 0; index < SCPI_SOURCE_GPSYSTEM_LOCATION_REST; index++) {
    if (kinds[index] == SCPI_NONE) {
      left_out++;
    }
  }
  return left_out;
}

/* A satellite list's numbers, scpi_response_list's. */
#define SCPI_SOURCE_GPSYSTEM_LIST_SIZE \
  (sizeof(scpi_response_list) / sizeof(int16_t))

/* The ignore list's satellites, each of up to 32 parameters, the first
 * required and the rest while given, each added to the mask or removed
 * from it; all to pllc if each was a satellite number, else -222, Data
 * out of range, and none.  :INCLude sends the ignore list too. */
static void scpi_source_gpsystem_ignore_edit(scpi_short_parameter* satellites,
                                             bool (*edit)(char number,
                                                          uint32_t* mask),
                                             scpi_port* input) {
  uint32_t mask = nv_settings.ignored_satellites;
  bool invalid = !edit(satellites[0].value, &mask);

  for (uint8_t index = 1; index < SCPI_SOURCE_GPSYSTEM_LIST_SIZE &&
                          satellites[index].kind == SCPI_PARAMETER_DECIMAL;
       index++) {
    if (!edit(satellites[index].value, &mask)) {
      invalid = true;
    }
  }
  if (invalid) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE, scpi_input_source(input));
  } else {
    pllc_request_c(&scpi_param_SOURce_GPSystem_SATellite_TRACking_IGNore, 0,
                   mask, 0, scpi_input_source(input));
  }
}

/* The antenna delay's upper limit's margin, 0.1 ns, that :GPS:ADELay
 * adds to its descriptor's maximum (0x2db60). */
static const float SCPI_SOURCE_GPSYSTEM_DELAY_MARGIN = 1e-10f;

/* :GPS:POSition?: during a survey its estimate, -230, Data corrupt or
 * stale, while there is none; else the position held (0x2e5be). */
int32_t scpi_query_SOURce_GPSystem_POSition(void* parameters, void* response,
                                            scpi_call* call) {
  int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];
  int32_t* answered = nv_settings.position;

  if (nv_settings.douglas_state == 0) {
    if (survey_position_valid == 0) {
      return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
    }
    answered = survey_position;
  }
  copy_longs_locked((uint32_t*)answered, (uint32_t*)position,
                    SCPI_SOURCE_GPSYSTEM_POSITION_LONGS);
  scpi_source_gpsystem_location_answer(position, true, response);
  return 0;
}

/* :GPS:POSition:HOLD:LAST?: the last position held (0x2e6fe). */
int32_t scpi_query_SOURce_GPSystem_POSition_HOLD_LAST(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];

  copy_longs_locked((uint32_t*)nv_settings.position, (uint32_t*)position,
                    SCPI_SOURCE_GPSYSTEM_POSITION_LONGS);
  scpi_source_gpsystem_location_answer(position, true, response);
  return 0;
}

/* :GPS:POSition:HOLD:STATe?: the survey's state, sign-extended as
 * return_byte's char gives it (0x2e77a). */
int32_t scpi_query_SOURce_GPSystem_POSition_HOLD_STATe(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  int16_t* state = response;

  *state = (int8_t)nv_settings.douglas_state;
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore:COUNt? (0x2eff8). */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_IGNore_COUNt(
    void* parameters, void* response, scpi_call* call) {
  uint16_t* count = response;

  *count = scpi_source_gpsystem_mask_list(nv_settings.ignored_satellites, 0);
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore:STATe? n and :INCLude:STATe? n, by the
 * path word: 1 when the satellite is ignored or included as asked;
 * -222, Data out of range, for a number not 1 to 32 (0x2f068).  The
 * image tests the path word's low byte. */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_IGNore_STATe(
    void* parameters, void* response, scpi_call* call) {
  scpi_source_gpsystem_state_parameters* asked = parameters;
  uint16_t* state = response;
  bool asks_ignored = (uint8_t)asked->path_word != 0;
  uint8_t number = (uint8_t)asked->satellite.value;

  if ((uint8_t)satellite_number_valid(number) == 0) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  bool ignored = satellite_in_mask(number, nv_settings.ignored_satellites);

  *state = ignored == asks_ignored;
  return 0;
}

/* :GPS:SATellite:TRACking:INCLude:COUNt? (0x2f22a). */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_INCLude_COUNt(
    void* parameters, void* response, scpi_call* call) {
  uint16_t* count = response;

  *count = scpi_source_gpsystem_mask_list(~nv_settings.ignored_satellites, 0);
  return 0;
}

/* :GPS:ADELay:FEET: a number of feet as seconds, then :GPS:ADELay; MIN
 * and MAX as they are (0x2dbf6). */
int32_t scpi_set_SOURce_GPSystem_ADELay_FEET(void* parameters, void* response,
                                             scpi_call* call) {
  scpi_float_parameter* delay = parameters;

  if (delay->kind != SCPI_PARAMETER_CHARACTER) {
    float_multiply_in_place(&delay->value, SCPI_SOURCE_GPSYSTEM_FOOT);
  }
  return scpi_set_SOURce_GPSystem_ADELay(parameters, response, call);
}

/* :GPS:ADELay:FEET?: the antenna delay, or its MIN or MAX, in feet
 * (0x2dc28). */
int32_t scpi_query_SOURce_GPSystem_ADELay_FEET(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_word_parameter* bound = parameters;
  float* feet = response;

  *feet = float_divide(scpi_numeric_get(&scpi_param_SOURce_GPSystem_ADELay, 0,
                                        bound->kind, bound->value),
                       SCPI_SOURCE_GPSYSTEM_FOOT_NANOSECONDS);
  return 0;
}

/* :GPS:REFerence:BIAS?: the bias, or its MIN or MAX, in seconds
 * (0x2da90). */
int32_t scpi_query_SOURce_GPSystem_REFerence_BIAS(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_word_parameter* bound = parameters;
  float* seconds = response;

  *seconds = float_multiply(
      scpi_numeric_get(&scpi_param_SOURce_GPSystem_REFerence_BIAS, 0,
                       bound->kind, bound->value),
      SCPI_SOURCE_GPSYSTEM_NANOSECOND);
  return 0;
}

/* :GPS:POSition <location> | SURVey | LAST: holds the position given, the
 * survey's estimate or the last held, to pllc, and ends the survey
 * (0x2e17e).  LAST and SURVey take no more parameters, -108, Parameter
 * not allowed, and a <location> all of them, -109, Missing parameter.
 * SURVey with no estimate is -221, Settings conflict; while a survey runs
 * with positions averaged, it goes to pllc as an action instead. */
int32_t scpi_set_SOURce_GPSystem_POSition(void* parameters, void* response,
                                          scpi_call* call) {
  scpi_source_gpsystem_location_parameters* location = parameters;
  int16_t first = location->latitude.hemisphere.value;
  request_source source = scpi_input_source(call->input);
  int32_t* staged = scpi_staged[source];

  if (first == SCPI_SOURCE_GPSYSTEM_LAST ||
      first == SCPI_SOURCE_GPSYSTEM_SURVEY) {
    if (scpi_source_gpsystem_left_out(location) !=
        SCPI_SOURCE_GPSYSTEM_LOCATION_REST) {
      status_report_from(STATUS_PARAMETER_NOT_ALLOWED, source);
      return 0;
    }
    if (first == SCPI_SOURCE_GPSYSTEM_LAST) {
      copy_longs_locked((uint32_t*)nv_settings.position, (uint32_t*)staged,
                        SCPI_SOURCE_GPSYSTEM_POSITION_LONGS);
    } else if (survey_position_valid == 0) {
      status_report_from(STATUS_SETTINGS_CONFLICT, source);
      return 0;
    } else if (nv_settings.douglas_state == 0 && gps.average_count != 0) {
      pllc_request_b(&scpi_action_SOURce_GPSystem_POSition, 0, source);
      return 0;
    } else {
      copy_longs_locked((uint32_t*)survey_position, (uint32_t*)staged,
                        SCPI_SOURCE_GPSYSTEM_POSITION_LONGS);
    }
  } else {
    if (scpi_source_gpsystem_left_out(location) != 0) {
      status_report_from(STATUS_MISSING_PARAMETER, source);
      return 0;
    }
    int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];
    /* The height's limits are the descriptor's third (0x2e4e0). */
    static const uint8_t HEIGHT = 2;

    scpi_source_gpsystem_location(location,
                                  &scpi_param_SOURce_GPSystem_POSition, HEIGHT,
                                  source, position);
    for (uint8_t index = 0; index < SCPI_SOURCE_GPSYSTEM_POSITION_LONGS;
         index++) {
      staged[index] = position[index];
    }
  }
  pllc_request_d(&scpi_param_SOURce_GPSystem_POSition,
                 SCPI_SOURCE_GPSYSTEM_POSITION_LONGS - 1, (uint32_t)staged,
                 source);
  pllc_request_a(&scpi_param_DOUGlas_STATe, 0, 1, source);
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore: adds satellites to the ignore list
 * (0x2eef6). */
int32_t scpi_set_SOURce_GPSystem_SATellite_TRACking_IGNore(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  scpi_source_gpsystem_ignore_edit(parameters, satellite_mask_add, call->input);
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore? (0x2efba). */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_IGNore(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)scpi_input_source(call->input)];

  scpi_source_gpsystem_list_answer(
      list,
      scpi_source_gpsystem_mask_list(nv_settings.ignored_satellites, list),
      response);
  return 0;
}

/* :GPS:SATellite:TRACking:INCLude: removes satellites from the ignore
 * list (0x2f150). */
int32_t scpi_set_SOURce_GPSystem_SATellite_TRACking_INCLude(void* parameters,
                                                            void* response,
                                                            scpi_call* call) {
  scpi_source_gpsystem_ignore_edit(parameters, satellite_mask_remove,
                                   call->input);
  return 0;
}

/* :GPS:SATellite:TRACking:INCLude?: those not ignored (0x2f214). */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking_INCLude(void* parameters,
                                                              void* response,
                                                              scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)scpi_input_source(call->input)];

  scpi_source_gpsystem_list_answer(
      list,
      scpi_source_gpsystem_mask_list(~nv_settings.ignored_satellites, list),
      response);
  return 0;
}

/* :GPS:ADELay: holds the loop in temporary mode, then sends the antenna
 * delay in nanoseconds, MIN or MAX the descriptor's limits; a number
 * clamped to them, the upper a tenth of a nanosecond over (0x2dac2). */
int32_t scpi_set_SOURce_GPSystem_ADELay(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_float_parameter* delay = parameters;
  scpi_numeric_param* param = &scpi_param_SOURce_GPSystem_ADELay;
  int32_t nanoseconds;

  pllc_request_b(&scpi_action_temporary_hold, 0,
                 scpi_input_source(call->input));
  if (delay->kind == SCPI_PARAMETER_CHARACTER) {
    int32_t* limit = delay->keyword == SCPI_SOURCE_GPSYSTEM_MINIMUM
                         ? param->minimum
                         : param->maximum;

    nanoseconds = *limit;
  } else {
    int32_t* minimum = param->minimum;
    int32_t* maximum = param->maximum;
    scpi_step step = scpi_source_gpsystem_step(
        scpi_source_gpsystem_float_bits(SCPI_SOURCE_GPSYSTEM_NANOSECOND),
        load_long);
    float lowest = float_multiply(integer_to_float(*minimum),
                                  SCPI_SOURCE_GPSYSTEM_NANOSECOND);
    float highest = float_add(float_multiply(integer_to_float(*maximum),
                                             SCPI_SOURCE_GPSYSTEM_NANOSECOND),
                              SCPI_SOURCE_GPSYSTEM_DELAY_MARGIN);

    nanoseconds =
        clamp_float_to_step_double(&delay->value, 0, &lowest, &highest, &step,
                                   scpi_input_source(call->input));
  }
  pllc_request_c(param, 0, nanoseconds, 0, scpi_input_source(call->input));
  return 0;
}

/* :GPS:REFerence:BIAS: the GPS reference's bias in nanoseconds, MIN or
 * MAX the descriptor's limits (0x2d9b2). */
int32_t scpi_set_SOURce_GPSystem_REFerence_BIAS(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_float_parameter* bias = parameters;
  scpi_numeric_param* param = &scpi_param_SOURce_GPSystem_REFerence_BIAS;
  int32_t nanoseconds;

  if (bias->kind == SCPI_PARAMETER_CHARACTER) {
    int32_t* limit = bias->keyword == SCPI_SOURCE_GPSYSTEM_MINIMUM
                         ? param->minimum
                         : param->maximum;

    nanoseconds = *limit;
  } else {
    nanoseconds = scpi_source_gpsystem_steps(&bias->value, param, 0,
                                             SCPI_SOURCE_GPSYSTEM_NANOSECOND,
                                             scpi_input_source(call->input));
  }
  pllc_request_c(param, 0, nanoseconds, 0, scpi_input_source(call->input));
  return 0;
}

/* The satellite lists' limits by the list: the numbers a mask holds, the
 * most it lists and the first number less one; other lists leave them
 * as they are (0x2ea92, its jump table at 0x2eab6). */
void satellite_list_limits(satellite_list kind, uint8_t* id_count,
                           uint8_t* max_listed, uint8_t* id_base) {
  /* The most a list of tracked or of visible satellites holds. */
  static const uint8_t LISTED = 12;
  static const uint8_t PSEUDOLITES = 5;

  switch (kind) {
    case SATELLITE_LIST_TRACKING:
      *id_count = SCPI_SOURCE_GPSYSTEM_SATELLITES;
      *max_listed = LISTED;
      *id_base = 0;
      break;
    case SATELLITE_LIST_TRACKING_PSEUDOLITES:
      *id_count = PSEUDOLITES;
      *max_listed = LISTED;
      *id_base = SCPI_SOURCE_GPSYSTEM_SATELLITES;
      break;
    case SATELLITE_LIST_VISIBLE:
      *id_count = SCPI_SOURCE_GPSYSTEM_SATELLITES;
      *max_listed = LISTED;
      *id_base = 0;
      break;
    case SATELLITE_LIST_ALL:
      *id_count = SCPI_SOURCE_GPSYSTEM_SATELLITES;
      *max_listed = *id_count;
      *id_base = 0;
      break;
    default:
      break;
  }
}

/* :GPS:REFerence:VALid?: 1 while the GPS reference is valid with its 1
 * PPS alive, or questionable (0x2e9de). */
int32_t scpi_query_SOURce_GPSystem_REFerence_VALid(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const gps_reference_record* reference = &gps.references[0];
  uint16_t* valid = response;

  *valid =
      (reference->valid == GPS_REFERENCE_VALID && reference->pps_dead == 0) ||
      reference->valid == GPS_REFERENCE_QUESTIONABLE;
  return 0;
}

/* :GPS:SATellite:TRACking:DATA?, :CHOE? too: the satellites used, each
 * its number, signal strength, elevation and azimuth, -001 for a
 * strength of 0 or below and for the two angles of one with no
 * position, a lone "+00,-001,-001,-001" for none; fails as stale data
 * while satellites_used_list has no list (0x2ecb4).  New in this
 * image. */
int32_t scpi_query_CHOE(void* parameters, void* response, scpi_call* call) {
  /* the list's entries, satellites_used_list's most */
  enum { SATELLITES = 12 };
  char* text = scpi_response_texts[(uint8_t)scpi_input_source(call->input)];
  char** answer = response;
  uint8_t count;
  char item[11];
  satellite_list_entry list[SATELLITES];
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (!satellites_used_list(&count, list)) {
    set_interrupt_mask(sr);
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  set_interrupt_mask(sr);
  text[0] = '\0';
  if (count == 0) {
    libc_copy(text, "+00,-001,-001,-001");
  } else {
    for (uint8_t index = 0; index < count && index < SATELLITES; index++) {
      const satellite_list_entry* entry = &list[index];

      if (index != 0) {
        strcat(text, ",");
      }
      sprintf(item, "%+03d,", (uint32_t)entry->satellite);
      strcat(text, item);
      if (entry->level > 0) {
        sprintf(item, "%+04d,", (int32_t)entry->level);
      } else {
        libc_copy(item, "-001,");
      }
      strcat(text, item);
      if (entry->cell.cell.status == SATELLITE_CELL_POSITION) {
        sprintf(item, "%+04d,%+04d", (uint32_t)entry->cell.cell.elevation,
                (uint32_t)entry->cell.cell.azimuth);
      } else {
        libc_copy(item, "-001,-001");
      }
      strcat(text, item);
    }
  }
  *answer = text;
  return 0;
}

/* :GPS:INITial:POSition's parameters as its setter reads them: a word
 * and then the <location>, the 66 bytes its node gives them (0x5f074),
 * where the nine parameters the node lists lay the <location> out from
 * the first byte, in 64 (0x567fc). */
typedef struct scpi_source_gpsystem_initial_position_parameters {
  int16_t field_00;                                  /* +0x00 */
  scpi_source_gpsystem_location_parameters location; /* +0x02 */
} scpi_source_gpsystem_initial_position_parameters;

/* :GPS:INITial:POSition <location> (0x2ddc6): -113, Undefined header,
 * when its parameters' first word is not 0, by the node's list the
 * latitude hemisphere's kind (0x2de24); -221, Settings conflict, while
 * the receiver holds its position or surveys; else the latitude,
 * longitude and height to pllc, each by its own descriptor.  After them
 * it has a fourth request for that word not 0, which it has returned
 * for by then (0x2e0d4). */
int32_t scpi_set_SOURce_GPSystem_INITial_POSition(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_source_gpsystem_initial_position_parameters* initial = parameters;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  bool settable = position_settable();

  set_interrupt_mask(sr);
  request_source source = scpi_input_source(call->input);

  if (initial->field_00 != 0) {
    status_report_from(STATUS_UNDEFINED_HEADER, source);
    return 0;
  }
  if (!settable) {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
    return 0;
  }
  int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];

  scpi_source_gpsystem_location(
      &initial->location, &scpi_param_SOURce_GPSystem_INITial_POSition_height,
      0, source, position);
  pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_latitude, 0,
                 position[0], 0, source);
  pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_longitude, 0,
                 position[1], 0, source);
  pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_height, 0,
                 position[2], 0, source);
  if (initial->field_00 != 0) {
    pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_request, 0, 0,
                   0, source);
  }
  return 0;
}
