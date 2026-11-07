/* The cells of the status screen's satellites the images share, at the
 * same values in each: the head of each image's satellite_cell_status,
 * whose smartclock.h includes this header inside that enum
 * (docs/firmware/rewrite.md, "Shared C").  The rest is each image's
 * own: satellite_status' acquiring bands from 5 in the Z3805A 3543B, from
 * 6 in the Z3815A 4010, whose 5 is "Isolat". */
/* clang-format off */
  SATELLITE_CELL_NONE = 0,         /* blank */
  SATELLITE_CELL_POSITION = 1,     /* "%2d %3d", elevation and azimuth */
  SATELLITE_CELL_NO_POSITION = 2,  /* "-- ---" */
  SATELLITE_CELL_IGNORED = 3,      /* "Ignore" */
  SATELLITE_CELL_UNHEALTHY = 4,    /* "Not OK" */
    /* clang-format on */
