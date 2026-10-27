/* satellite_health_class and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* satellite_track_class's class of a @@Bb record, by its health
 * (oncore_health; satellite_track_class, its table at
 * satellite_track_class+0x26): satellite_status shows a removed one as "Ignore"
 * and an unhealthy one as "Not OK". */
typedef enum __attribute__((packed)) satellite_health_class {
  SATELLITE_CLASS_UNKNOWN = 0, /* no record, or a health past 3 */
  SATELLITE_CLASS_HEALTHY = 1,
  SATELLITE_CLASS_UNHEALTHY = 2,
  SATELLITE_CLASS_REMOVED = 3 /* healthy or not */
} satellite_health_class;
