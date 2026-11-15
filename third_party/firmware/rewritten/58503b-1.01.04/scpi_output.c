/* From the Z3816A 4001's rewritten/scpi_output.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* :OUTPut:STATe?, as a signed byte (0x3db72). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x4d188). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* Whether the OCXO is warm: always here, where the Z3815A 4010's reads
 * the oven monitor; powerup and efc_calibration_measure test it (0x31b0e).
 * It is under :OUTPut, where modules.tsv's seeded row puts it, image_diff
 * pairing its two instructions with the Z3815A's :OUTPut:PIN1:DELay. */
uint8_t ocxo_warm(void) { return 1; }

/* efc_calibration_measure's readings, one a step. */
enum {
  SCPI_OUTPUT_EFC_STEPS = 10,
};

/* The EFC's change a step, in DAC counts (0x4d1f8). */
static const float SCPI_OUTPUT_EFC_STEP = 116508.0f;

/* Whether a double is zero, either sign, by its bits: the sign bit
 * shifted out of the high long, or'd with the low one (0x4d2b8). */
static bool scpi_output_double_zero(double value) {
  union {
    double value;
    uint32_t longs[2];
  } bits = {.value = value};

  return ((bits.longs[0] << 1) | bits.longs[1]) == 0;
}

/* Fits the OCXO calibration's a as the least-squares slope of
 * efc_calibration.frequency_offset's readings against each step's EFC,
 * the step times SCPI_OUTPUT_EFC_STEP, b 0, and a 0 when the sums leave
 * no slope; nothing calls it (0x4d1c8). */
void efc_calibration_fit_linear(void) {
  static const double STEPS = SCPI_OUTPUT_EFC_STEPS;
  double sum_offsets = 0.0;
  double sum_efc = 0.0;
  double sum_products = 0.0;
  double sum_squares = 0.0;

  for (int32_t step = 0; step < SCPI_OUTPUT_EFC_STEPS; step++) {
    double efc = float_to_double(
        float_multiply(integer_to_float(step), SCPI_OUTPUT_EFC_STEP));

    double_add_in_place(
        &sum_offsets, float_to_double(efc_calibration.frequency_offset[step]));
    double_add_in_place(&sum_efc, efc);
    double_add_in_place(
        &sum_products,
        double_multiply_pointer(
            float_to_double(efc_calibration.frequency_offset[step]), &efc));
    double_add_in_place(&sum_squares, double_multiply_pointers(&efc, &efc));
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
  double spread =
      double_subtract_reversed(double_multiply_pointer(STEPS, &sum_squares),
                               double_multiply_pointers(&sum_efc, &sum_efc));

  if (scpi_output_double_zero(spread)) {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = 0.0f;
    return;
  }
  double covariance = double_subtract_reversed(
      double_multiply_pointer(STEPS, &sum_products),
      double_multiply_pointers(&sum_offsets, &sum_efc));

  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe =
      double_to_float(double_divide_pointer(covariance, &spread));
}
