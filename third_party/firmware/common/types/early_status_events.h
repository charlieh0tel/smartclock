/* early_status_events and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A buffer of the status events reported before the status model is
 * ready: early_status_reports, status_report's, and
 * early_status_happenings, status_happening's.  They pass
 * it to status_event_hook, which does nothing, so nothing fills them;
 * happening_flush replays one as a ring of five. */
typedef struct early_status_events {
  int8_t count; /* +0x00: the events reported; above 5 the ring has wrapped */
  uint8_t next; /* +0x01: where the next goes, the oldest once wrapped */
  int8_t events[5]; /* +0x02 */
  uint8_t field_07;
} early_status_events;

/* What happening_replay replays a buffer's codes
 * as, by its jump table at happening_replay+0x32; happening_flush passes the
 * first for early_status_reports and the second for early_status_happenings
 * (happening_flush+0x16, happening_flush+0x22). */
typedef enum __attribute__((packed)) status_replay_mode {
  STATUS_REPLAY_REPORTS = 0,    /* by status_report */
  STATUS_REPLAY_HAPPENINGS = 1, /* by status_happening */
} status_replay_mode;
