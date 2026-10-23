/* The SCPI engine's decimal rounding as C calls it: a wrapper under the
 * routine's own name and prototype that binds its arguments to the
 * routine's registers and makes the jsr in one asm statement, naming as
 * changed each register the routine changes (softfloat.h).
 * scpi_round_decimal (0x24d42 to 0x24d86) steps A2 into the record and
 * on past its digits, walks A1 over them and counts D0 down, and
 * changes nothing else. */

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
