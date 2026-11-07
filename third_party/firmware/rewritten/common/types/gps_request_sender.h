/* The senders of a GPS request the images share, at the same values in
 * each: the head of each image's gps_request_sender, whose smartclock.h
 * includes this header inside that enum (docs/firmware/rewrite.md,
 * "Shared C").  The monitors' senders, which gps_request_post sends
 * with, are each image's own: 3 and 4 in the Z3805A 3543B, 4 and 5 in
 * the Z3815A 4010. */
/* clang-format off */
  GPS_SENDER_NONE = 0,
  GPS_SENDER_SEQUENCE = 1,
  GPS_SENDER_SETTING = 2,
    /* clang-format on */
