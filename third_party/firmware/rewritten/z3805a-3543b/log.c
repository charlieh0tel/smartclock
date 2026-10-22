/* The event log and the reset-cause log, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/log`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* Clears the last exception and the crash message (0x2442c). */
void exception_record_clear(void) {
  reset_record_count();
  last_exception.pending = 0;
  crash_message[0] = 0;
}

/* Logs why the last reset happened, with the time: the exception, the
 * fatal message, or, with neither and the watchdog's grace run out,
 * the watchdog counts; then clears both (0x24440). */
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

/* The sum and its complement of an event log page's entries (0x3137a). */
uint16_t log_page_checksum(log_page* page) {
  return records_sum_and_complement(
      records_byte_sum((const uint8_t*)page->entries, sizeof page->entries));
}

/* The event log's pages in the EEPROM, at 0x400d40, which the export
 * declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d40;

/* log_page_write's result when the EEPROM write fails. */
static const uint32_t LOG_PAGE_WRITE_FAILED = 0xfffffffe;

/* Empties a page (0x31bba). */
void log_page_clear(void* page) { memset(page, 0, sizeof(log_page)); }

/* Reads an event log page from the EEPROM; 0xff if its checksum fails,
 * else 0 (0x3141a). */
uint32_t log_page_read(uint8_t page, log_page* destination) {
  static const uint32_t CHECKSUM_FAILS = 0xff;

  *destination = LOG_EEPROM_PAGES[page];
  return log_page_checksum(destination) == destination->checksum
             ? 0
             : CHECKSUM_FAILS;
}

/* Writes an event log page to the EEPROM if its entries have changed,
 * its write count one past the EEPROM's and its checksum made;
 * LOG_PAGE_WRITE_FAILED if the write fails, else 0 (0x313a4). */
uint32_t log_page_write(uint8_t page, log_page* source) {
  log_page* stored = &LOG_EEPROM_PAGES[page];

  if (memcmp(stored->entries, source->entries, sizeof source->entries) == 0) {
    return 0;
  }
  source->write_count = stored->write_count + 1;
  source->checksum = log_page_checksum(source);
  if ((uint8_t)eeprom_write((char*)stored, (char*)source, sizeof *source) ==
      0) {
    return LOG_PAGE_WRITE_FAILED;
  }
  return 0;
}

/* Empties the event log, every page in the EEPROM, reporting a memory
 * error for source if a write fails, and logs "Log cleared" if none
 * did; returns the first failure's low byte, or 0 (0x31536). */
char log_erase(uint8_t source) {
  uint8_t status = 0;

  log_page_clear(&log_write_page);
  log_page_clear(&log_page_buffer);
  for (uint8_t page = 0; page < RECORDS_LOG_PAGES; page++) {
    status = log_page_write(page, &log_write_page);
    if (status != 0) {
      if (status == (uint8_t)LOG_PAGE_WRITE_FAILED) {
        status_report_from(STATUS_MEMORY_ERROR, source);
      }
      break;
    }
  }
  log_write_page_number = 0;
  log_page_number = 0;
  log_cursor = log_write_page.entries;
  log_entries = 0;
  log_write_page_open = 0;
  log_full = 0;
  status_happening(STATUS_LOG_ALMOST_FULL_CLEARED);
  if (status == 0) {
    log_append("Log cleared", 0, 0);
  }
  return status;
}

/* log_erase under log_mutex (0x315e8). */
int32_t log_erase_locked(uint8_t source) {
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t status = log_erase(source);
  mutex_unlock(&log_mutex);
  return status;
}

/* log_erase under log_mutex when the log holds count entries (0x3161a). */
int32_t log_erase_checked(int32_t count, uint8_t source) {
  /* The result for a count not the log's. */
  static const uint8_t LOG_COUNT_DIFFERS = 0xff;
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t status = count == log_entries ? log_erase(source) : LOG_COUNT_DIFFERS;
  mutex_unlock(&log_mutex);
  return status;
}

/* The event log's entries, and through full whether it is full
 * (0x31662). */
int32_t log_entry_total(uint8_t* full) {
  if (full != 0) {
    *full = log_full;
  }
  return log_entries;
}

/* Creates log_mutex and finds the event log's page to write next, the
 * first with a free entry or whose checksum fails (0x3167c). */
