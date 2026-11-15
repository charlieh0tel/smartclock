/* scpi_format_data.c's functions that are this image's own: the rest are
 * common/scpi_format_data.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  From the 58503A 3704's
 * rewritten/scpi_format_data.c: of its functions, those the Z3816A's code
 * carries (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x3e5e2).  Another format leaves the response as it is. */
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
