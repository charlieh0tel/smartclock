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

/* An entry keeps its format by address, in the EEPROM, where the log
 * outlives a restart and the code that wrote it: every format the C
 * logs is the image's own text, at its own address, so that an entry
 * the image wrote reads back as it does and one the C writes is the
 * image's, byte for byte (docs/firmware/rewrite.md, "Booting the C").
 * log_read knows by that address the one format it gives a date and a
 * time, not a byte and a long: log_text_first_satellite_tracked_at. */

/* The sum and its complement of an event log page's entries (0x30202). */
uint16_t log_page_checksum(log_page* page) {
  return records_sum_and_complement(
      records_byte_sum((const uint8_t*)page->entries, sizeof page->entries));
}

/* The event log's pages in the EEPROM, at 0x400d40, which the export
 * declares no object for. */
static log_page* const LOG_EEPROM_PAGES = (log_page*)0x400d40;

/* log_page_write's result when the EEPROM write fails. */
static const uint32_t LOG_PAGE_WRITE_FAILED = 0xfffffffe;

/* Reads an event log page from the EEPROM; 0xff if its checksum fails,
 * else 0 (0x302a2). */
uint32_t log_page_read(uint8_t page, log_page* destination) {
  static const uint32_t CHECKSUM_FAILS = 0xff;

  *destination = LOG_EEPROM_PAGES[page];
  return log_page_checksum(destination) == destination->checksum
             ? 0
             : CHECKSUM_FAILS;
}

/* Writes an event log page to the EEPROM if its entries have changed,
 * its write count one past the EEPROM's and its checksum made;
 * LOG_PAGE_WRITE_FAILED if the write fails, else 0 (0x3022c). */
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
 * did; returns the first failure's low byte, or 0 (0x303be). */
char log_erase(request_source source) {
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
    log_append(log_text_log_cleared, 0, 0);
  }
  return status;
}

/* Creates log_mutex and finds the event log's page to write next, the
 * first with a free entry or whose checksum fails (0x30504). */
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

/* Logs when the first satellite was tracked, a date and a time
 * (0x309da). */
void log_first_satellite(uint32_t date, uint32_t time) {
  log_write(log_text_first_satellite_tracked_at, date, time);
}

/* log_event's messages, by code: the image's texts. */
static char* const records_log_events[] = {
    log_text_log_cleared,
    log_text_power_on,
    log_text_re_boot,
    log_text_survey_mode_started,
    log_text_position_hold_mode_started,
    log_text_ext_lock_started,
    log_text_gps_lock_started,
    log_text_holdover_started_manual,
    log_text_holdover_started_ext,
    log_text_holdover_started_invalid_ext_1pps,
    log_text_holdover_started_gps,
    log_text_holdover_started_invalid_gps_1pps,
    log_text_holdover_started_not_tracking_gps,
    log_text_holdover_started_gps_raim_alarm,
    log_text_holdover_started_gps_alarm,
    log_text_holdover_started_hw_error,
    log_text_holdover_started_ti_error,
    log_text_holdover_started_ti_limit_exceeded,
    log_text_holdover_started_temporary,
    log_text_holdover_started_selecting_1pps,
    log_text_holdover_started,
    log_text_output_specifications_exceeded,
    log_text_hardware_failure,
    log_text_self_test_failed,
    log_text_eeprom_save_failed,
    log_text_clk_do_out_of_event_buffers,
    log_text_system_preset,
};

/* Logs one of log_event's messages by its code (0x307da). */
int32_t log_event(log_event_code code) {
  /* The result for a code it has no message for. */
  static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

  if (code >= sizeof records_log_events / sizeof records_log_events[0]) {
    return LOG_EVENT_UNKNOWN;
  }
  return log_write(records_log_events[code]);
}

/* Adds an entry, its format, arguments, date and time, to the event log
 * and writes its page, moving to the next page after its last entry
 * (0x30a7a). */
uint32_t log_append(char* format, int32_t argument_1, int32_t argument_2) {
  if (log_full) {
    return RECORDS_LOG_FULL;
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

/* "Log status: ...", the event log's (reset_causes false) or the
 * reset-cause log's entries, into text (0x2a386). */
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

/* 1 when a reset-cause record's checksum holds (0x31e2c). */
uint8_t reset_record_valid(uint8_t index) {
  return reset_record_checksum(&eeprom_reset_records[index]) ==
         eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x31e62). */
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
 * when the number was clamped or the entry is unused (0x31f40). */
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
 * log is full; 0 when written (0x3203c). */
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
  record.checksum = reset_record_checksum(&record);
  record.sequence = eeprom_reset_records[index].sequence + 1;
  if (!eeprom_write((char*)&eeprom_reset_records[index], (char*)&record,
                    sizeof record)) {
    return 1;
  }
  log_entry_count++;
  return 0;
}
