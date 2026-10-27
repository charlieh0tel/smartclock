/* data_format and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The data format :FORMat:DATA sets, ASCii or INTeger (097-59551-02,
 * "Reading Time Stamps"), which only the :ROBin queries' responses use:
 * robin_store gives ASCii as an array of the points' shorts
 * and INTeger as a block of their bytes, by its jump table at robin_store+0x2e.
 */
typedef enum __attribute__((packed)) data_format {
  FORMAT_ASCII = 0,
  FORMAT_INTEGER = 1,
} data_format;
