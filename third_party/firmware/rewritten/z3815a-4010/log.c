/* log.c's functions that are this image's own: the rest are
 * common/log.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C"), or the decompiler's.  The event log is a ring of entries,
 * its ends log_oldest and log_newest and each entry's first byte
 * log_lap (docs/firmware/structures.md, "What the prints name").
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The event log: LOG_PAGES pages of three entries in the EEPROM, as the
 * 58503A 3704's (0x45a6a). */
static const uint8_t LOG_PAGES = 74;

/* The ring's entries, LOG_PAGES pages of three (0x45c28), and the count
 * past which log_init and log_append report the log almost full
 * (0x45cd6, 0x46356). */
static const int32_t LOG_ENTRIES =
    LOG_PAGES *
    (sizeof log_write_page.entries / sizeof log_write_page.entries[0]);

static const int16_t LOG_ENTRIES_REPORTED = 177;

/* log_page_write's result when the EEPROM write fails. */
static const uint32_t LOG_PAGE_WRITE_FAILED = 0xfffffffe;

/* An entry's lap, log_lap when log_append wrote it: the first byte of
 * its time, which a time_of_day leaves free (structures.md, "A date and
 * time"), log_append's store over it (0x46306). */
static uint8_t* log_entry_lap(log_entry* entry) {
  return (uint8_t*)&entry->time;
}

/* Empties the event log, every page in the EEPROM, reporting a memory
 * error for source if a write fails, and logs "Log cleared" if none
 * did; returns the first failure's low byte, or 0 (0x45a16).  The ring's
 * newest entry is set one past its last, so that log_append's first
 * entry is 0. */
char log_erase(request_source source) {
  uint8_t status = 0;

  log_page_clear(&log_write_page);
  log_page_clear(&log_page_buffer);
  for (uint8_t page = 0; page < LOG_PAGES; page++) {
    status = log_page_write(page, &log_write_page);
    if (status != 0) {
      if (status == (uint8_t)LOG_PAGE_WRITE_FAILED) {
        status_report_from(STATUS_MEMORY_ERROR, (uint8_t)source);
      }
      break;
    }
  }
  log_oldest = 0;
  log_newest = LOG_ENTRIES;
  log_lap = 0;
  log_write_page_number = 0;
  log_page_number = 0;
  log_cursor = log_write_page.entries;
  log_entries = 0;
  log_write_page_open = 0;
  log_full = 0;
  status_happening(STATUS_LOG_ALMOST_FULL_CLEARED);
  if (status == 0) {
    log_append(log_text_log_cleared, 0, 0);
  }
  return status;
}

/* The lap of page 0's first entry, log_init's lap to start from;
 * the page read whether its checksum holds or not (0x461f8). */
uint8_t log_first_lap(void) {
  log_page page;

  log_page_read(0, &page);
  return *log_entry_lap(&page.entries[0]);
}

/* A page's first entry of another lap than log_lap's, the lap before, or
 * 0 for none (0x461ce). */
log_entry* log_page_previous_lap_entry(log_page* page) {
  for (log_entry* entry = page->entries;
       entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++) {
    if (*log_entry_lap(entry) != log_lap) {
      return entry;
    }
  }
  return 0;
}

/* Creates log_mutex and finds the ring's ends (0x45b72).  The page to
 * write next is the first with a free entry, or whose checksum fails,
 * its first entry then; or the first with an entry of the lap before,
 * which is the oldest, the log full.  With neither in any page every
 * entry is of one lap: the log is full, its newest entry the last, and
 * the next written the first, of the other lap.  The 58503A 3704's log_init
 * but for the ring; like it, it returns a page's read error with
 * log_mutex locked, and it only ever sets log_full. */
