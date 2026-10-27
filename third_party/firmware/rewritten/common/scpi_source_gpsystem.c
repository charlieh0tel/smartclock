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

/* An angle: its hemisphere, then degrees, minutes and seconds. */
typedef struct scpi_source_gpsystem_angle_parameters {
  scpi_word_parameter hemisphere; /* +0x00 */
  scpi_long_parameter degrees;    /* +0x04 */
  scpi_long_parameter minutes;    /* +0x0c */
  scpi_float_parameter seconds;   /* +0x14 */
} scpi_source_gpsystem_angle_parameters;

/* A <location> (097-z3801-01, page 4-6), :POSition's and
 * :INITial:POSition's: a latitude, a longitude and the height above
 * mean sea level in meters.  :POSition's first keyword may instead be
 * LAST or SURVey, the rest left out. */
typedef struct scpi_source_gpsystem_location_parameters {
  scpi_source_gpsystem_angle_parameters latitude;  /* +0x00 */
  scpi_source_gpsystem_angle_parameters longitude; /* +0x1c */
  scpi_float_parameter height;                     /* +0x38 */
} scpi_source_gpsystem_location_parameters;

/* The hemisphere keywords' values in the nodes' lists: N and S
 * (scpi_choices_query_RLOC_2), E and W (scpi_choices_query_RLOC_6), and
 * :POSition's LAST and SURVey after N and S
 * (scpi_choices_set_SOURce_GPSystem_POSition_1). */
enum {
  SCPI_SOURCE_GPSYSTEM_NORTH = 0,
  SCPI_SOURCE_GPSYSTEM_SOUTH = 1,
  SCPI_SOURCE_GPSYSTEM_LAST = 2,
  SCPI_SOURCE_GPSYSTEM_SURVEY = 3,
  SCPI_SOURCE_GPSYSTEM_EAST = 2,
  SCPI_SOURCE_GPSYSTEM_WEST = 3,
};

/* The antenna delay's unit, a nanosecond, in seconds. */
static const float SCPI_SOURCE_GPSYSTEM_NANOSECOND = 1e-9f;

/* The response items, as the formatters read them: a short and a float
 * each take two bytes more (scpi_format_short, scpi_format_float). */
typedef struct scpi_source_gpsystem_real_item {
  float value;    /* +0x00 */
  int16_t unused; /* +0x04 */
} scpi_source_gpsystem_real_item;

typedef struct scpi_source_gpsystem_angle_items {
  int16_t hemisphere;                     /* +0x00 */
  scpi_short_item degrees;                /* +0x02 */
  scpi_short_item minutes;                /* +0x06 */
  scpi_source_gpsystem_real_item seconds; /* +0x0a */
} scpi_source_gpsystem_angle_items;

/* A <location> as the position queries answer it (their nodes' items,
 * scpi_node_SOURce_PTIMe_GPSystem_POSition_ACTual's among them). */
typedef struct scpi_source_gpsystem_location_items {
  scpi_source_gpsystem_angle_items latitude;  /* +0x00 */
  scpi_source_gpsystem_angle_items longitude; /* +0x10 */
  scpi_source_gpsystem_real_item height;      /* +0x20 */
} scpi_source_gpsystem_location_items;

/* A position as the receiver and the settings keep it: latitude and
 * longitude in milliarcseconds, height in centimeters. */
#define SCPI_SOURCE_GPSYSTEM_POSITION_LONGS 3

/* A float's bits, which a scpi_step holds as its value. */
static uint32_t scpi_source_gpsystem_float_bits(float value) {
  union libc_float step = {.value = value};

  return step.bits;
}

/* A step record of a value and its routine. */
static scpi_step scpi_source_gpsystem_step(uint32_t value,
                                           scpi_step_function* function) {
  scpi_step step = {
      .value = value,
      .function = function,
  };

  return step;
}

/* A real parameter in steps of unit, within a descriptor's long limits
 * at index, each times unit, by clamp_float_to_step_double. */
static int32_t scpi_source_gpsystem_steps(float* value,
                                          scpi_numeric_param* param,
                                          uint8_t index, float unit,
                                          request_source source) {
  int32_t* minimum = param->minimum;
  int32_t* maximum = param->maximum;
  scpi_step step = scpi_source_gpsystem_step(
      scpi_source_gpsystem_float_bits(unit), load_long);
  float lowest = float_multiply(integer_to_float(minimum[index]), unit);
  float highest = float_multiply(integer_to_float(maximum[index]), unit);

  return clamp_float_to_step_double(value, 0, &lowest, &highest, &step, source);
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
                   angle->value, 0, angle->value, return_zero(call->input));
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
  request_source source = return_zero(input);

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

