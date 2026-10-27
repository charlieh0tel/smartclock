/* dac_measure and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The measurement dac_search_bisect and dac_search_step move a DAC by:
 * it sets the DAC to its argument and returns a reading. */
typedef uint8_t dac_measure(char_slot dac);
