/* The SCPI :FORMat tree, :FORMat:DATA, the data format of the :ROBin
 * queries' responses (097-59551-02, "Reading Time Stamps"): its
 * handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/format`).  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x2eeb8).  Another format leaves the response as it is. */
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
