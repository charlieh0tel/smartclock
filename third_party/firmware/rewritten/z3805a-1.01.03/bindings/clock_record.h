/* The 58503B 1.01.04's bindings/clock_record.h, copied for the Z3805A
 * 1.01.03, each of its functions identical or relocated in image_diff's
 * matches (docs/firmware/z3805a-1.01.03.md, "The export and the
 * checks"); its comments' addresses are the Z3805A 1.01.03's.  The
 * 58503B's note follows. */

/* clock_record_derive's binding, this image's own, as the routine
 * differs between the images; common/bindings/clock_record.h has the
 * clock record's other routines and CLOCK_RECORD_CALL. */

/* Converts the record's date and time to its +6 and +0xa (0x13b82):
 * changes D0, D1, D6, D7, A0 and A1. */
__attribute__((always_inline)) static inline void clock_record_derive(
    clock_record* record) {
  CLOCK_RECORD_CALL("clock_record_derive", record, "d0", "d1", "d7", "a0",
                    "a1");
}
