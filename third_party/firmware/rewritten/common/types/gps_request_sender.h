/* gps_request_sender and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Who sends a GPS request (gps_request_entry.sender), so that its result
 * comes back to it: gpsx_send for the loop's GPS sequences (pllp_task
 * takes the result as gps_ack_code), pll_scpi_send_to_loop for a
 * setting (pll_scpi_complete), and gps_request_post for the command
 * and status monitors, whose results gps_count_response keeps in
 * command_monitor_result and status_monitor_result.  request_kind_valid
 * takes 1 to 4, from its table at day_names+0x40, which GPS_SENDER_NONE ends.
 */
typedef enum __attribute__((packed)) gps_request_sender {
  GPS_SENDER_NONE = 0,
  GPS_SENDER_SEQUENCE = 1,
  GPS_SENDER_SETTING = 2,
  GPS_SENDER_COMMAND_MONITOR = 3,
  GPS_SENDER_STATUS_MONITOR = 4,
} gps_request_sender;
