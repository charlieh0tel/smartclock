/* SCPI command handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/scpi/README.md).  The
 * exported C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* Makes a :ROBin query's response of its points, by its format, in the
 * index'th record of that kind (0x29a68). */
void robin_store(scpi_engine_array_record* record, int16_t* points,
                 int32_t count, robin_format format, uint8_t index) {
  /* The :ROBin queries' responses, two of each kind, unnamed in the
   * export: blocks at 0x102a26 and arrays of shorts at 0x102a36
   * (0x29a74, 0x29a7a). */
  static scpi_engine_block* const ROBIN_BLOCKS = (scpi_engine_block*)0x102a26;
  static scpi_engine_array* const ROBIN_ARRAYS = (scpi_engine_array*)0x102a36;

  switch (format) {
    case ROBIN_BLOCK: {
      scpi_engine_block* block = &ROBIN_BLOCKS[index];

      block->length = count * sizeof *points;
      block->data = points;
      record->kind = SCPI_ARRAY_BLOCK;
      record->array = block;
      break;
    }
    case ROBIN_SHORTS: {
      scpi_engine_array* array = &ROBIN_ARRAYS[index];

      array->count = count;
      array->values = points;
      record->kind = SCPI_ARRAY_SHORTS;
      record->array = array;
      break;
    }
  }
}

/* 0: RTCM corrections are not supported (0x29b08). */
uint8_t rtcm_supported(void) { return 0; }
