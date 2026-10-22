/* SCPI command handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/scpi/README.md).  The
 * exported C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* Makes a :ROBin query's response of its points, by its format, in the
 * index'th record of that kind (0x29a68). */
void robin_store(scpi_array_record* record, int16_t* points, int32_t count,
                 robin_format format, uint8_t index) {
  switch (format) {
    case ROBIN_BLOCK: {
      scpi_block* block = &robin_blocks[index];

      block->length = count * sizeof *points;
      block->data = points;
      record->kind = SCPI_ARRAY_BLOCK;
      record->array = block;
      break;
    }
    case ROBIN_SHORTS: {
      scpi_array* array = &robin_arrays[index];

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
