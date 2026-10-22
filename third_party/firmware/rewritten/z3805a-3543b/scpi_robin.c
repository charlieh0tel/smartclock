/* The SCPI :ROBin tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/robin`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

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