/* An angle's degrees, minutes and seconds as milliarcseconds, the
 * hemisphere aside: the degrees clamped to 0 to the limit, the minutes
 * to 0 to 59 and the seconds to 0 to 59.999 in steps of 0.001, each
 * clamp reporting -222, Data out of range; at the limit the minutes and
 * seconds are 0, and either given nonzero is reported so
 * (scpi_set_SOURce_GPSystem_INITial_POSition+0x6e to
 * scpi_set_SOURce_GPSystem_INITial_POSition+0x17e). */
static int32_t scpi_source_gpsystem_angle(
    scpi_source_gpsystem_angle_parameters* angle, int32_t degrees_limit,
    request_source source) {
  static const int32_t MINUTES_LIMIT = 59;
  static const float SECONDS_LIMIT = 59.999f;
  static const float MILLISECOND = 0.001f;
  /* A float's bits but its sign (the lsl at
   * scpi_set_SOURce_GPSystem_INITial_POSition+0xe6). */
  static const uint32_t MAGNITUDE_BITS = 0x7fffffff;
  scpi_step unit_step = scpi_source_gpsystem_step(1, load_long_unit_step);
  scpi_step milli_step = scpi_source_gpsystem_step(
      scpi_source_gpsystem_float_bits(MILLISECOND), load_long);
  int32_t lowest = 0;
  int32_t highest = degrees_limit;
  int32_t degrees = clamp_to_limits(&angle->degrees.value, 0, &lowest, &highest,
                                    &unit_step, source);
  int32_t minutes = 0;
  float seconds = 0.0f;

  if (degrees == highest) {
    if (angle->minutes.value != 0) {
      status_report_from(STATUS_DATA_OUT_OF_RANGE, source);
    }
    if ((scpi_source_gpsystem_float_bits(angle->seconds.value) &
         MAGNITUDE_BITS) != 0) {
      status_report_from(STATUS_DATA_OUT_OF_RANGE, source);
    }
  } else {
    lowest = 0;
    highest = MINUTES_LIMIT;
    minutes = clamp_to_limits(&angle->minutes.value, 0, &lowest, &highest,
                              &unit_step, source);
    float seconds_lowest = 0.0f;
    float seconds_highest = SECONDS_LIMIT;

    /* Its prototype takes the step as a long. */
    seconds = clamp_float_to_step_and_limits(&angle->seconds.value, 0,
                                             &seconds_lowest, &seconds_highest,
                                             (int32_t)&milli_step, source);
  }
  int32_t mas;

  dms_to_mas(&mas, (uint16_t)degrees, (uint8_t)minutes,
             float_to_double(seconds));
  return mas;
}

/* The limits of a latitude's and a longitude's degrees. */
static const int32_t SCPI_SOURCE_GPSYSTEM_LATITUDE_LIMIT = 90;

static const int32_t SCPI_SOURCE_GPSYSTEM_LONGITUDE_LIMIT = 180;

/* A height's step and its limits' unit, a centimeter in meters. */
static const float SCPI_SOURCE_GPSYSTEM_CENTIMETER = 0.01f;

/* A <location> as milliarcseconds, south and west negative, and
 * centimeters, the height within the descriptor's limits at index. */
static void scpi_source_gpsystem_location(
    scpi_source_gpsystem_location_parameters* location,
    scpi_numeric_param* height_param, uint8_t height_index,
    request_source source,
    int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS]) {
  position[0] = scpi_source_gpsystem_angle(
      &location->latitude, SCPI_SOURCE_GPSYSTEM_LATITUDE_LIMIT, source);
  position[1] = scpi_source_gpsystem_angle(
      &location->longitude, SCPI_SOURCE_GPSYSTEM_LONGITUDE_LIMIT, source);
  position[2] = scpi_source_gpsystem_steps(
      &location->height.value, height_param, height_index,
      SCPI_SOURCE_GPSYSTEM_CENTIMETER, source);
  if (location->latitude.hemisphere.value == SCPI_SOURCE_GPSYSTEM_SOUTH) {
    position[0] = -position[0];
  }
  if (location->longitude.hemisphere.value == SCPI_SOURCE_GPSYSTEM_WEST) {
    position[1] = -position[1];
  }
}

/* :GPS:INITial:POSition <location>: its latitude, longitude and height
 * to pllc, each by its own descriptor, unless the receiver holds its
 * position or surveys, -221, Settings conflict. */
