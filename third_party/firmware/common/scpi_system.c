/* The SCPI :SYSTem tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/system`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.
 *
 * A :COMMunicate:SERial handler's parameters start with the port, the
 * word its SERial node adds to the path, then each parameter's kind
 * word and its value; a number's, or a query's optional MINimum or
 * MAXimum, is scpi_engine_long_parameter's. */

/* :PRESet: requests the factory settings (097-z3801-01, table 4-2). */
int32_t scpi_set_SYSTem_PRESet(void* parameters, void* response,
                               scpi_call* call) {
  request_source source = return_zero(call->input);

  pllc_request_b(&scpi_action_SYSTem_PRESet, 0, source);
  return 0;
}

/* :PRINt:LENGth?: the status screen's length. */
int32_t scpi_query_SYSTem_PRINt_LENGth(void* parameters, void* response,
                                       scpi_call* call) {
  uint16_t* length = response;

  *length = status_screen_length();
  return 0;
}
