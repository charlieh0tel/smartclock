/* The classes of satellite_track_class the images share, at the same
 * values in each: the head of each image's satellite_health_class, whose
 * smartclock.h includes this header inside that enum
 * (docs/firmware/rewrite.md, "Shared C").  The rest is each image's own:
 * the Z3815A 4010's 4, healthy and in Time RAIM's set.  Where a comment
 * cites code, it is the Z3805A 3543B's. */
/* clang-format off */
  SATELLITE_CLASS_UNKNOWN = 0, /* no record, or a health past 3 */
  SATELLITE_CLASS_HEALTHY = 1,
  SATELLITE_CLASS_UNHEALTHY = 2,
  SATELLITE_CLASS_REMOVED = 3, /* healthy or not */
    /* clang-format on */
