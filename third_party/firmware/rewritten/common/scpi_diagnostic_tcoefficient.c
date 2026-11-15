/* The :DIAGnostic:ROSCillator:TCOefficient handlers, the same in each
 * image that includes this file (docs/firmware/rewrite.md, "Shared C", "A
 * file for some images").  An image whose tree has no such command leaves
 * the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :DIAGnostic:ROSCillator:TCOefficient, by pllc. */
int32_t scpi_set_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                     void* response,
                                                     scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;

  scpi_numeric_set(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                   given->kind, given->bound, given->real_bits, 0,
                   scpi_input_source(call->input));
  return 0;
}

/* :DIAGnostic:ROSCillator:TCOefficient?, or its bound. */
int32_t scpi_query_DIAGnostic_ROSCillator_TCOefficient(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_diagnostic_numeric* given = parameters;
  scpi_diagnostic_float* coefficient = response;

  coefficient->value =
      scpi_numeric_get(&scpi_param_DIAGnostic_ROSCillator_TCOefficient, 0,
                       given->kind, given->bound);
  return 0;
}
