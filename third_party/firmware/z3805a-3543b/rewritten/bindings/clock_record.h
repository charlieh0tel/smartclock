/* The clock record's routines (0x134a8 to 0x138e4) as C calls them:
 * each a wrapper under the routine's own name and prototype that binds
 * its arguments to the routine's registers and makes the jsr in one asm
 * statement, naming as changed each register the routine changes
 * (softfloat.h).  The record routines take the record in A6, as
 * clock_init holds it (0x1390c); in C A6 is the frame pointer, so the
 * statement saves it around the call, as clock.c's clock_record_valid
 * does.  Each changes D6, in which it saves the status register around
 * its critical section (0x134b2), and the registers its comment names,
 * read from its code and its calls'; each writes the record. */

/* The record in A6, the registers the routine changes but A6 the
 * clobbers. */
#define CLOCK_RECORD_CALL(routine, record, ...) \
  __asm__ volatile(                             \
      "move.l %%a6,-(%%sp)\n\t"                 \
      "movea.l %[record],%%a6\n\t"              \
      "jsr " routine                            \
      "\n\t"                                    \
      "movea.l (%%sp)+,%%a6"                    \
      :                                         \
      : [record] "a"(record)                    \
      : "d6", "cc", "memory", __VA_ARGS__)

/* Sums +0x02 to +0x32 of the record and stores the sum and its
 * complement at +1 and +0 (0x134a8): changes D0, D6, A0 and A1. */
__attribute__((always_inline)) static inline void clock_record_checksum(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_checksum", record, "d0", "a0", "a1");
}

/* Adds a second to the record (0x13502): changes D0, D1, D6, D7, A0
 * and A1. */
__attribute__((always_inline)) static inline void clock_record_advance(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_advance", record, "d0", "d1", "d7", "a0",
                    "a1");
}

/* Adds a second to the record's UTC pair (0x135f0): changes D0, D6, D7
 * and A0. */
__attribute__((always_inline)) static inline void clock_record_advance_utc(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_advance_utc", record, "d0", "d7", "a0");
}

/* Converts the record's date and time to its +6 and +0xa (0x13882):
 * changes D0, D1, D6, D7, A0 and A1. */
__attribute__((always_inline)) static inline void clock_record_derive(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_derive", record, "d0", "d1", "d7", "a0",
                    "a1");
}

/* Sets February's days at A0, 28 or 29, for the year in D7's high word
 * (0x138e4): changes D7. */
__attribute__((always_inline)) static inline void clock_set_leap_year(
    char* february, int32_t year) {
  register char* a0 __asm__("a0") = february;
  register int32_t d7 __asm__("d7") = year;

  __asm__ volatile("jsr clock_set_leap_year"
                   : "+r"(d7)
                   : "r"(a0)
                   : "cc", "memory");
}