int32_t log_init(void) {
  /* log_page_read's result for a page whose checksum fails. */
  static const uint8_t LOG_PAGE_BAD = 0xff;
  int32_t error = mutex_create("logr", &log_mutex);

  if (error != 0 || (error = mutex_lock(&log_mutex)) != 0) {
    return error;
  }
  log_write_page_open = 0;
  log_entries = -1;
  log_page_number = -1;

  int32_t page = 0;
  for (;;) {
    uint32_t status = log_page_read(page, &log_write_page);
    if (status == LOG_PAGE_BAD) {
      log_cursor = log_write_page.entries;
      break;
    }
    if (status != 0) {
      return status;
    }
    log_cursor = log_page_free_entry(&log_write_page);
    if (log_cursor != 0 || ++page >= RECORDS_LOG_PAGES) {
      break;
    }
  }
  log_write_page_number = (int16_t)page;
  log_full = log_cursor == 0;
  log_entries = (int16_t)(log_write_page_number * RECORDS_LOG_PAGE_ENTRIES);
  if (!log_full) {
    log_entries += (int16_t)(log_cursor - log_write_page.entries);
  }
  if (log_entries > RECORDS_LOG_ENTRIES_REPORTED) {
    status_happening(STATUS_LOG_ALMOST_FULL_SET);
  }
  return mutex_unlock(&log_mutex);
}

/* Event log entry number (from 1, clamped to the log): its text and a
 * "Log NNN:YYYYMMDD.HH:MM:SS" heading, either pointer 0 for none; 0, or
 * mutex_lock's error (0x31762). */
int32_t log_read(int32_t number, char* text, char* heading) {
  int32_t index = number - 1;
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t page = index / RECORDS_LOG_PAGE_ENTRIES;
  int32_t entry = index % RECORDS_LOG_PAGE_ENTRIES;
  if (page >= RECORDS_LOG_PAGES) {
    page = RECORDS_LOG_PAGES - 1;
    entry = RECORDS_LOG_PAGE_ENTRIES - 1;
    index = RECORDS_LOG_PAGES * RECORDS_LOG_PAGE_ENTRIES - 1;
  }
  if (page < 0 || entry < 0) {
    page = 0;
    entry = 0;
    index = 0;
  }
  if (page == log_write_page_number && log_write_page_open) {
    log_flush();
  }

  uint32_t status = 0;
  if (page != log_page_number) {
    status = log_page_read(page, &log_page_buffer);
    log_page_number = page;
    if (status != 0) {
      log_page_clear(&log_page_buffer);
    }
  }

  log_entry* found = &log_page_buffer.entries[entry];
  if (status != 0 || found->format == 0) {
    records_log_unused(text, heading, index + 1);
  } else {
    if (text != 0) {
      if (found->format == records_first_satellite_format) {
        uint32_t date = found->argument_1;
        uint32_t time = found->argument_2;
        sprintf(text, (char*)records_first_satellite_format,
                date >> (2 * RECORDS_FIELD_BITS),
                date >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
                date & RECORDS_FIELD_MASK,
                time >> (2 * RECORDS_FIELD_BITS) & RECORDS_FIELD_MASK,
                time >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
                time & RECORDS_FIELD_MASK);
      } else {
        /* The image passes the first argument's low byte. */
        sprintf(text, found->format, (int8_t)found->argument_1,
                found->argument_2);
      }
    }
    if (heading != 0) {
      uint32_t time = found->time;
      records_log_heading(heading, index + 1, found->date,
                          time >> (2 * RECORDS_FIELD_BITS) & RECORDS_FIELD_MASK,
                          time >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
                          time & RECORDS_FIELD_MASK);
    }
  }
  mutex_unlock(&log_mutex);
  return 0;
}

/* Logs when the first satellite was tracked, a date and a time
 * (0x31ba2). */
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

/* Logs one of log_event's messages by its code (0x319a2). */
int32_t log_event(log_event_code code) {
  /* The result for a code it has no message for. */
  static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

  if (code >= sizeof records_log_events / sizeof records_log_events[0]) {
    return LOG_EVENT_UNKNOWN;
  }
  return log_write(records_log_events[code]);
}

/* The first entry of a page with no format, or 0 (0x31bd0). */
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
 * holds the same page (0x31bf6). */
uint8_t log_flush(void) {
  log_write_page_open = 0;
  if (log_page_number == log_write_page_number) {
    memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
  }
  return log_page_write(log_write_page_number, &log_write_page);
}

/* Adds an entry, its format, arguments, date and time, to the event log
 * and writes its page, moving to the next page after its last entry
 * (0x31c42). */
uint32_t log_append(char* format, int32_t argument_1, int32_t argument_2) {
  /* The result when the log is full. */
  static const uint8_t LOG_FULL = 0xfc;

  if (log_full) {
    return LOG_FULL;
  }
  log_cursor->format = format;
  log_cursor->argument_1 = argument_1;
  log_cursor->argument_2 = argument_2;
  /* log_entry's time is a time_of_day, which smartclock.h does not say. */
  get_date_time(&log_cursor->date, (time_of_day*)&log_cursor->time);

  uint32_t status = log_flush();
  if (log_cursor == &log_write_page.entries[RECORDS_LOG_PAGE_ENTRIES - 1]) {
    log_write_page_number++;
    log_full = log_write_page_number >= RECORDS_LOG_PAGES;
    log_cursor = log_write_page.entries;
    log_page_clear(&log_write_page);
  } else {
    log_write_page_open = 1;
    log_cursor++;
  }
  log_entries++;
  if (log_entries > RECORDS_LOG_ENTRIES_REPORTED) {
    status_happening(STATUS_LOG_ALMOST_FULL_SET);
  }
  return status;
}

