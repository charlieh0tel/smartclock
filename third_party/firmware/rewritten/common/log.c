/* The event log and the reset-cause log, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/log`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* Clears the last exception and the crash message. */
void exception_record_clear(void) {
  reset_record_count();
  last_exception.pending = 0;
  crash_message[0] = 0;
}

/* Empties a page. */
void log_page_clear(void* page) { memset(page, 0, sizeof(log_page)); }

/* log_erase under log_mutex. */
int32_t log_erase_locked(request_source source) {
  int32_t error = mutex_lock(&log_mutex);

  if (error != 0) {
    return error;
  }
  int32_t status = log_erase(source);
  mutex_unlock(&log_mutex);
  return status;
}

/* log_erase under log_mutex when the log holds count entries. */
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

/* The event log's entries, and through full whether it is full. */
int32_t log_entry_total(uint8_t* full) {
  if (full != 0) {
    *full = log_full;
  }
  return log_entries;
}

/* Appends an entry to the event log under its lock; returns the lock's
 * error or log_append's status.  Variadic in name: an entry
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

/* A log line's text and heading buffers: an array size, so #define. */
#define LOG_TEXT_SIZE 80

/* Prints the event log or the reset-cause log: its status, then each
 * entry under its heading. */
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
 * time on. */
uint16_t reset_record_checksum(reset_record* record) {
  return records_sum_and_complement(records_byte_sum(
      (const uint8_t*)&record->time,
      sizeof *record - __builtin_offsetof(reset_record, time)));
}

/* The reset-cause log's entries, and through full whether it is full. */
int32_t log_count(uint8_t* full) {
  if (full != 0) {
    *full = log_entry_count >= RECORDS_RESET_RECORDS;
  }
  return log_entry_count;
}

/* Counts the valid reset-cause records from the first into
 * log_entry_count. */
void reset_record_count(void) {
  log_entry_count = 0;
  for (uint8_t index = 0;
       index < RECORDS_RESET_RECORDS && reset_record_valid(index); index++) {
    log_entry_count++;
  }
}

/* The sum and its complement of an event log page's entries. */
uint16_t log_page_checksum(log_page* page) {
  return records_sum_and_complement(
      records_byte_sum((const uint8_t*)page->entries, sizeof page->entries));
}
