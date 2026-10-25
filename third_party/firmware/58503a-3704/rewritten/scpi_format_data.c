/* The SCPI :FORMat tree, :FORMat:DATA, the data format of the :ROBin
 * queries' responses (097-59551-02, "Reading Time Stamps"): its
 * handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/format`).  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* :FORMat:DATA ASCii or INTeger (0x2eb0a). */
int32_t scpi_set_FORMat_DATA(void* parameters, void* response,
                             scpi_call* call) {
  const uint8_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_FORMat_DATA, 0,
                 scpi_two_choice(given->value, caller_d2),
                 return_zero(call->input));
  return 0;
}

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x2eb5a).  Another format leaves the response as it is. */
int32_t scpi_query_FORMat_DATA(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* choice = response;

  switch (nv_settings.format_data) {
    case FORMAT_ASCII:
      *choice = FORMAT_ASCII;
      break;
    case FORMAT_INTEGER:
      *choice = FORMAT_INTEGER;
      break;
  }
  return 0;
}
