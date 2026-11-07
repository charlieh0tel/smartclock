/* The time code's formats the images share, at the same values in each:
 * the head of each image's timecode_format, whose smartclock.h includes
 * this header inside that enum (docs/firmware/rewrite.md, "Shared C").
 * The rest is each image's own: the Z3815A 4010's FL1 and FL2.  Where a
 * comment cites code, it is the Z3805A 3543B's. */
/* clang-format off */
  TIMECODE_T1 = 0,
  TIMECODE_T2_TEST = 1,
  TIMECODE_T2 = 2,
    /* clang-format on */
