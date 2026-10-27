/* gps_statistics and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The GPS link's counters, which the "GPS Process Monitor" page
 * (gps_print_process_monitor) prints.  Cleared as one, memset(&gps_counts,
 * 0, 0x36).  All words. */
typedef struct gps_statistics {
  uint16_t received; /* Msgs Rcvd */
  uint16_t sent;     /* Msgs Sent */
  uint16_t jammed;   /* Msgs Jam */
  uint16_t sent_errors;
  uint16_t received_errors;
  uint16_t jam_errors;
  uint16_t successes;  /* Success */
  uint16_t field_0e;   /* no response code counts it and nothing prints it */
  uint16_t rejects;    /* Reject */
  uint16_t timeouts;   /* Timeout */
  uint16_t busy;       /* Busy */
  uint16_t aborts;     /* Abort */
  uint16_t no_command; /* No Cmd */
  uint16_t no_query;   /* No Qry */
  uint16_t unknown_command;  /* ? Cmd */
  uint16_t unknown_sender;   /* ? Sndr */
  uint16_t commands_sent;    /* Cmds Sent */
  uint16_t responses_read;   /* Resp Read */
  uint16_t e_retries;        /* E Retry */
  uint16_t t_retries;        /* T Retry */
  uint16_t raim_detected;    /* Time RAIM page: (%d detected) */
  uint16_t raim_alarms;      /* # Alarms */
  uint16_t unknown_response; /* ? Resp */
  uint16_t frame_errors;     /* Frame */
  uint16_t checksum_errors;  /* Cksum */
  uint16_t command_monitor_failures;
  uint16_t status_monitor_failures;
} gps_statistics;
