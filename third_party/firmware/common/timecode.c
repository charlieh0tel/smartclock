/* The functions of timecode.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/timecode.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The time code's cc: its characters' sum. */
char tcode_checksum(char* text) {
  char sum = 0;

  for (; *text != 0; text++) {
    sum += *text;
  }
  return sum;
}

/* A float's bits as a long: the image compares positive floats as
 * longs, which order as the floats do. */
static int32_t timecode_float_bits(float value) {
  union {
    float value;
    int32_t bits;
  } reading = {.value = value};

  return reading.bits;
}

/* The decimal exponent of a float's magnitude, given as its bits, -30
 * to 31: a binary search of powers_of_ten_float for the first power
 * not below it. */
int16_t tfom_table_index(uint32_t tfom) {
  static const uint32_t SIGN_BIT = 0x80000000u;
  /* 10^0's index in powers_of_ten_float. */
  static const int16_t EXPONENT_ZERO = 30;
  int32_t magnitude = tfom & ~SIGN_BIT;
  int16_t low = 0;
  int16_t high = sizeof powers_of_ten_float / sizeof powers_of_ten_float[0];

  for (;;) {
    int16_t middle = (low + high) / 2;
    int32_t power;

    if (middle == low) {
      return high - EXPONENT_ZERO;
    }
    power = timecode_float_bits(powers_of_ten_float[middle]);
    if (magnitude == power) {
      return middle - EXPONENT_ZERO;
    }
    if (magnitude < power) {
      high = middle;
    } else {
      low = middle;
    }
  }
}

/* The time code's t, from pll.tfom's bits: its decimal exponent moved by 9
 * and kept to 0 to 9. */
int16_t tfom_code(uint32_t tfom) {
  /* t, 0 to 9: tfom_table_index's exponent moved by 9. */
  static const int16_t TFOM_BIAS = 9;
  static const int16_t TFOM_MAX = 9;
  int16_t code = tfom_table_index(tfom) + TFOM_BIAS;

  if (code < 0) {
    return 0;
  }
  if (code > TFOM_MAX) {
    return TFOM_MAX;
  }
  return code;
}
