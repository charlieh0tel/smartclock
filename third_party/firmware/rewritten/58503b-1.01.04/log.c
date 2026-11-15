/* The 58503B 1.01.04's log_erase, the Z3817A 3752's code (relocated),
 * in the Z3816A 4001's C's shape, and its log_event, this image's own:
 * each logs a message by the text's address, the image's own, where the
 * decompiler's reading passes a copy of the text, so that the entry it
 * wrote kept a stack address (docs/firmware/58503b-1.01.04.md, "The
 * function check").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

/* The event log: LOG_PAGES pages of three entries, 64 bytes each, in the
 * EEPROM from 0x400d80 (log_page_read). */
static const uint8_t LOG_PAGES = 74;

/* log_page_write's result when the EEPROM write fails, as its low byte
 * (0x4162c). */
static const uint8_t LOG_PAGE_WRITE_FAILED = 0xfe;

/* Empties the event log, every page in the EEPROM, reporting a memory
 * error for source if a write fails, resets the log's cursors, its
 * oldest and newest entries and its lap, and logs "Log cleared" if no
 * write failed; returns the first failure's low byte, or 0 (0x415f4). */
char log_erase(request_source source) {
  uint32_t status = 0;

  log_page_clear(&log_write_page);
  log_page_clear(&log_page_buffer);
  for (int32_t page = 0; page < LOG_PAGES; page++) {
    status = (uint8_t)log_page_write(page, &log_write_page);
    if (status != 0) {
      if (status == LOG_PAGE_WRITE_FAILED) {
        status_report_from(STATUS_MEMORY_ERROR, source);
      }
      break;
    }
  }
  log_oldest = 0;
  log_newest = -1;
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
  return status;
}

/* One of log_event's messages: its format, the image's text at its own
 * address, which an entry keeps, and for a holdover's the text its %s
 * takes, else none. */
typedef struct log_event_text {
  char* format;
  char* reason;
} log_event_text;

/* "Holdover started, %s", the holdovers' format (0x41b24). */
static char* const LOG_HOLDOVER_FORMAT = (char*)0x4205a;

/* log_event's messages, by code (its jump table, 0x41b4c); the position
 * hold's, which logs two entries, is its own case. */
static const log_event_text LOG_EVENTS[] = {
    [LOG_CLEARED] = {(char*)0x42002, NULL},
    [LOG_POWER_ON] = {(char*)0x4206f, NULL},
    [LOG_REBOOT] = {(char*)0x42078, NULL},
    [LOG_SURVEY_STARTED] = {(char*)0x42080, NULL},
    [LOG_POSITION_HOLD_STARTED] = {NULL, NULL},
    [LOG_EXT_LOCK_STARTED] = {(char*)0x420ed, NULL},
    [LOG_GPS_LOCK_STARTED] = {(char*)0x420fe, NULL},
    [LOG_HOLDOVER_MANUAL] = {LOG_HOLDOVER_FORMAT, (char*)0x4210f},
    [LOG_HOLDOVER_EXT] = {LOG_HOLDOVER_FORMAT, (char*)0x42151},
    [LOG_HOLDOVER_INVALID_EXT_PPS] = {LOG_HOLDOVER_FORMAT, (char*)0x42155},
    [LOG_HOLDOVER_GPS] = {LOG_HOLDOVER_FORMAT, (char*)0x420c3},
    [LOG_HOLDOVER_INVALID_GPS_PPS] = {LOG_HOLDOVER_FORMAT, (char*)0x42140},
    [LOG_HOLDOVER_NOT_TRACKING] = {LOG_HOLDOVER_FORMAT, (char*)0x42116},
    [LOG_HOLDOVER_RAIM_ALARM] = {LOG_HOLDOVER_FORMAT, (char*)0x42127},
    [LOG_HOLDOVER_GPS_ALARM] = {LOG_HOLDOVER_FORMAT, (char*)0x42136},
    [LOG_HOLDOVER_HW_ERROR] = {LOG_HOLDOVER_FORMAT, (char*)0x42166},
    [LOG_HOLDOVER_TI_ERROR] = {LOG_HOLDOVER_FORMAT, (char*)0x4216f},
    [LOG_HOLDOVER_TI_LIMIT] = {LOG_HOLDOVER_FORMAT, (char*)0x42178},
    [LOG_HOLDOVER_TEMPORARY] = {LOG_HOLDOVER_FORMAT, (char*)0x4218a},
    [LOG_HOLDOVER_SELECTING_PPS] = {LOG_HOLDOVER_FORMAT, (char*)0x42194},
    [LOG_HOLDOVER_STARTED] = {(char*)0x421a3, NULL},
    [LOG_EXT_REFERENCE_VALID] = {(char*)0x421b4, NULL},
    [LOG_SPECIFICATIONS_EXCEEDED] = {(char*)0x421c8, NULL},
    [LOG_HARDWARE_FAILURE] = {(char*)0x421e7, NULL},
    [LOG_SELFTEST_FAILED] = {(char*)0x421f8, NULL},
    [LOG_EEPROM_SAVE_FAILED] = {(char*)0x42209, NULL},
    [LOG_OUT_OF_EVENT_BUFFERS] = {(char*)0x4221c, NULL},
    [LOG_SYSTEM_PRESET] = {(char*)0x42239, NULL},
};

