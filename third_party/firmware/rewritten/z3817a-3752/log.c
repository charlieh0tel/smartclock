/* The event log's routines the function check finds the decompiler's
 * reading wrong for: log_event and log_erase, whose texts an entry keeps
 * by address, the image's own (docs/firmware/z3817a-3752.md, "The
 * function check"); and the ring's own, log_init, log_append, log_read,
 * log_flush and log_page_free_entry, the Z3815A's and the other images' written
 * as this image's.  The ring's ends are log_oldest and log_newest and
 * each entry's first byte log_lap.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* The event log: LOG_PAGES pages of three entries in the EEPROM
 * (0x4224a). */
static const uint8_t LOG_PAGES = 74;

/* log_page_write's result when the EEPROM write fails. */
static const uint32_t LOG_PAGE_WRITE_FAILED = 0xfffffffe;

/* log_newest for an empty ring, so that log_append's first entry, which
 * it moves log_newest on to first, is 0 (0x42256). */
static const int32_t LOG_NEWEST_EMPTY = -1;

/* The ring's entries, LOG_PAGES pages of three (0x4243e, 0x42448), and
 * the count past which log_init and log_append report the log almost full
 * (0x424b2, 0x42b40). */
static const int32_t LOG_ENTRIES =
    LOG_PAGES *
    (sizeof log_write_page.entries / sizeof log_write_page.entries[0]);

static const int16_t LOG_ENTRIES_REPORTED = 177;

/* Empties the event log, every page in the EEPROM, reporting a memory
 * error for source if a write fails, and logs "Log cleared" if none
 * did; returns the first failure's low byte, or 0 (0x421f6). */
char log_erase(request_source source) {
  uint8_t status = 0;

  log_page_clear(&log_write_page);
  log_page_clear(&log_page_buffer);
  for (uint8_t page = 0; page < LOG_PAGES; page++) {
    status = (uint8_t)log_page_write(page, &log_write_page);
    if (status != 0) {
      if (status == (uint8_t)LOG_PAGE_WRITE_FAILED) {
        status_report_from(STATUS_MEMORY_ERROR, source);
      }
      break;
    }
  }
  log_oldest = 0;
  log_newest = LOG_NEWEST_EMPTY;
  log_lap = 0;
  log_write_page_number = 0;
  log_page_number = 0;
  log_cursor = log_write_page.entries;
  log_entries = 0;
  log_full = 0;
  status_happening(STATUS_LOG_ALMOST_FULL_CLEARED);
  if (status == 0) {
    log_append(log_text_log_cleared, 0, 0);
  }
  return (char)status;
}

/* The format of a holdover's message, its reason the argument
 * (0x42722). */
static char* const LOG_TEXT_HOLDOVER_STARTED_REASON = (char*)0x42c20;

/* log_event's texts, by code: the image's, at their addresses, which an
 * entry keeps; for a holdover with a reason, from
 * LOG_HOLDOVER_MANUAL to LOG_HOLDOVER_SELECTING_PPS, the reason, which
 * LOG_TEXT_HOLDOVER_STARTED_REASON formats (the jump table at 0x42744,
 * 28 cases).  The export declares objects for some (log_text_*), not
 * all. */
