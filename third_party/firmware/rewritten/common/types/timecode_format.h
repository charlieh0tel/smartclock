/* timecode_format and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The time code's format, :PTIMe:TCODe:FORMat, which tcode_request
 * takes: T1, the GPS seconds; T2 with a test prefix; T2. */
typedef enum __attribute__((packed)) timecode_format {
  TIMECODE_T1 = 0,
  TIMECODE_T2_TEST = 1,
  TIMECODE_T2 = 2,
} timecode_format;
