/* klok.c's functions that are this image's own: the rest are
 * common/klok.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

#include <stddef.h>

/* A clock entry's time and date, laid out as a packed_datetime is, which
 * datetime_add and datetime_subtract take; the image passes the entry's
 * +4 (0x10b86, 0x10bee) as one. */
static packed_datetime* klok_when(uint32_t* time) {
  return (packed_datetime*)time;
}

/* Queues a copy of an event on the clock list from the free list; NULL
 * with none free or the mutex not taken (0x10540). */
klok_entry* clk_do(klok_entry* event) {
  char message[KLOK_MESSAGE_SIZE];
  uint32_t status = mutex_lock(&klok_mutex);

  if (status != 0) {
    debug_sprintf(message, "Clk_do: error %#04x\r\n", status);
    task_dmessage(message);
    return NULL;
  }
  klok_entry* entry = klok_free;

  if (entry == NULL) {
    task_dmessage("Clk_do: out of event buffers");
    log_event_58503a(LOG_OUT_OF_EVENT_BUFFERS);
  } else {
    klok_free = entry->next;
    *entry = *event;
    klok_insert_sorted(&klok_entries, entry);
  }
  mutex_unlock(&klok_mutex);
  return entry;
}

/* Runs the clock list's entries due now, under klok_mutex: an entry's
 * function runs, through spoo, at its very second, and so does a periodic
 * one's that is past, moved to now first (0x10ba8), where the Z3805A's
 * drops it unrun as it does a one-shot entry past; a periodic entry goes
 * back on the list a period on, the rest on the free list (0x10b62). */
void klok_run_due(void) {
  klok_entry* entry = klok_entries;
  int32_t order;

  mutex_lock(&klok_mutex);
  while (entry != NULL &&
         (order = datetime_compare(&entry->time, klok_now)) <= 0) {
    if (order < 0 && (entry->period_time != 0 || entry->period_date != 0)) {
      entry->time = klok_now[0];
      entry->date = klok_now[1];
      order = 0;
    }
    if (order == 0 && entry->function != NULL) {
      spoo_run(entry->function, 0, 0, 0);
    }
    list_unlink(&klok_entries, entry);
    if (entry->period_time == 0 && entry->period_date == 0) {
      list_push(&klok_free, entry);
    } else {
      datetime_add(klok_when(&entry->period_time), klok_when(&entry->time));
      klok_insert_sorted(&klok_entries, entry);
    }
    entry = klok_entries;
  }
  mutex_unlock(&klok_mutex);
}