static char* const LOG_EVENT_TEXTS[] = {
    [LOG_CLEARED] = (char*)0x42bc8,
    [LOG_POWER_ON] = (char*)0x42c35,
    [LOG_REBOOT] = (char*)0x42c3e,
    [LOG_SURVEY_STARTED] = (char*)0x42c46,
    [LOG_POSITION_HOLD_STARTED] = (char*)0x42c5a,
    [LOG_EXT_LOCK_STARTED] = (char*)0x42c75,
    [LOG_GPS_LOCK_STARTED] = (char*)0x42c86,
    [LOG_HOLDOVER_MANUAL] = (char*)0x42c97,
    [LOG_HOLDOVER_EXT] = (char*)0x42cdd,
    [LOG_HOLDOVER_INVALID_EXT_PPS] = (char*)0x42ce1,
    [LOG_HOLDOVER_GPS] = (char*)0x42cc8,
    [LOG_HOLDOVER_INVALID_GPS_PPS] = (char*)0x42ccc,
    [LOG_HOLDOVER_NOT_TRACKING] = (char*)0x42c9e,
    [LOG_HOLDOVER_RAIM_ALARM] = (char*)0x42caf,
    [LOG_HOLDOVER_GPS_ALARM] = (char*)0x42cbe,
    [LOG_HOLDOVER_HW_ERROR] = (char*)0x42cf2,
    [LOG_HOLDOVER_TI_ERROR] = (char*)0x42cfb,
    [LOG_HOLDOVER_TI_LIMIT] = (char*)0x42d04,
    [LOG_HOLDOVER_TEMPORARY] = (char*)0x42d16,
    [LOG_HOLDOVER_SELECTING_PPS] = (char*)0x42d20,
    [LOG_HOLDOVER_STARTED] = (char*)0x42d2f,
    [LOG_EXT_REFERENCE_VALID] = (char*)0x42d40,
    [LOG_SPECIFICATIONS_EXCEEDED] = (char*)0x42d54,
    [LOG_HARDWARE_FAILURE] = (char*)0x42d73,
    [LOG_SELFTEST_FAILED] = (char*)0x42d84,
    [LOG_EEPROM_SAVE_FAILED] = (char*)0x42d95,
    [LOG_OUT_OF_EVENT_BUFFERS] = (char*)0x42da8,
    [LOG_SYSTEM_PRESET] = (char*)0x42dc5,
};

/* Logs one of log_event's messages by its code, a holdover's with its
 * reason as the format's argument (0x4271c).  The image pushes only
 * what it passes, so that log_write takes the rest of the entry's
 * arguments from log_event's own stack: each copy's, a known
 * difference (function_check.py's IMAGE_KNOWN). */
int32_t log_event(log_event_code code) {
  /* The result for a code it has no message for. */
  static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

  if (code >= sizeof LOG_EVENT_TEXTS / sizeof LOG_EVENT_TEXTS[0]) {
    return LOG_EVENT_UNKNOWN;
  }
  if (code >= LOG_HOLDOVER_MANUAL && code <= LOG_HOLDOVER_SELECTING_PPS) {
    return log_write(LOG_TEXT_HOLDOVER_STARTED_REASON, LOG_EVENT_TEXTS[code]);
  }
  return log_write(LOG_EVENT_TEXTS[code]);
}

/* An entry's lap, log_lap when log_append wrote it: the first byte of
 * its time, which a time_of_day leaves free (structures.md, "A date and
 * time"), log_append's store over it (0x42ae8). */
static uint8_t* log_entry_lap(log_entry* entry) {
  return (uint8_t*)&entry->time;
}

/* The lap of page 0's first entry, log_init's lap to start from;
 * the page read whether its checksum holds or not (0x429f0). */
uint8_t log_first_lap(void) {
  log_page page;

  log_page_read(0, &page);
  return *log_entry_lap(&page.entries[0]);
}

/* A page's first entry of another lap than log_lap's, the lap before, or
 * 0 for none (0x429c6). */
log_entry* log_page_previous_lap_entry(log_page* page) {
  for (log_entry* entry = page->entries;
       entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++) {
    if (*log_entry_lap(entry) != log_lap) {
      return entry;
    }
  }
  return 0;
}

/* The first entry of a page with no format, or whose format lies past
 * the ROM's last long, which no text in the image does, an erased
 * entry's among them; 0 for none (0x42994).  The other images' takes no format
 * as free alone. */
log_entry* log_page_free_entry(log_page* page) {
  static char* const LOG_FORMAT_LIMIT = (char*)0x7fffc;

  for (log_entry* entry = page->entries;
       entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++) {
    if (entry->format == 0 || entry->format >= LOG_FORMAT_LIMIT) {
      return entry;
    }
  }
  return 0;
}

/* Writes the page being filled, and copies it to log_page_buffer when that
 * holds the same page (0x42a0c).  The other images' also clears
 * log_write_page_open, which this image does not keep. */
uint8_t log_flush(void) {
  if (log_page_number == log_write_page_number) {
    memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
  }
  return log_page_write(log_write_page_number, &log_write_page);
}

/* Creates log_mutex and finds the ring's ends (0x4234a).  The page to
 * write next is the first with a free entry, or whose checksum fails,
 * its first entry then; or the first with an entry of the lap before,
 * which is the oldest, the log full.  With neither in any page every
 * entry is of one lap: the log is full, its newest entry the last, and
 * the next written the first, of the other lap, whose page it reads back.
 * The Z3815A's but that it clears log_full where that clears its
 * log_write_page_open, which this image does not keep, and reads page 0
 * back; like it, it returns a page's read error with log_mutex locked. */
