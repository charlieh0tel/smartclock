/* From the 58503B 1.01.04's rewritten/scpi_format_data.c, the Z3805A
 * 1.01.03's own beside common/'s: of its functions, those the Z3805A
 * 1.01.03's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list").  Its comments'
 * addresses are the Z3805A 1.01.03's, but where they name another image.
 * Each keeps its generated prototype. */

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x30610).  Another format leaves the response as it is. */
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
