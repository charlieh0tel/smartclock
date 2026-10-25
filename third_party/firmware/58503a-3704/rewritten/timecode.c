/* The time code's checksum and TFOM digit, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The time code").  At each 1PPS
 * timecode_update (0x13da8) builds the :PTIMe:TCODe? message
 * (097-z3801-01, "Detailed Description of the Two Time Code Formats") in
 * tcode_message, which timecode_schedule (0x140d0) sends a delay after
 * the 1PPS's capture, output compare 3 counting it off, through the
 * routine tcode_request leaves in tcode_send, drtb_send_async (0x13d6a);
 * the 58503A has no ES message, which the Z3805A's sends so.  The
 * exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* The delay after the 1PPS capture, in GPT ticks, at which
 * timecode_update sends the time code (0x13ff6), as the Z3805A's es_build
 * does its ES message. */
static const uint32_t TIMECODE_DELAY = 0x28f5;

/* An array size, so #define. */
#define TIMECODE_CHECKSUM_SIZE 3

/* The digits of a byte's tens and ones. */
static uint8_t timecode_tens(uint32_t value) { return value / 10; }

static uint8_t timecode_ones(uint32_t value) {
  return value - timecode_tens(value) * 10;
}

/* The time code's cc: its characters' sum (0x13d16). */
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
 * not below it (0x12aca). */
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
 * and kept to 0 to 9 (0x13d86). */
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

/* The time code's length, and its checksum appended. */
static uint8_t timecode_finish(void) {
  char checksum[TIMECODE_CHECKSUM_SIZE];
  uint8_t length = 0;

  sprintf(checksum, "%02X", (uint8_t)tcode_checksum(tcode_message));
  strcat(tcode_message, checksum);
  while (tcode_message[length] != 0) {
    length++;
  }
  return length;
}

/* Whether an output compare at a delay after the 1PPS capture, input
 * capture 1, is already past: the free-running count, taken past the
 * capture across its wrap, has reached it. */
static bool timecode_compare_passed(uint32_t compare) {
  static const uint32_t TIMER_WRAP = 0x10000;
  uint32_t now = gpt.tcnt;

  if (now < gpt.tic[0]) {
    now += TIMER_WRAP;
  }
  return compare <= now;
}
