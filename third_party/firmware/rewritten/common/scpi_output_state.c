/* The :OUTPut:STATe command's handler, the same in each image that
 * includes this file (docs/firmware/rewrite.md, "Shared C", "A file for
 * some images").  An image whose tree has no such command leaves the file
 * out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:STATe: hands pllc the new state. */
int32_t scpi_set_OUTPut_STATe(void* parameters, void* response,
                              scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_OUTPut_STATe, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}
