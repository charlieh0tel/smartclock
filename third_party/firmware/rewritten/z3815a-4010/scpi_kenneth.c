/* scpi_kenneth.c's functions that are this image's own: the rest are
 * common/scpi_kenneth.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* :KENneth:MEASured?: the measured time uncertainty and its resolution,
 * a signed power of ten; -230, Data corrupt or stale, while
 * kenneth_measured_get has none.  Unlike :KENneth:PREDicted?, it leaves
 * the interrupt mask as it is (0x43e38). */
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
