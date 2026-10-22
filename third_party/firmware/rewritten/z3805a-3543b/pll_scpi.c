/* pllc's side of the SCPI settings, the firmware's own "pll_scpi"
 * (finish_scpi_cmd's message): the descriptors' validators and hooks, written
 * by hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C").  The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its generated
 * prototype. */

/* Does nothing: the changed hook of most numeric and setting descriptors,
 * and the done hook of some (0x12af0). */
void scpi_param_hook_nothing(void) {}

/* A double clamped to its minimum and maximum, then divided by its step
 * and rounded half away from zero, the step a thousand times coarser
 * beyond 1e9 steps: the steps, as an int (0x32596). */
int32_t clamp_double_to_step_and_limits(double* value, uint32_t index,
                                        double* minimum, double* maximum,
                                        scpi_double_step* step_record,
                                        int32_t source) {
  static const double COARSE_STEPS = 1e9;
  static const double COARSER = 1000.0;
  static const double HALF = 0.5;
  double clamped;
  double whole;

  /* clamp_double's prototype stores the double through a long pointer. */
  clamp_double(value, (uint32_t*)&clamped, minimum, maximum, (uint8_t)source);
  double step = step_record->function(step_record, &clamped, index);
  double steps = double_divide_reversed_pointers(&step, &clamped);
  if (double_compare(COARSE_STEPS, steps) > 0 ||
      double_compare(-COARSE_STEPS, steps) < 0) {
    double_multiply_in_place(&step, COARSER);
  }
  double half = double_compare(0.0, clamped) < 0 ? -HALF : HALF;
  double_add_in_place(&clamped, double_multiply_pointers(&step, &half));
  double_divide_in_place(&clamped, step);
  modf(clamped, &whole);
  return double_to_integer(whole);
}
