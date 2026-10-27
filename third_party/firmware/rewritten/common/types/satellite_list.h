/* satellite_list and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Which list satellite_list_limits gives the limits of. */
typedef enum __attribute__((packed)) satellite_list {
  SATELLITE_LIST_TRACKING = 0, /* :SAT:TRAC?, six channels */
  SATELLITE_LIST_TRACKING_PSEUDOLITES = 1,
  SATELLITE_LIST_VISIBLE = 2, /* :SAT:VIS:PRED?, twelve */
  SATELLITE_LIST_ALL = 3      /* the ignore and include lists */
} satellite_list;
