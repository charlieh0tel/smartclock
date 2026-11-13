/* From the Z3815A 4010's rewritten/scpi_kenneth.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* :KENneth:MEASured?: the measured time uncertainty and its resolution,
 * a signed power of ten; -230, Data corrupt or stale, while
 * kenneth_measured_get has none.  Unlike :KENneth:PREDicted?, it leaves
 * the interrupt mask as it is (0x40926). */
int32_t scpi_query_KENneth_MEASured(void* parameters, void* response,
                                    scpi_call* call) {
  scpi_float_resolution* uncertainty = response;
  int8_t resolution;

  if (kenneth_measured_get(&uncertainty->value, &resolution) == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  uncertainty->resolution = resolution;
  return 0;
}