/* A log line's text and heading buffers: an array size, so #define. */
#define LOG_TEXT_SIZE 80

/* "Log status: ...", the event log's (reset_causes false) or the
 * reset-cause log's entries, into text (0x2b4c2). */
void log_status(char* text, char reset_causes) {
  uint8_t event_log_full = 0;
  uint8_t reset_log_full = 0;
  int16_t count = reset_causes ? log_count(&reset_log_full)
                               : log_entry_total(&event_log_full);

  if (event_log_full || reset_log_full) {
    sprintf(text, "Log status: Full");
    return;
  }
  switch (count) {
    case -1:
      /* The image falls through, so that "Empty" replaces it. */
      sprintf(text, "Log status: Error");
      /* fall through */
    case 0:
      sprintf(text, "Log status: Empty");
      break;
    case 1:
      sprintf(text, "Log status: One entry");
      break;
    default:
      sprintf(text, "Log status: %-3d entries", count);
      break;
  }
}

/* Prints the event log or the reset-cause log: its status, then each
 * entry under its heading (0x2b578). */
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
 * time on (0x32ffa). */
uint16_t reset_record_checksum(reset_record* record) {
  return records_sum_and_complement(records_byte_sum(
      (const uint8_t*)&record->time,
      sizeof *record - __builtin_offsetof(reset_record, time)));
}

/* 1 when a reset-cause record's checksum holds (0x33028). */
uint8_t reset_record_valid(uint8_t index) {
  return reset_record_checksum(&eeprom_reset_records[index]) ==
         eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x3305e). */
int32_t log_clear(void) {
  int32_t failed = 0;
  char zero_checksum[sizeof eeprom_reset_records[0].checksum] = {0};

  for (int32_t i = 0; i < RECORDS_RESET_RECORDS; i++) {
    if (reset_record_valid(i) &&
        !eeprom_write((char*)&eeprom_reset_records[i].checksum, zero_checksum,
                      sizeof zero_checksum)) {
      failed = 1;
    }
  }
  log_entry_count = 0;
  if (!failed) {
    uint32_t date;
    time_of_day time;
    get_date_time(&date, &time);
    reset_record_write(date, time, "Log cleared");
  }
  return failed;
}

/* The reset-cause log's entries, and through full whether it is full
 * (0x330ee). */
int32_t log_count(uint8_t* full) {
  if (full != 0) {
    *full = log_entry_count >= RECORDS_RESET_RECORDS;
  }
  return log_entry_count;
}

/* Counts the valid reset-cause records from the first into
 * log_entry_count (0x33114). */
void reset_record_count(void) {
  log_entry_count = 0;
  for (uint8_t index = 0;
       index < RECORDS_RESET_RECORDS && reset_record_valid(index); index++) {
    log_entry_count++;
  }
}

/* Reset-cause log entry number (from 1, clamped to 1 to 32): its text and
 * a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either pointer 0 for none; 1
 * when the number was clamped or the entry is unused (0x3313c). */
uint32_t log_entry_text(int32_t number, char* text, char* heading) {
  /* The text each reset-cause record keeps. */
  static const int32_t RESET_TEXT_SIZE =
      (int32_t)sizeof eeprom_reset_records[0].text;
  uint32_t clamped = 0;
  int32_t index = number - 1;

  if (index >= RECORDS_RESET_RECORDS) {
    index = RECORDS_RESET_RECORDS - 1;
    clamped = 1;
  } else if (index < 0) {
    index = 0;
    clamped = 1;
  }

  if (!reset_record_valid(index)) {
    records_log_unused(text, heading, index + 1);
    return 1;
  }

  reset_record* record = &eeprom_reset_records[index];
  if (text != 0) {
    strncpy(text, record->text, RESET_TEXT_SIZE);
    text[RESET_TEXT_SIZE - 1] = '\0';
  }
  if (heading != 0) {
    records_log_heading(heading, index + 1, record->date, record->time.hour,
                        record->time.minute, record->time.second);
  }
  return clamped;
}

/* Writes the next reset-cause record, its date, time and text, unless the
 * log is full; 0 when written (0x33238). */
uint32_t reset_record_write(uint32_t date, time_of_day time, char* text) {
  uint8_t full;
  uint8_t index = (uint8_t)log_count(&full);

  if (full) {
    return 1;
  }
  reset_record record;
  record.date = date;
  record.time = time;
  strncpy(record.text, text, sizeof record.text);
  record.checksum = reset_record_checksum(&record);
  record.sequence = eeprom_reset_records[index].sequence + 1;
  if (!eeprom_write((char*)&eeprom_reset_records[index], (char*)&record,
                    sizeof record)) {
    return 1;
  }
  log_entry_count++;
  return 0;
}
