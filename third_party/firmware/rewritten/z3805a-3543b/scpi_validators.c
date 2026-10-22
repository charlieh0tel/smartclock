/* The numeric SCPI descriptors' validators and step routines, which
 * pllc's validate_num calls through a descriptor
 * (docs/firmware/structures.md, "Numeric SCPI parameters"): the clamps
 * to a value's limits and steps, the step loads, and the report of a
 * clamped value, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/validators`).  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

/* round_float_to_step and clamp_double_to_step_and_limits: beyond so
 * many steps the step is a thousand times coarser; a value rounds half
 * away from zero. */
static const double PLLC_COARSE_STEPS = 1e9;

static const float PLLC_COARSER = 1000.0f;

static const float PLLC_HALF = 0.5f;

/* Prints that a value was clamped to its minimum (0) or maximum, and
 * reports it out of range (0x31ff8). */
void report_clamped(char too_large, uint8_t source) {
  if (too_large == 0) {
    task_dmessage("Value too small, set to min limit\r\n");
  } else {
    task_dmessage("Value too large, set to max limit\r\n");
  }
  status_report_from(STATUS_DATA_OUT_OF_RANGE, source);
}

/* A float clamped to its minimum and maximum into *clamped, reporting a
 * clamp: 1 if it clamped (0x3209c). */
uint32_t clamp_float(float* value, float* clamped, float* minimum,
                     float* maximum, uint8_t source) {
  if (float_compare(*minimum, *value) > 0) {
    report_clamped(0, source);
    *clamped = *minimum;
    return 1;
  }
  if (float_compare(*value, *maximum) > 0) {
    report_clamped(1, source);
    *clamped = *maximum;
    return 1;
  }
  *clamped = *value;
  return 0;
}

/* A float rounded to the nearest multiple of its step, half away from
 * zero, the step a thousand times coarser beyond 1e9 steps (0x3217e). */
float round_float_to_step(float* value, float* step_pointer) {
  float step = *step_pointer;
  double rounded = float_to_double(*value);
  double whole;

  float steps = double_to_float(
      double_divide_reversed_pointer(float_to_double(step), &rounded));
  if (double_compare(float_to_double(steps), PLLC_COARSE_STEPS) < 0 ||
      double_compare(float_to_double(steps), -PLLC_COARSE_STEPS) > 0) {
    step = float_multiply(step, PLLC_COARSER);
  }
  float half = double_compare(0.0, rounded) < 0 ? -PLLC_HALF : PLLC_HALF;
  double_add_in_place(&rounded, float_to_double(float_multiply(half, step)));
  double_divide_in_place(&rounded, float_to_double(step));
  modf(rounded, &whole);
  double_multiply_in_place(&whole, float_to_double(step));
  return double_to_float(whole);
}

/* A long rounded toward zero to its step, then clamped to its minimum and
 * maximum, reporting a clamp (0x322b4). */
int32_t clamp_to_limits(int32_t* value, int32_t index, int32_t* minimum,
                        int32_t* maximum, scpi_step* step, int32_t source) {
  int32_t step_size = step->function(step, value, index);

  if (step_size != 1) {
    *value = step_size * (*value / step_size);
  }
  if (*value > *maximum) {
    report_clamped(1, (uint8_t)source);
    return *maximum;
  }
  if (*value < *minimum) {
    report_clamped(0, (uint8_t)source);
    return *minimum;
  }
  return *value;
}

/* The larger of two floats (0x323b2). */
float float_max_of(float* a, float* b) {
  return float_compare(*a, *b) > 0 ? *a : *b;
}

/* The smaller of two floats (0x323d6). */
float float_min_of(float* a, float* b) {
  return float_compare(*b, *a) > 0 ? *a : *b;
}

/* A double clamped to its minimum and maximum, then divided by its step
 * and rounded half away from zero, the step a thousand times coarser
 * beyond 1e9 steps: the steps, as an int (0x32596). */
int32_t clamp_double_to_step_and_limits(double* value, uint32_t index,
                                        double* minimum, double* maximum,
                                        scpi_double_step* step_record,
                                        int32_t source) {
  double clamped;
  double whole;

  clamp_double(value, &clamped, minimum, maximum, (uint8_t)source);
  double step = step_record->function(step_record, &clamped, index);
  double steps = double_divide_reversed_pointers(&step, &clamped);
  if (double_compare(PLLC_COARSE_STEPS, steps) > 0 ||
      double_compare(-PLLC_COARSE_STEPS, steps) < 0) {
    double_multiply_in_place(&step, PLLC_COARSER);
  }
  double half = double_compare(0.0, clamped) < 0 ? -PLLC_HALF : PLLC_HALF;
  double_add_in_place(&clamped, double_multiply_pointers(&step, &half));
  double_divide_in_place(&clamped, step);
  modf(clamped, &whole);
  return double_to_integer(whole);
}

/* A step record's long, the position setters' unit step; load_long's
 * code again (0x32e26). */
uint32_t load_long_unit_step(uint32_t* step) { return *step; }

/* A step record's long, the step of most integer descriptors (0x32e2e). */
uint32_t load_long(uint32_t* step) { return *step; }
