/* A status condition's table entry, as the images' smartclock.h declare them
 * (each includes this header where they stand): docs/firmware/rewrite.md,
 * "Shared C". Where a comment cites code, it is the Z3805A 3543B's, which the
 * 58503A's copy of the comment came from. */

/* An entry of the status conditions table: which register and bit a
 * condition sets, and how.  6 bytes. */
typedef struct status_condition {
  int16_t kind; /* a status_condition_kind */
  int16_t register_index;
  int16_t bit;
} status_condition;
