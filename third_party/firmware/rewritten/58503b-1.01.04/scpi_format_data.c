/* From the Z3816A 4001's rewritten/scpi_format_data.c, the 58503B 1.01.04's
 * own beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x3ecaa).  Another format leaves the response as it is. */
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
