/* gps_request_result and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* What became of a GPS request, gps_count_response's kind: what it
 * counts in gps_counts (its jump table at gps_count_response+0x26), stores in
 * command_monitor_result or status_monitor_result for the monitors'
 * requests, and sends back to plla_exchange in gps_request_entry.result
 * for the others.  Every sender sends a request with 1;
 * gps_request_post sets its monitor's result to pending. */
typedef enum __attribute__((packed)) gps_request_result {
  GPS_RESULT_SUCCESS = 0,         /* successes */
  GPS_RESULT_REJECT = 1,          /* rejects: the same wrong response twice */
  GPS_RESULT_ABORT = 2,           /* aborts: emptied by a link reset */
  GPS_RESULT_UNKNOWN_SENDER = 3,  /* unknown_sender: request_kind_valid's */
  GPS_RESULT_UNKNOWN_COMMAND = 4, /* unknown_command: gps_request_class's 3 */
  GPS_RESULT_BUSY = 5,            /* busy: not queued, or replaced */
  GPS_RESULT_NO_COMMAND = 6, /* no_command: " // command not supported //" */
  GPS_RESULT_NO_QUERY = 7,   /* no_query: " // query form not supported //" */
  GPS_RESULT_TIMEOUT = 8,    /* timeouts: no response after three sends */
  GPS_RESULT_PENDING = 9,    /* not yet answered; counted as none */
} gps_request_result;
