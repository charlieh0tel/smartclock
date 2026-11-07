/* What the SCPI :SOURce:GPSystem tree's files share
 * (scpi_source_gpsystem.c and each image's own scpi_source_gpsystem.c):
 * a <location>'s parameters and response items, the hemisphere keywords,
 * the antenna delay's unit, a real parameter in steps, a location's and
 * a satellite list's answers.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

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

/* The satellite numbers a mask holds, 1 to 32, its bit 0 satellite 1. */
static const uint8_t SCPI_SOURCE_GPSYSTEM_SATELLITES = 32;

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
