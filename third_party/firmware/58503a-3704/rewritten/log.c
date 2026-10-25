/* The event log and the reset-cause log, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/log`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* Clears the last exception and the crash message (0x23750). */
void exception_record_clear(void) {
  reset_record_count();
  last_exception.pending = 0;
  crash_message[0] = 0;
}

/* Logs why the last reset happened, with the time: the exception, the
 * fatal message, or, with neither and the watchdog's grace run out,
 * the watchdog counts; then clears both (0x23764). */
void log_reset_cause(void) {
  char text[MONITORS_CRASH_MESSAGE_SIZE];
  uint32_t date;
  time_of_day time;

  reset_record_count();
  get_date_time(&date, &time);
  text[0] = 0;
  if (last_exception.pending) {
    if ((last_exception.vector & CPU_VECTOR_FORMAT_MASK) == 0) {
      sprintf(text, "Exception: %s, vector %04X, PC = %08X, SR = %04X",
              last_exception.name, last_exception.vector, last_exception.pc,
              last_exception.sr);
    } else {
      sprintf(text,
              "Exception: %s, vector %04X, PC = %08X, SR = %04X, addr = %08X",
              last_exception.name, last_exception.vector, last_exception.pc,
              last_exception.sr, last_exception.address);
    }
    reset_record_write(date, time, text);
  }
  if (crash_message[0] != 0) {
    strncpy(text, crash_message, MONITORS_CRASH_MESSAGE_SIZE);
    text[MONITORS_CRASH_MESSAGE_SIZE - 1] = 0;
    reset_record_write(date, time, text);
  }
  if (watchdog_countdown == 0 && crash_message[0] == 0 &&
      !last_exception.pending) {
    sprintf(text, "Watchdog timeout: clk %1d gps %4d mon %4d pll %1d spl %1d",
            watchdog_clock, watchdog_gps, watchdog_monitor, watchdog_pll,
            watchdog_spool);
    reset_record_write(date, time, text);
  }
  last_exception.pending = 0;
  crash_message[0] = 0;
}

/* The reset-cause log's records. */
static const int32_t RECORDS_RESET_RECORDS =
    (int32_t)(sizeof eeprom_reset_records / sizeof eeprom_reset_records[0]);

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

static const int32_t RECORDS_LOG_PAGE_ENTRIES =
    (int32_t)(sizeof log_write_page.entries / sizeof log_write_page.entries[0]);

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

/* Empties a page (0x31adc). */
void log_page_clear(void* page) { memset(page, 0, sizeof(log_page)); }

/* log_erase under log_mutex (0x314d8). */
int32_t log_erase_locked(request_source source) {
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t status = log_erase(source);
  mutex_unlock(&log_mutex);
  return status;
}

/* log_erase under log_mutex when the log holds count entries (0x3150a). */
int32_t log_erase_checked(int32_t count, request_source source) {
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t status =
      count == log_entries ? log_erase(source) : RECORDS_LOG_COUNT_DIFFERS;
  mutex_unlock(&log_mutex);
  return status;
}

/* The event log's entries, and through full whether it is full
 * (0x31552). */
int32_t log_entry_total(uint8_t* full) {
  if (full != 0) {
    *full = log_full;
  }
  return log_entries;
}

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

/* Appends an entry to the event log under its lock; returns the lock's
 * error or log_append's status (0x31852).  Variadic in name: an entry
 * keeps two longs of arguments, which it takes whatever its format
 * converts, a double's two halves among them. */
int32_t log_write(char* format, ...) {
  va_list arguments;

  va_start(arguments, format);
  const int32_t argument_1 = va_arg(arguments, int32_t);
  const int32_t argument_2 = va_arg(arguments, int32_t);

  va_end(arguments);

  uint32_t status = mutex_lock(&log_mutex);

  if (status == 0) {
    status = log_append(format, argument_1, argument_2);
    mutex_unlock(&log_mutex);
  }
  return status;
}

/* The first entry of a page with no format, or 0 (0x31af2). */
log_entry* log_page_free_entry(log_page* page) {
  for (log_entry* entry = page->entries;
       entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++) {
    if (entry->format == 0) {
      return entry;
    }
  }
  return 0;
}

/* Writes the page being filled, and copies it to log_page_buffer when that
 * holds the same page (0x31b18). */
uint8_t log_flush(void) {
  log_write_page_open = 0;
  if (log_page_number == log_write_page_number) {
    memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
  }
  return log_page_write(log_write_page_number, &log_write_page);
}

/* A log line's text and heading buffers: an array size, so #define. */
#define LOG_TEXT_SIZE 80

/* Prints the event log or the reset-cause log: its status, then each
 * entry under its heading (0x2b050). */
void log_print(char reset_causes, write_function* write,
               printf_function* print) {
  char text[LOG_TEXT_SIZE];
  char heading[LOG_TEXT_SIZE];
  int16_t count = reset_causes ? log_count(0) : log_entry_total(0);

  log_status(text, reset_causes);
  print("%s\r\n", text);
  write("\r\n");
  for (int16_t number = 1; number <= count; number++) {
    if (reset_causes) {
      log_entry_text(number, text, heading);
    } else {
      log_read(number, text, heading);
    }
    print("%s:  %s\r\n", heading, text);
  }
  write("\r\n");
}

/* The sum and its complement of a reset-cause record's bytes from its
 * time on (0x33006). */
uint16_t reset_record_checksum(reset_record* record) {
  return records_sum_and_complement(records_byte_sum(
      (const uint8_t*)&record->time,
      sizeof *record - __builtin_offsetof(reset_record, time)));
}

/* The reset-cause log's entries, and through full whether it is full
 * (0x330fa). */
int32_t log_count(uint8_t* full) {
  if (full != 0) {
    *full = log_entry_count >= RECORDS_RESET_RECORDS;
  }
  return log_entry_count;
}

/* Counts the valid reset-cause records from the first into
 * log_entry_count (0x33120). */
void reset_record_count(void) {
  log_entry_count = 0;
  for (uint8_t index = 0;
       index < RECORDS_RESET_RECORDS && reset_record_valid(index); index++) {
    log_entry_count++;
  }
}
