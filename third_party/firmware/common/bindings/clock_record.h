/* The clock record's routines (clock_record_checksum to clock_set_leap_year) as
 * C calls them: each a wrapper under the routine's own name and prototype that
 * binds its arguments to the routine's registers and makes the jsr in one asm
 * statement, naming as changed each register the routine changes
 * (softfloat.h).  The record routines take the record in A6, as
 * clock_init holds it (clock_init+0x4); in C A6 is the frame pointer, so the
 * statement saves it around the call, as clock.c's clock_record_valid
 * does.  Each changes D6, in which it saves the status register around
 * its critical section (clock_record_checksum+0xa), and the registers its
 * comment names, read from its code and its calls'; each writes the record. */

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
 * complement at +1 and +0: changes D0, D6, A0 and A1. */
__attribute__((always_inline)) static inline void clock_record_checksum(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_checksum", record, "d0", "a0", "a1");
}

/* Adds a second to the record: changes D0, D1, D6, D7, A0
 * and A1. */
__attribute__((always_inline)) static inline void clock_record_advance(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_advance", record, "d0", "d1", "d7", "a0",
                    "a1");
}

/* Adds a second to the record's UTC pair: changes D0, D6, D7
 * and A0. */
__attribute__((always_inline)) static inline void clock_record_advance_utc(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_advance_utc", record, "d0", "d7", "a0");
}

/* Sets February's days at A0, 28 or 29, for the year in D7's high word:
 * changes D7. */
__attribute__((always_inline)) static inline void clock_set_leap_year(
    char* february, int32_t year) {
  register char* a0 __asm__("a0") = february;
  register int32_t d7 __asm__("d7") = year;

  __asm__ volatile("jsr clock_set_leap_year"
                   : "+r"(d7)
                   : "r"(a0)
                   : "cc", "memory");
}

/* Checks a date, D0, and a time, D1, and stores them in the record,
 * marking it set for a mark, D2, not 0; 0 in D7's low byte, or 2 for a
 * bad time, 1 for a bad date: changes D1, which it masks to
 * 24 bits, D6, D7 and A0, which it points at the record's days of
 * February for clock_set_leap_year.  No C calls it: set_timedate's and
 * set_timedate_unflagged's assembly does (set_timedate+0x1e,
 * set_timedate_unflagged+0x1e). */
__attribute__((always_inline)) static inline void clock_record_store(
    clock_record* record, int32_t date, int32_t time, int32_t mark) {
  register int32_t d0 __asm__("d0") = date;
  register int32_t d1 __asm__("d1") = time;
  register int32_t d2 __asm__("d2") = mark;

  __asm__ volatile(
      "move.l %%a6,-(%%sp)\n\t"
      "movea.l %[record],%%a6\n\t"
      "jsr clock_record_store\n\t"
      "movea.l (%%sp)+,%%a6"
      : "+r"(d1)
      : [record] "a"(record), "r"(d0), "r"(d2)
      : "d6", "d7", "a0", "cc", "memory");
}
