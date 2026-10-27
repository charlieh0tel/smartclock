/* log.c's functions that are this image's own: the rest are
 * common/log.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* A log entry's heading, its number and its date and time. */
static void records_log_heading(char* heading, int32_t number, uint32_t date,
                                uint32_t hour, uint32_t minute,
                                uint32_t second) {
  sprintf(heading, "Log %03d:%04d%02d%02d.%02d:%02d:%02d", number,
          date >> (2 * RECORDS_FIELD_BITS),
          date >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
          date & RECORDS_FIELD_MASK, hour, minute, second);
}

/* An unused log entry's text and heading, either pointer 0 for none. */
static void records_log_unused(char* text, char* heading, int32_t number) {
  if (text != 0) {
    const char* unused = "Unused";
    while ((*text++ = *unused++) != '\0') {
    }
  }
  if (heading != 0) {
    sprintf(heading, "Log %03d:00000000.00:00:00", number);
  }
}

/* The event log: RECORDS_LOG_PAGES pages of three entries, 64 bytes each,
 * in the EEPROM from 0x400d40 (log_page_read). */
static const uint8_t RECORDS_LOG_PAGES = 75;

/* Past this many entries log_init and log_append report the log almost
 * full. */
static const uint8_t RECORDS_LOG_ENTRIES_REPORTED = 180;

/* The one format log_read gives a date and a time, not a byte and a
 * long; log_read knows it by its address, so log_first_satellite and
 * log_read share the one copy.  The casts drop const for the exported
 * prototypes, which have none. */
static const char records_first_satellite_format[] =
    "First satellite tracked at %04d%02d%02d.%02d:%02d:%02d";

/* The sum and its complement of an event log page's entries (0x3126a). */
uint16_t log_page_checksum(log_page* page) {
  return records_sum_and_complement(
      records_byte_sum((const uint8_t*)page->entries, sizeof page->entries));
}

/* The event log's pages in the EEPROM, at 0x400d40, which the export
 * declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d40;

/* log_page_write's result when the EEPROM write fails. */
static const uint32_t LOG_PAGE_WRITE_FAILED = 0xfffffffe;

/* Logs when the first satellite was tracked, a date and a time
 * (0x31aac). */
void log_first_satellite(uint32_t date, uint32_t time) {
  log_write((char*)records_first_satellite_format, date, time);
}

/* log_event's messages, by code. */
static char* const records_log_events[] = {
    "Log cleared",
    "Power on",
    "Re-boot",
    "Survey mode started",
    "Position hold mode started",
    "EXT lock started",
    "GPS lock started",
    "Holdover started, manual",
    "Holdover started, EXT",
    "Holdover started, invalid EXT 1PPS",
    "Holdover started, GPS",
    "Holdover started, invalid GPS 1PPS",
    "Holdover started, not tracking GPS",
    "Holdover started, GPS RAIM alarm",
    "Holdover started, GPS alarm",
    "Holdover started, HW error",
    "Holdover started, TI error",
    "Holdover started, TI limit exceeded",
    "Holdover started, temporary",
    "Holdover started, selecting 1PPS",
    "Holdover started",
    "Output specifications exceeded",
    "Hardware failure",
    "Self-test failed",
    "EEPROM save failed",
    "Clk_do: out of event buffers",
    "System preset",
};
