/* The SCPI engine's decimal record routines as C calls them: each a
 * wrapper under the routine's own name and prototype that binds its
 * arguments to the routine's registers and makes the jsr in one asm
 * statement, naming as changed each register the routine changes
 * (softfloat.h).  scpi_round_decimal (0x24d42 to 0x24d86) steps A2 into
 * the record and on past its digits, walks A1 over them and counts D0
 * down, and changes nothing else; scpi_decimal_to_text_unused (0x24d88
 * to 0x24e16) steps A0 over the record and A1 over the text, points A2
 * at the exponent and then the digit pairs, counts D0 down and works
 * the exponent in D1, and changes nothing else.  No C calls the second,
 * nor does the image. */

/* Rounds the decimal record to digits places: adds 5 at that digit and
 * carries left, a carry off the front writing 1 and raising the
 * exponent, and fills the digits after with 0 (0x24d42). */
__attribute__((always_inline)) static inline void scpi_round_decimal(
    char* record, int16_t digits) {
  register char* a2 __asm__("a2") = record;
  register int32_t d0 __asm__("d0") = digits;

  __asm__ volatile("jsr scpi_round_decimal"
                   : "+r"(a2), "+r"(d0)
                   :
                   : "a1", "cc", "memory");
}

/* Writes the decimal record as NR3 text, digits places: its sign, its
 * first digit, a point, the rest, E and the exponent (0x24d88). */
__attribute__((always_inline)) static inline void scpi_decimal_to_text_unused(
    char* record, char* text, int16_t digits) {
  register char* a0 __asm__("a0") = record;
  register char* a1 __asm__("a1") = text;
  register int32_t d0 __asm__("d0") = digits;

  __asm__ volatile("jsr scpi_decimal_to_text_unused"
                   : "+r"(a0), "+r"(a1), "+r"(d0)
                   :
                   : "d1", "a2", "cc", "memory");
}
