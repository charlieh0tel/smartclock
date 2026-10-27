/* gps_command_log_entry and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A GPS Receiver Command Log entry: what command_log_add takes of a
 * request gpsm_task has sent, and gps_print_command prints as
 * "%-5s %02d  %-2s %-2s [%s]   %11s  %8s %s". */
typedef struct gps_command_log_entry {
  gps_request_code_word code;    /* +0x00: the request's +0xa (move.w
                                    command_log_add+0x18), printed under "ID"
                                    (movea.w gps_print_command+0xf6) */
  gps_request_sender sender;     /* +0x02: the request's; printed PLL, SCPI1,
                                    GPSc and GPSs, else "?" */
  uint8_t field_03;              /* never written */
  gps_request_code_word message; /* +0x04: the request's +0x1e (move.w
                                    command_log_add+0x1e), its index in the
                                    message table at oncore_messages, whose ID
                                    prints (ext.l gps_print_command+0xe4) */
  char argument_text[3];         /* +0x06: characters 4 and 5 of
                                    gps_message_to_text's text, the first byte
                                    after the ID in hex; NUL */
  gps_argument_mode query;       /* +0x09: the command's; printed q, else c */
  uint8_t from_clock; /* +0x0a: 1 when the clock is set, the date and time its;
                         0 for a clock not set since power-up (get_date_time's
                         1), the date and time @@Ba's.  Printed blank, else e */
  uint8_t field_0b;   /* never written */
  uint32_t date; /* +0x0c: the clock's while from_clock, else @@Ba's, local */
  time_of_day
      time; /* +0x10: likewise; gps_print_command passes it to format_time */
} gps_command_log_entry;

/* The GPS Receiver Command Log, a ring at command_log, and its copy at
 * command_log_snapshot, which status_snapshot_update takes with the snapshot at
 * status_snapshot_flagged: 21 entries, the oldest at read, the next free at
 * write; empty when the two are equal, and full at 20, when command_log_add
 * drops the oldest.  gps_print_command takes the oldest. */
typedef struct gps_command_log {
  gps_command_log_entry entries[21];
  gps_command_log_entry* read;  /* +0x1a4 */
  gps_command_log_entry* write; /* +0x1a8 */
} gps_command_log;