uint32_t log_init(void) {
  /* log_page_read's result for a page whose checksum fails. */
  static const uint32_t LOG_PAGE_BAD = 0xff;
  uint32_t error = mutex_create("logr", &log_mutex);

  if (error != 0 || (error = mutex_lock(&log_mutex)) != 0) {
    return error;
  }
  log_write_page_open = 0;
  log_entries = -1;
  log_page_number = log_entries;
  log_lap = log_first_lap();
  log_oldest = 0;

  int32_t page = 0;
  for (; page < LOG_PAGES; page++) {
    const uint32_t status = log_page_read(page, &log_write_page);

    if (status == LOG_PAGE_BAD) {
      log_cursor = log_write_page.entries;
      break;
    }
    if (status != 0) {
      return status;
    }
    log_cursor = log_page_free_entry(&log_write_page);
    if (log_cursor != 0) {
      break;
    }
    log_cursor = log_page_previous_lap_entry(&log_write_page);
    if (log_cursor != 0) {
      log_full = 1;
      log_entries = LOG_ENTRIES;
      log_oldest = page * RECORDS_LOG_PAGE_ENTRIES +
                   (log_cursor - log_write_page.entries);
      break;
    }
  }
  if (log_cursor == 0) {
    log_write_page_number = 0;
    log_cursor = log_write_page.entries;
    log_newest = LOG_ENTRIES - 1;
    log_entries = LOG_ENTRIES;
    log_full = 1;
    log_lap = !log_lap;
  } else {
    log_write_page_number = page;
    log_newest = page * RECORDS_LOG_PAGE_ENTRIES +
                 (log_cursor - log_write_page.entries) - 1;
  }
  if (!log_full) {
    log_entries =
        page * RECORDS_LOG_PAGE_ENTRIES + (log_cursor - log_write_page.entries);
  }
  if (log_entries > LOG_ENTRIES_REPORTED) {
    status_happening(STATUS_LOG_ALMOST_FULL_SET);
  }
  return mutex_unlock(&log_mutex);
}

/* Adds an entry, its format, arguments, date and time, and log_lap, at
 * log_cursor and writes its page (0x46260).  The 58503A 3704's
 * but for the ring: full, it goes on while :DIAGnostic:LOG:WRAP is set,
 * over the oldest entry; it reads the page from the EEPROM first, which
 * log_init may have left holding another; past the last page it
 * goes back to the first, the log full and the lap flipped; and the count
 * stops at the ring's size. */
uint32_t log_append(char* format, int32_t argument_1, int32_t argument_2) {
  if (log_full && !nv_settings.log_wrap) {
    return RECORDS_LOG_FULL;
  }
  log_page_read(log_write_page_number, &log_write_page);
  if (++log_newest > LOG_ENTRIES - 1) {
    log_newest = 0;
  }
  if (log_newest == log_oldest && log_cursor->format != 0) {
    log_oldest++;
  }
  if (log_oldest > LOG_ENTRIES - 1) {
    log_oldest = 0;
  }
  log_cursor->format = format;
  log_cursor->argument_1 = argument_1;
  log_cursor->argument_2 = argument_2;
  /* log_entry's time is a time_of_day, which smartclock.h does not say. */
  get_date_time(&log_cursor->date, (time_of_day*)&log_cursor->time);
  *log_entry_lap(log_cursor) = log_lap;

  const uint32_t status = log_flush();

  if (log_cursor == &log_write_page.entries[RECORDS_LOG_PAGE_ENTRIES - 1]) {
    if (++log_write_page_number > LOG_PAGES - 1) {
      log_full = 1;
      log_write_page_number = 0;
      log_lap = !log_lap;
    }
    log_cursor = log_write_page.entries;
  } else {
    log_write_page_open = 1;
    log_cursor++;
  }
  if (++log_entries > LOG_ENTRIES) {
    log_entries = LOG_ENTRIES;
  }
  if (log_entries > LOG_ENTRIES_REPORTED) {
    status_happening(STATUS_LOG_ALMOST_FULL_SET);
  }
  return status;
}

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

/* An entry keeps its format by address, in the EEPROM, where the log
 * outlives a restart and the code that wrote it: every format the C
 * logs is the image's own text, at its own address, so that an entry
 * the image wrote reads back as it does and one the C writes is the
 * image's, byte for byte.
 * log_read knows by their addresses the two formats it gives a date and
 * a time, not a byte and a long.  The export declares no object for
 * them. */
static char* const LOG_TEXT_EXT_REFERENCE_VALID_AT =
    (char*)0x46378; /* "EXT reference valid at %04d%02d%02d.%02d:%02d:%02d" */

static char* const LOG_TEXT_GPS_REFERENCE_VALID_AT =
    (char*)0x463ab; /* "GPS reference valid at %04d%02d%02d.%02d:%02d:%02d" */

/* Logs when the GPS reference became valid, a date and a time, as the
 * Z3805A's logs its first satellite tracked (0x46162). */
void log_first_satellite(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_GPS_REFERENCE_VALID_AT, date, time);
}

/* Logs when the external reference became valid, a date and a time;
 * nothing calls it (0x4617a). */
void log_ext_reference_valid(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_EXT_REFERENCE_VALID_AT, date, time);
}