uint32_t log_init(void) {
  /* log_page_read's result for a page whose checksum fails. */
  static const uint32_t LOG_PAGE_BAD = 0xff;
  uint32_t error = mutex_create("logr", &log_mutex);

  if (error != 0 || (error = mutex_lock(&log_mutex)) != 0) {
    return error;
  }
  log_entries = -1;
  log_page_number = log_entries;
  log_full = 0;
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
    log_page_read(log_write_page_number, &log_write_page);
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
 * log_cursor and writes its page (0x42a52): full, it goes on while
 * :DIAGnostic:LOG:WRAP is set, over the oldest entry; past a page's last
 * entry it reads the next page back from the EEPROM, past the last page
 * the first, the log full and the lap flipped; and the count stops at the
 * ring's size.  The Z3815A's reads the page before it adds the entry and
 * keeps log_write_page_open. */
uint32_t log_append(char* format, int32_t argument_1, int32_t argument_2) {
  if (log_full && !nv_settings.log_wrap) {
    return RECORDS_LOG_FULL;
  }
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
    log_page_read(log_write_page_number, &log_write_page);
  } else {
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
 * image's, byte for byte (docs/firmware/rewrite.md, "Booting the C").
 * log_read knows by their addresses the two formats it gives a date and
 * a time, not a byte and a long.  The export declares no object for
 * them. */
static char* const LOG_TEXT_EXT_REFERENCE_VALID_AT = (char*)0x42b62;

static char* const LOG_TEXT_GPS_REFERENCE_VALID_AT = (char*)0x42b95;

/* Logs when the GPS reference became valid, a date and a time, as the
 * Z3805A's logs its first satellite tracked (0x4294e). */
void log_first_satellite(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_GPS_REFERENCE_VALID_AT, date, time);
}

/* Logs when the external reference became valid, a date and a time;
 * nothing calls it (0x42966). */
void log_ext_reference_valid(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_EXT_REFERENCE_VALID_AT, date, time);
}

/* Event log entry number (from 1, from the ring's oldest, clamped to the
 * log): its text and a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either
 * pointer 0 for none; 0, or mutex_lock's error (0x424d8).  The entry is
 * counted from log_oldest, its page taken past the last back to the
 * first (0x4252c to 0x42536), and its first argument passed whole
 * (0x42628).  The Z3815A's but that it reads the page only when
 * log_page_buffer holds another (0x42580) and flushes no open page. */
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
    index = LOG_ENTRIES - 1;
  }
  if (page < 0 || entry < 0) {
    page = 0;
    entry = 0;
    index = 0;
  }

  log_entry* found = &log_page_buffer.entries[entry];
  uint32_t status = 0;

  if (page != log_page_number) {
    status = log_page_read(page, &log_page_buffer);
    log_page_number = page;
    if (status != 0) {
      log_page_clear(&log_page_buffer);
    }
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

/* 1 when a reset-cause record's checksum holds (0x43eb0). */
uint8_t reset_record_valid(uint8_t index) {
  return reset_record_checksum(&eeprom_reset_records[index]) ==
         eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x43ee6). */
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
 * when the number was clamped or the entry is unused (0x43fc4). */
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
 * full; 0 when written (0x440c0). */
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

/* The event log's pages in the EEPROM, at 0x400d80 (0x42070), which the
 * export declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d80;

/* Writes an event log page to the EEPROM if its entries have changed,
 * its write count one past the EEPROM's and its checksum made;
 * LOG_PAGE_WRITE_FAILED if the write fails, else 0 (0x42064). */
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

/* Logs why the last reset happened, with the time: the exception, the
 * fatal message, or, with neither and the watchdog's grace run out,
 * the watchdog counts; then clears both. */
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

/* Reads an event log page from the EEPROM, a long at a time as the
 * image copies it (0x420f6 to 0x420fa), where gcc copies the packed
 * page a byte at a time; 0xff if its checksum fails, else 0 (0x420da). */
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
 * reset-cause log's entries, into text; the 58503A 3704's (0x3b730). */
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
