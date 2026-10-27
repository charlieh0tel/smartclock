/* log_entry and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* An event log entry: its time and date, packed as a klok_entry's, and
 * the format and two arguments it is printed with.  log_read formats
 * one as "Log %03d:%04d%02d%02d.%02d:%02d:%02d" and its text. */
typedef struct log_entry {
  uint32_t time;
  uint32_t date;
  char* format;
  int32_t argument_1;
  int32_t argument_2;
} log_entry;

/* A page of the event log, three entries, as log_read loads it. */
typedef struct log_page {
  uint16_t write_count; /* +0x00: incremented by each log_page_write */
  uint16_t
      checksum; /* +0x02: log_page_checksum's byte sum and its complement */
  log_entry entries[3];
} log_page;