/* log_event's messages, by code: the image's texts at their addresses,
 * which an entry keeps; the export declares objects for some
 * (log_text_*), not all. */
static char* const records_log_events[] = {
    [LOG_CLEARED] = (char*)0x463de,
    [LOG_POWER_ON] = (char*)0x46436,
    [LOG_REBOOT] = (char*)0x4643f,
    [LOG_SURVEY_STARTED] = (char*)0x46447,
    [LOG_POSITION_HOLD_STARTED] = (char*)0x4645b,
    [LOG_EXT_LOCK_STARTED] = (char*)0x46476,
    [LOG_GPS_LOCK_STARTED] = (char*)0x46487,
    [LOG_HOLDOVER_MANUAL] = (char*)0x46498,
    [LOG_HOLDOVER_EXT] = (char*)0x4654a,
    [LOG_HOLDOVER_INVALID_EXT_PPS] = (char*)0x46560,
    [LOG_HOLDOVER_GPS] = (char*)0x46511,
    [LOG_HOLDOVER_INVALID_GPS_PPS] = (char*)0x46527,
    [LOG_HOLDOVER_NOT_TRACKING] = (char*)0x464b1,
    [LOG_HOLDOVER_RAIM_ALARM] = (char*)0x464d4,
    [LOG_HOLDOVER_GPS_ALARM] = (char*)0x464f5,
    [LOG_HOLDOVER_HW_ERROR] = (char*)0x46583,
    [LOG_HOLDOVER_TI_ERROR] = (char*)0x4659e,
    [LOG_HOLDOVER_TI_LIMIT] = (char*)0x465b9,
    [LOG_HOLDOVER_TEMPORARY] = (char*)0x465dd,
    [LOG_HOLDOVER_SELECTING_PPS] = (char*)0x465f9,
    [LOG_HOLDOVER_STARTED] = (char*)0x4661a,
    [LOG_EXT_REFERENCE_VALID] = (char*)0x4662b,
    [LOG_SPECIFICATIONS_EXCEEDED] = (char*)0x4663f,
    [LOG_HARDWARE_FAILURE] = (char*)0x4665e,
    [LOG_SELFTEST_FAILED] = (char*)0x4666f,
    [LOG_EEPROM_SAVE_FAILED] = (char*)0x46680,
    [LOG_OUT_OF_EVENT_BUFFERS] = (char*)0x46693,
    [LOG_SYSTEM_PRESET] = (char*)0x466b0,
};

/* Logs one of log_event's messages by its code (0x45f50). */
int32_t log_event(log_event_code code) {
  /* The result for a code it has no message for. */
  static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

  if (code >= sizeof records_log_events / sizeof records_log_events[0]) {
    return LOG_EVENT_UNKNOWN;
  }
  return log_write(records_log_events[code]);
}

/* 1 when a reset-cause record's checksum holds (0x4779e). */
uint8_t reset_record_valid(uint8_t index) {
  return reset_record_checksum(&eeprom_reset_records[index]) ==
         eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x477d4). */
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

/* Reset-cause log entry number (from 1, clamped to 1 to 32): its text and
 * a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either pointer 0 for none; 1
 * when the number was clamped or the entry is unused (0x478b2). */
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

/* Writes the next reset-cause record, its date, time and text, ended
 * within the record where the Z3805A's need not be, unless the log is
 * full; 0 when written (0x479ae). */
uint32_t reset_record_write(uint32_t date, time_of_day time, char* text) {
  uint8_t full;
  uint8_t index = (uint8_t)log_count(&full);

  if (full) {
    return 1;
  }
  /* Even, since gcc writes its words and longs as such, misaligned at an
   * odd address (MC68331UM, 4.4.4): smartclock.h packs it, and beside
   * full it lies at one. */
  reset_record __attribute__((aligned(2))) record;
  record.date = date;
  record.time = time;
  strncpy(record.text, text, sizeof record.text);
  record.text[sizeof record.text - 1] = '\0';
  record.checksum = reset_record_checksum(&record);
  record.sequence = eeprom_reset_records[index].sequence + 1;
  if (!eeprom_write((char*)&eeprom_reset_records[index], (char*)&record,
                    sizeof record)) {
    return 1;
  }
  log_entry_count++;
  return 0;
}

/* The event log's pages in the EEPROM, at 0x400d80 (0x45890), which the
 * export declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d80;

/* Writes an event log page to the EEPROM if its entries have changed,
 * its write count one past the EEPROM's and its checksum made;
 * LOG_PAGE_WRITE_FAILED if the write fails, else 0 (0x45884). */
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

