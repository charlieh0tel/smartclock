/* gps_command_mailbox and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The GPS command the console's GPS words leave for pllp_task, at
 * gps_command: gps_command_send maps the code to an Oncore request, code
 * 0x20 sending request as it is, and sends it with the arguments. */
typedef struct gps_command_mailbox {
  gps_console_command code; /* +0x00: 0 when none is pending */
  uint8_t field_01;
  int32_t arguments[4]; /* +0x02 */
  int32_t request;      /* +0x12: code 0x20's request */
} gps_command_mailbox;