/* The position hold's two entries: "Position hold started.  [%s Hgt =
 * %+8d cm]" with "MSL" or "GPS" and the held height, then "     [Lat =
 * %+10d ms, Lon = %+10d ms]" (0x41bc4 to 0x41bf6). */
static char* const LOG_POSITION_HOLD_FORMAT = (char*)0x42094;
static char* const LOG_POSITION_HOLD_LATITUDE_FORMAT = (char*)0x420c7;
static char* const LOG_HEIGHT_MSL = (char*)0x420bf;
static char* const LOG_HEIGHT_GPS = (char*)0x420c3;

/* Logs one of log_event's messages by its code, a holdover's with its
 * reason and the position hold's as two entries, with the held
 * position, its height's datum by gps.field_608's MSL bit; returns the
 * last log_write's result, or 0xfd for a code past them (0x41b1a).  The
 * Z3816A 4001's in its shape, this image's messages. */
int32_t log_event(log_event_code code) {
  /* The result for a code it has no message for. */
  static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

  if (code >= sizeof LOG_EVENTS / sizeof LOG_EVENTS[0]) {
    return LOG_EVENT_UNKNOWN;
  }
  if (code == LOG_POSITION_HOLD_STARTED) {
    log_write(LOG_POSITION_HOLD_FORMAT,
              (gps.field_608 & GPS_FIELD_608_HEIGHT_MSL) != 0 ? LOG_HEIGHT_MSL
                                                              : LOG_HEIGHT_GPS,
              nv_settings.position[2]);
    return log_write(LOG_POSITION_HOLD_LATITUDE_FORMAT, nv_settings.position[0],
                     nv_settings.position[1]);
  }
  if (LOG_EVENTS[code].reason != NULL) {
    return log_write(LOG_EVENTS[code].format, LOG_EVENTS[code].reason);
  }
  return log_write(LOG_EVENTS[code].format);
}

/* An entry's lap, log_lap when log_append wrote it: the first byte of
 * its time, which a time_of_day leaves free (structures.md, "A date and
 * time"), log_append's store over it. */
