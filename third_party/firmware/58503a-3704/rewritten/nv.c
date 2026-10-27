/* The NV register, the settings it keeps and its two slots, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `services/nv`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

static const uint32_t RECORDS_ERASED_LONG = 0xffffffffu;

/* The NV register's settings, packed into longs most significant bit
 * first; a value that would cross a long's end starts the next. */
typedef struct records_bit_stream {
  uint32_t* next; /* the long to store or load next */
  uint32_t bits;  /* the long being filled or emptied */
  uint8_t used;   /* its bits filled or emptied */
} records_bit_stream;

/* Stores the long being filled, its bits moved to the top. */
static void records_pack_flush(records_bit_stream* stream) {
  if (stream->used > 0 && stream->used < CPU_LONG_BITS) {
    stream->bits <<= CPU_LONG_BITS - stream->used;
  }
  *stream->next++ = stream->bits;
  stream->bits = 0;
  stream->used = 0;
}

/* The low n bits, n 0 to 32: the image's low_bit_masks (0x314b2). */
static const uint32_t records_low_bit_masks[] = {
    0x0,        0x1,        0x3,        0x7,       0xf,       0x1f,
    0x3f,       0x7f,       0xff,       0x1ff,     0x3ff,     0x7ff,
    0xfff,      0x1fff,     0x3fff,     0x7fff,    0xffff,    0x1ffff,
    0x3ffff,    0x7ffff,    0xfffff,    0x1fffff,  0x3fffff,  0x7fffff,
    0xffffff,   0x1ffffff,  0x3ffffff,  0x7ffffff, 0xfffffff, 0x1fffffff,
    0x3fffffff, 0x7fffffff, 0xffffffff,
};

/* Packs a value's low bits. */
static void records_pack(records_bit_stream* stream, uint32_t value,
                         uint8_t width) {
  if ((uint8_t)(width + stream->used) > CPU_LONG_BITS) {
    records_pack_flush(stream);
  }
  stream->bits = (width >= CPU_LONG_BITS ? 0 : stream->bits << width) |
                 (value & records_low_bit_masks[width]);
  stream->used += width;
}

/* Unpacks a value of so many bits.  For 32 the image takes the long as it
 * is; rotating it a whole turn is the same. */
static uint32_t records_unpack(records_bit_stream* stream, uint8_t width) {
  if ((uint8_t)(width + stream->used) > CPU_LONG_BITS) {
    stream->used = 0;
    stream->bits = *stream->next++;
  }
  stream->bits = rotate_left(stream->bits, width);
  stream->used += width;
  return stream->bits & records_low_bit_masks[width];
}

/* A value's bits in the NV register, from a descriptor's widths. */
static uint8_t records_nv_width(const int8_t* widths, int32_t index) {
  return (uint8_t)abs(widths[index]);
}
