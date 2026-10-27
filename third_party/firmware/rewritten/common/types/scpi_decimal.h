/* The SCPI engine's decimals, units and multipliers, as the images'
 * smartclock.h declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C". Where a comment cites code, it is the
 * Z3805A 3543B's, which the 58503A's copy of the comment came from. */

/* The record scpi_double_to_decimal fills at A0, on its caller's stack:
 * the double's sign, its significant digits in ASCII, and the exponent
 * of 0.d1d2... */
typedef struct scpi_decimal {
  int16_t sign;     /* +0x00 */
  char digits[16];  /* +0x02 */
  int16_t exponent; /* +0x12 */
} scpi_decimal;

/* A unit multiplier, one of scpi_multipliers, which
 * scpi_multiplier_exponent walks to the entry whose name is 0. */
typedef struct scpi_multiplier {
  uint16_t name;   /* +0x00: one or two letters, "M" milli, "MA" mega */
  int8_t exponent; /* +0x02: the power of ten */
  uint8_t unused;  /* +0x03: 0 */
} scpi_multiplier;

/* A unit a numeric parameter accepts: one entry of the table at +4 of
 * the parser's parameter descriptor, which scpi_parse_message_unit
 * passes scpi_parse_short and its kind, and scpi_match_suffix walks, 6
 * bytes a step, to the entry whose name is 0. */
typedef struct scpi_unit {
  char* name;       /* +0x00: upper case and reversed, "ZHM" for MHZ;
                       scpi_suffix_compare_tail matches it against the suffix's
                       end */
  int16_t exponent; /* +0x04: the power of ten the unit scales by; -128 for one
                       that takes a multiplier before it,
                       scpi_multiplier_exponent's */
} scpi_unit;
