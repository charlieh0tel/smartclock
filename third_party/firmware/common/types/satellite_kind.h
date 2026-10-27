/* satellite_kind and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Which satellite IDs: satellite_id_valid's ranges. */
typedef enum __attribute__((packed)) satellite_kind {
  SATELLITE_GPS = 0,        /* 1 to 32 */
  SATELLITE_PSEUDOLITE = 1, /* 33 to 37 */
  SATELLITE_ANY = 2         /* 1 to 37 */
} satellite_kind;