int32_t scpi_set_SOURce_GPSystem_INITial_POSition(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_numeric_param* height_param =
      &scpi_param_SOURce_GPSystem_INITial_POSition_height;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  /* The image tests the result's low byte
   * (scpi_set_SOURce_GPSystem_INITial_POSition+0x3e). */
  bool settable = (uint8_t)position_settable() != 0;

  set_interrupt_mask(sr);
  if (!settable) {
    status_report_from(STATUS_SETTINGS_CONFLICT, return_zero(call->input));
    return 0;
  }
  int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS];

  scpi_source_gpsystem_location(parameters, height_param, 0,
                                return_zero(call->input), position);
  pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_latitude, 0,
                 position[0], 0, return_zero(call->input));
  pllc_request_c(&scpi_param_SOURce_GPSystem_INITial_POSition_longitude, 0,
                 position[1], 0, return_zero(call->input));
  pllc_request_c(height_param, 0, position[2], 0, return_zero(call->input));
  return 0;
}

/* An angle's response items: the hemisphere by its sign, and the
 * degrees, minutes and seconds of the milliarcseconds shown, which the
 * caller gives as the angle or its magnitude. */
static void scpi_source_gpsystem_angle_answer(
    int32_t mas, int32_t shown, int16_t positive, int16_t negative,
    scpi_source_gpsystem_angle_items* items) {
  uint16_t degrees;
  uint8_t minutes;
  double seconds;

  mas_to_dms(shown, &degrees, &minutes, &seconds);
  items->hemisphere = mas < 0 ? negative : positive;
  items->degrees.value = degrees;
  items->minutes.value = minutes;
  items->seconds.value = double_to_float(seconds);
}

/* A position's response items, its angles by their magnitudes or as
 * they are, and its height in meters. */
static void scpi_source_gpsystem_location_answer(
    const int32_t position[SCPI_SOURCE_GPSYSTEM_POSITION_LONGS],
    bool magnitudes, scpi_source_gpsystem_location_items* items) {
  static const double CENTIMETERS_PER_METER = 100.0;
  int32_t latitude = position[0];
  int32_t longitude = position[1];

  scpi_source_gpsystem_angle_answer(
      latitude, magnitudes ? labs(latitude) : latitude,
      SCPI_SOURCE_GPSYSTEM_NORTH, SCPI_SOURCE_GPSYSTEM_SOUTH, &items->latitude);
  scpi_source_gpsystem_angle_answer(
      longitude, magnitudes ? labs(longitude) : longitude,
      SCPI_SOURCE_GPSYSTEM_EAST, SCPI_SOURCE_GPSYSTEM_WEST, &items->longitude);
  items->height.value = double_to_float(double_divide_reversed(
      CENTIMETERS_PER_METER, integer_to_double(position[2])));
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

/* The satellite numbers a mask holds, 1 to 32, its bit 0 satellite 1. */
static const uint8_t SCPI_SOURCE_GPSYSTEM_SATELLITES = 32;

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

/* Answers a list of so many satellites, or for none the list 0
 * (097-z3801-01, page 4-7). */
static void scpi_source_gpsystem_list_answer(int16_t* list, uint32_t listed,
                                             scpi_array* array) {
  array->count = listed;
  if (listed == 0) {
    array->count = 1;
    list[0] = 0;
  }
  array->values = list;
}

/* :GPS:SATellite:TRACking?: the GPS satellites and then the pseudolites
 * the channels use, as many of each as are in use; -230, Data corrupt or
 * stale, while unknown.  The image tests the result's low
 * byte. */
int32_t scpi_query_SOURce_GPSystem_SATellite_TRACking(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)return_zero(call->input)];
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
                 return_zero(call->input));
  return 0;
}

/* :GPS:SATellite:TRACking:IGNore:NONE, and :INCLude:ALL: ignores none. */
int32_t scpi_set_SOURce_GPSystem_SATellite_TRACking_IGNore_NONE(
    void* parameters, void* response, scpi_call* call) {
  pllc_request_c(&scpi_param_SOURce_GPSystem_SATellite_TRACking_IGNore, 0, 0, 0,
                 return_zero(call->input));
  return 0;
}

/* :GPS:SATellite:VISible:PREDicted?: the satellites the almanac predicts
 * visible, as many as it counts; -230, Data corrupt or stale, while
 * unknown.  The image tests the result's low byte. */
int32_t scpi_query_SOURce_GPSystem_SATellite_VISible_PREDicted(
    void* parameters, void* response, scpi_call* call) {
  int16_t* list = scpi_response_lists[(uint8_t)return_zero(call->input)];
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
