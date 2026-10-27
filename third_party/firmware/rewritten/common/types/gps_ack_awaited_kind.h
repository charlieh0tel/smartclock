/* gps_ack_awaited_kind and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Which GPS sequence waits for the acknowledgment of the request it sent,
 * gps_ack_awaited, gpsx_send's last argument; pllp_task's reply handling
 * tells the console command's and the self-test's apart. */
typedef enum __attribute__((packed)) gps_ack_awaited_kind {
  GPS_ACK_NONE = 0,
  GPS_ACK_INIT = 1,
  GPS_ACK_LEAP_ACK = 2,
  GPS_ACK_LEAP_CHECK = 3,
  GPS_ACK_COMMAND = 4,
  GPS_ACK_SELFTEST = 5,
} gps_ack_awaited_kind;
