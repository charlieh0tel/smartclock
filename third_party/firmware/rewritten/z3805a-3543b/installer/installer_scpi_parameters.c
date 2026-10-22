/* The installer's copies of the SCPI parameters' matching
 * (`applications/scpi/parameters`; scpi_parameters.c has the
 * primary's), whose bodies, scpi_parameters.h's, both share.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Finds the node's next parameter that takes a token of the kind and
 * marks it with the kind (installer::0x1055a2). */
int32_t installer_scpi_parameter_match(scpi_parameter** next, int32_t kind,
                                       scpi_call* call, int16_t* offset) {
  return scpi_parameters_match(next, kind, call, offset);
}

/* Marks the node's parameters left over left out, unless one is
 * required (installer::0x105628). */
uint32_t installer_scpi_parameters_finish(scpi_parameter** next,
                                          scpi_call* call, int16_t* offset) {
  return scpi_parameters_left_out(next, call, offset);
}
