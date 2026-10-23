/* The SCPI :SENSe tree, :SENSe:TST1:EDGE, which this firmware does not
 * support: its handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/sense`).  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* :SENSe:TST1:EDGE, RISing or FALLing, which reports an undefined header
 * and succeeds (0x2f2ae). */
int32_t scpi_set_SENSe_TST1_EDGE(void* parameters, void* response,
                                 scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :SENSe:TST1:EDGE?, which fails as the four-letter stubs do, the call
 * passed for the response (scpi_unsupported.c) (0x2f2ce). */
int32_t scpi_query_SENSe_TST1_EDGE(void* parameters, void* response,
                                   scpi_call* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}
