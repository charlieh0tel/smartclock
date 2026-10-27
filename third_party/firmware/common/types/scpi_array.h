/* scpi_array and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A response's array, as scpi_format_double_array and _short_array take
 * it. */
typedef struct scpi_array {
  int32_t count; /* +0x00 */
  void* values;  /* +0x04: doubles or shorts */
} scpi_array;

/* Doubles each with its resolution, as scpi_format_resolution_array
 * takes them. */
typedef struct scpi_resolution_array {
  int32_t count;        /* +0x00 */
  double* values;       /* +0x04 */
  int16_t* resolutions; /* +0x08 */
} scpi_resolution_array;

/* A definite-length block, as scpi_format_block takes it. */
typedef struct scpi_block {
  uint32_t length; /* +0x00 */
  void* data;      /* +0x04: its bytes */
} scpi_block;

/* An array response's kind, scpi_array_record.kind, by the jump table
 * at scpi_format_array+0x1c. */
typedef enum __attribute__((packed)) scpi_array_kind {
  SCPI_ARRAY_BLOCK = 0,   /* a scpi_block */
  SCPI_ARRAY_DOUBLES = 1, /* a scpi_array of doubles */
  SCPI_ARRAY_SHORTS = 2   /* a scpi_array of shorts */
} scpi_array_kind;

/* An array response by its kind, as scpi_format_array takes it and
 * robin_store makes it. */
typedef struct scpi_array_record {
  scpi_array_kind kind; /* +0x00 */
  uint8_t unused;
  void* array; /* +0x02: a scpi_block or scpi_array */
} scpi_array_record;

/* A double scpi_format_double_precision writes, to so many digits or
 * to a resolution. */
typedef struct scpi_precise_double {
  double value; /* +0x00 */
  char
      to_digits; /* +0x08: nonzero for precision as digits, else a resolution */
  uint8_t unused;
  int16_t precision; /* +0x0a */
} scpi_precise_double;

/* A float scpi_format_float_resolution writes, and its resolution. */
typedef struct scpi_float_resolution {
  float value;        /* +0x00 */
  int16_t resolution; /* +0x04 */
} scpi_float_resolution;
