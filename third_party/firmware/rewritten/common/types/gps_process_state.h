/* gps_process_state and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The GPS task's state, gps_task_state: the GPS Process Monitor's
 * "State: " strings. */
typedef enum __attribute__((packed)) gps_process_state {
  GPS_PROCESS_TASK_IDLE = 0,    /* takes the next request from gps_requests */
  GPS_PROCESS_SEND_COMMAND = 1, /* runs the script's next step */
  GPS_PROCESS_WAIT_FOR_RESPONSE =
      2 /* waits for the step's response, retrying on a timeout */
} gps_process_state;
