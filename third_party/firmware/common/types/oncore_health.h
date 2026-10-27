/* oncore_health and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* @@Bb: a satellite's health. */
typedef enum __attribute__((packed)) oncore_health {
  ONCORE_HEALTHY = 0,
  ONCORE_HEALTHY_REMOVED = 1,
  ONCORE_UNHEALTHY = 2,
  ONCORE_UNHEALTHY_REMOVED = 3
} oncore_health;