/* Reads an event log page from the EEPROM, a long at a time as the
 * image copies it (0x45916 to 0x4591a), where gcc copies the packed
 * page a byte at a time; 0xff if its checksum fails, else 0 (0x458fa). */
uint32_t log_page_read(uint8_t page, log_page* destination) {
  static const uint32_t CHECKSUM_FAILS = 0xff;
  static const uint32_t LONGS = sizeof(log_page) / sizeof(uint32_t);
  const uint32_t* from = (const uint32_t*)&LOG_EEPROM_PAGES[page];
  uint32_t* to = (uint32_t*)destination;

  for (uint32_t i = 0; i < LONGS; i++) {
    to[i] = from[i];
  }
  return log_page_checksum(destination) == destination->checksum
             ? 0
             : CHECKSUM_FAILS;
}

/* "Log status: ...", the event log's (reset_causes false) or the
 * reset-cause log's entries, into text; the 58503A 3704's (0x3e680). */
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

/* Event log entry number (from 1, from the ring's oldest, clamped to the
 * log): its text and a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either
 * pointer 0 for none; 0, or mutex_lock's error (0x45cfc).  The 58503A
 * 3704's but for its own: the entry is counted from log_oldest, its page
 * taken past the last back to the first (0x45d32 to 0x45d5a), the page
 * read whatever log_page_number holds, and an entry's first argument
 * passed whole (0x45e5c), where the 58503A's passes its low byte. */
int32_t log_read(int32_t number, char* text, char* heading) {
  int32_t index = number - 1;
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  /* div's quotient in the high long, its remainder in the low. */
  const uint64_t place = div(log_oldest + index, RECORDS_LOG_PAGE_ENTRIES);
  int32_t page = (int32_t)(place >> CPU_LONG_BITS);
  int32_t entry = (int32_t)place;
  if (page >= LOG_PAGES) {
    page -= LOG_PAGES;
  }
  if (page >= LOG_PAGES) {
    page = LOG_PAGES - 1;
    entry = RECORDS_LOG_PAGE_ENTRIES - 1;
    index = LOG_PAGES * RECORDS_LOG_PAGE_ENTRIES - 1;
  }
  if (page < 0 || entry < 0) {
    page = 0;
    entry = 0;
    index = 0;
  }
  if (page == log_write_page_number && log_write_page_open) {
    log_flush();
  }

  log_entry* found = &log_page_buffer.entries[entry];
  const uint32_t status = log_page_read(page, &log_page_buffer);
  log_page_number = page;
  if (status != 0) {
    log_page_clear(&log_page_buffer);
  }
  if (status != 0 || found->format == 0) {
    records_log_unused(text, heading, index + 1);
  } else {
    if (text != 0) {
      if (found->format == LOG_TEXT_EXT_REFERENCE_VALID_AT ||
          found->format == LOG_TEXT_GPS_REFERENCE_VALID_AT) {
        uint32_t date = found->argument_1;
        uint32_t time = found->argument_2;
        sprintf(text, found->format, date >> (2 * RECORDS_FIELD_BITS),
                date >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
                date & RECORDS_FIELD_MASK,
                time >> (2 * RECORDS_FIELD_BITS) & RECORDS_FIELD_MASK,
                time >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK,
                time & RECORDS_FIELD_MASK);
      } else {
        sprintf(text, found->format, found->argument_1, found->argument_2);
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

/* Logs why the last reset happened, with the time: the exception, the
 * fatal message, or, with neither and the watchdog's grace run out,
 * the watchdog counts; then clears both (0x3673e).  The 58503A 3704's
 * with this image's backp task's count last among the watchdog's
 * (0x36834). */
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
    sprintf(text,
            "Watchdog timeout: clk %1d gps %4d mon %4d pll %1d spl %1d bp %4d",
            watchdog_clock, watchdog_gps, watchdog_monitor, watchdog_pll,
            watchdog_spool, watchdog_bp);
    reset_record_write(date, time, text);
  }
  last_exception.pending = 0;
  crash_message[0] = 0;
}

/* The first entry of a page with no format, or 0. */
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
 * holds the same page. */
uint8_t log_flush(void) {
  log_write_page_open = 0;
  if (log_page_number == log_write_page_number) {
    memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
  }
  return log_page_write(log_write_page_number, &log_write_page);
}