static uint8_t* log_entry_lap(log_entry* entry) {
  return (uint8_t*)&entry->time;
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

/* The lap of page 0's first entry, log_init's lap to start from;
 * the page read whether its checksum holds or not (0x41e2a). */
uint8_t log_first_lap(void) {
  log_page page;

  log_page_read(0, &page);
  return *log_entry_lap(&page.entries[0]);
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

/* A page's first entry of another lap than log_lap's, the lap before, or
 * 0 for none (0x41e00). */
log_entry* log_page_previous_lap_entry(log_page* page) {
  for (log_entry* entry = page->entries;
       entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++) {
    if (*log_entry_lap(entry) != log_lap) {
      return entry;
    }
  }
  return 0;
}

/* The event log's pages in the EEPROM, at 0x400d80, which the export
 * declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d80;

/* Reads an event log page from the EEPROM; 0xff if its checksum fails,
 * else 0 (0x414d8). */
uint32_t log_page_read(uint8_t page, log_page* destination) {
  static const uint32_t CHECKSUM_FAILS = 0xff;

  *destination = LOG_EEPROM_PAGES[page];
  return log_page_checksum(destination) == destination->checksum
             ? 0
             : CHECKSUM_FAILS;
}

/* Writes an event log page to the EEPROM if its entries have changed,
 * its write count one past the EEPROM's and its checksum made;
 * LOG_PAGE_WRITE_FAILED if the write fails, else 0 (0x41462). */
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

/* 1 when a reset-cause record's checksum holds (0x43362). */
uint8_t reset_record_valid(uint8_t index) {
  return reset_record_checksum(&eeprom_reset_records[index]) ==
         eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x43398). */
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
 * when the number was clamped or the entry is unused (0x43476). */
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
 * full; 0 when written (0x43572). */
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

/* The ring's entries, LOG_PAGES pages of three, and the count past which
 * log_init and log_append report the log almost full. */
static const int32_t LOG_ENTRIES =
    LOG_PAGES *
    (sizeof log_write_page.entries / sizeof log_write_page.entries[0]);

static const int16_t LOG_ENTRIES_REPORTED = 177;

/* The first entry of a page with no format, or whose format lies past
 * the ROM's last long, which no text in the image does, an erased
 * entry's among them; 0 for none (0x41dce).  The Z3817A 3752's;
 * common/'s takes no format as free alone. */
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
 * holds the same page (0x41e46).  The Z3817A 3752's; common/'s also
 * clears log_write_page_open, which this image does not keep. */
uint8_t log_flush(void) {
  if (log_page_number == log_write_page_number) {
    memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
  }
  return log_page_write(log_write_page_number, &log_write_page);
}

/* Creates log_mutex and finds the ring's ends (0x41748).  The page to
 * write next is the first with a free entry, or whose checksum fails,
 * its first entry then; or the first with an entry of the lap before,
 * which is the oldest, the log full.  With neither in any page every
 * entry is of one lap: the log is full, its newest entry the last, and
 * the next written the first, of the other lap, whose page it reads
 * back.  The Z3817A 3752's; like the Z3815A's, it returns a page's read
 * error with log_mutex locked. */
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
 * log_cursor and writes its page (0x41e8c): full, it goes on while
 * :DIAGnostic:LOG:WRAP is set, over the oldest entry; past a page's last
 * entry it reads the next page back from the EEPROM, past the last page
 * the first, the log full and the lap flipped; and the count stops at the
 * ring's size.  The Z3817A 3752's; the Z3815A's reads the page before it
 * adds the entry and keeps log_write_page_open. */
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

/* An entry keeps its format by address, in the EEPROM, where the log
 * outlives a restart and the code that wrote it: every format the C
 * logs is the image's own text, at its own address.  log_read knows by
 * their addresses the two formats it gives a date and a time, not a byte
 * and a long.  The export declares no object for them. */
static char* const LOG_TEXT_EXT_REFERENCE_VALID_AT = (char*)0x41f9c;

static char* const LOG_TEXT_GPS_REFERENCE_VALID_AT = (char*)0x41fcf;

/* Logs when the GPS reference became valid, a date and a time, as the
 * Z3805A's logs its first satellite tracked (0x41d88). */
void log_first_satellite(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_GPS_REFERENCE_VALID_AT, date, time);
}

/* Logs when the external reference became valid, a date and a time
 * (0x41da0). */
void log_ext_reference_valid(uint32_t date, time_of_day time) {
  log_write(LOG_TEXT_EXT_REFERENCE_VALID_AT, date, time);
}

/* Event log entry number (from 1, from the ring's oldest, clamped to the
 * log): its text and a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either
 * pointer 0 for none; 0, or mutex_lock's error (0x418d6).  The entry is
 * counted from log_oldest, its page taken past the last back to the
 * first, and its first argument passed whole.  The Z3817A 3752's; the
 * Z3815A's reads the page whether log_page_buffer holds it or not and
 * flushes an open page. */
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

/* "Log status: ...", the event log's (reset_causes false) or the
 * reset-cause log's entries, into text, then " (overwriting)" or
 * " (full)", as :DIAGnostic:LOG:WRAP is set or not, when either log is
 * full (0x3a800).  The Z3817A 3752's gives "Log status: Full" alone for
 * a full log, and an error falls through to "Empty" there. */
void log_status(char* text, char reset_causes) {
  uint8_t event_log_full = 0;
  uint8_t reset_log_full = 0;
  int16_t count = reset_causes ? log_count(&reset_log_full)
                               : log_entry_total(&event_log_full);

  switch (count) {
    case -1:
      sprintf(text, "Log status: Error");
      break;
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
  if (event_log_full || reset_log_full) {
    strcat(text, nv_settings.log_wrap ? " (overwriting)" : " (full)");
  }
}
