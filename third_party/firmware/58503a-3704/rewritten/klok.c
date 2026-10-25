/* The klok task, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): a list of clock entries,
 * klok_entries, kept in time order, a free list, klok_free, of five in
 * klok_pool, and klok_mutex guarding both (docs/firmware/structures.md,
 * "The clock and the log").  Each second klok_task takes the 1PPS's GPS
 * time and runs the entries due.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

#include <stddef.h>

/* klok_entry_state's results. */
enum {
  KLOK_ON_LIST = 0,
  KLOK_ON_FREE_LIST = 1,
  KLOK_ON_NEITHER = -1,
};

static const uint32_t KLOK_POOL_SIZE = 5;

/* An array size, so #define. */
#define KLOK_MESSAGE_SIZE 80

/* A clock entry's time and date, laid out as a packed_datetime is, which
 * datetime_add and datetime_subtract take; the image passes the entry's
 * +4 (0x10b86, 0x10bee) as one. */
static packed_datetime* klok_when(uint32_t* time) {
  return (packed_datetime*)time;
}

/* Links an entry into a list sorted by time and date, after every entry
 * not later than it (0x10c5a). */
void klok_insert_sorted(klok_entry** list, klok_entry* entry) {
  while (*list != NULL && datetime_compare(&(*list)->time, &entry->time) <= 0) {
    list = &(*list)->next;
  }
  entry->next = *list;
  *list = entry;
}

/* Takes an entry off the clock list and puts it on the free list, under
 * klok_mutex: the mutex's error, NOT_IN_POOL, list_unlink's -2 for an
 * entry not on the list, or 0 (0x10786). */
int32_t klok_cancel(klok_entry* entry) {
  /* The result for an entry outside klok_pool (0x107b0). */
  static const int32_t NOT_IN_POOL = -3;
  int32_t status = mutex_lock(&klok_mutex);

  if (status != 0) {
    return status;
  }
  if (entry < klok_pool || entry > &klok_pool[KLOK_POOL_SIZE - 1]) {
    status = NOT_IN_POOL;
  } else {
    status = list_unlink(&klok_entries, entry);
    if (status == 0) {
      list_push(&klok_free, entry);
    }
  }
  mutex_unlock(&klok_mutex);
  return status;
}

/* print_clk_list: each clock entry's time, period, function and class
 * (0x107ee). */
void klok_print_entries(void) {
  if (mutex_lock(&klok_mutex) != 0) {
    debug_printf("Sem_open of clk_msg failed\r\n");
    return;
  }
  if (klok_entries == NULL) {
    debug_printf("No clock entries\r\n");
  }
  for (klok_entry* entry = klok_entries; entry != NULL; entry = entry->next) {
    uint32_t date = entry->date;
    uint32_t time = entry->time;

    debug_printf("at:\t%02d/%02d/%04d\t%2d:%2d:%2d\r\n", date >> 8 & 0xff,
                 date & 0xff, date >> 16, time >> 16 & 0xff, time >> 8 & 0xff,
                 time & 0xff);
    date = entry->period_date;
    time = entry->period_time;
    debug_printf("period:\t%02d/%02d/%04d\t%2d:%2d:%2d\r\n", date >> 8 & 0xff,
                 date & 0xff, date >> 16, time >> 16 & 0xff, time >> 8 & 0xff,
                 time & 0xff);
    debug_printf("function address:\t%8X\r\n", (uint32_t)entry->function);
    debug_printf("event class:\t%d\r\n", (int32_t)entry->event_class);
  }
  mutex_unlock(&klok_mutex);
}

/* The first entry on the clock list of an event class, under klok_mutex;
 * NULL for none or the mutex not taken (0x108fe). */
klok_entry* klok_find(int16_t event_class) {
  int32_t status = mutex_lock(&klok_mutex);
  klok_entry* entry = klok_entries;

  if (status != 0) {
    return NULL;
  }
  while (entry != NULL && entry->event_class != event_class) {
    entry = entry->next;
  }
  mutex_unlock(&klok_mutex);
  return entry;
}

/* Whether an entry is on the clock list, on the free list or on neither
 * (0x10946). */
int32_t klok_entry_state(klok_entry* entry) {
  if (entry == NULL || mutex_lock(&klok_mutex) != 0) {
    return KLOK_ON_NEITHER;
  }
  int32_t state = KLOK_ON_NEITHER;

  for (klok_entry* on = klok_entries; on != NULL; on = on->next) {
    if (on == entry) {
      state = KLOK_ON_LIST;
    }
  }
  if (state == KLOK_ON_NEITHER) {
    for (klok_entry* on = klok_free; on != NULL; on = on->next) {
      if (on == entry) {
        state = KLOK_ON_FREE_LIST;
      }
    }
  }
  mutex_unlock(&klok_mutex);
  return state;
}

/* Starts the klok task, unless one is already there: the 1PPS exchange,
 * the kernel's clock at midnight of the lifetime record's date, the free
 * list, the task and klok_mutex.  The message it ends with is unset when
 * nothing failed, as the image's is (0x109ae). */
uint32_t klok_task_create(uint32_t priority) {
  /* The klok task's supervisor and user stacks, in words (psos.md,
   * service 1). */
  static const uint32_t SUPERVISOR_STACK = 0x100;
  static const uint32_t USER_STACK = 0x10;
  char message[KLOK_MESSAGE_SIZE];
  uint32_t status = (uint8_t)psos_ident("klok", NULL, NULL) == 0;

  if (status == 0) {
    status = pps_init();
    if (status != 0) {
      debug_sprintf(message, "Pps_init error %#04x\r\n", status);
      task_dmessage(message);
    }
  }
  if (status == 0) {
    psos_set_time(lifetime.date, 0, 0);
    klok_entries = NULL;
    klok_free = klok_pool;
    for (klok_entry* entry = klok_pool; entry < &klok_pool[KLOK_POOL_SIZE];
         entry++) {
      entry->period_date = 0;
      entry->date = 0;
      entry->period_time = 0;
      entry->time = 0;
      entry->next = entry + 1;
    }
    klok_pool[KLOK_POOL_SIZE - 1].next = NULL;
  }
  if (status == 0) {
    status = (uint8_t)psos_create("klok", 0, priority, SUPERVISOR_STACK,
                                  USER_STACK, NULL, &task_klok);
  }
  if (status == 0) {
    status = (uint8_t)psos_start(task_klok, klok_task);
  }
  if (status == 0) {
    status = mutex_create("klok", &klok_mutex);
  } else {
    debug_sprintf(message, "Clk_go error %#04x\r\n", status);
  }
  task_dmessage(message);
  return status;
}

/* The klok task: in supervisor mode, at each 1PPS, the idle count of the
 * second past, the entries due, and the watchdog's clock count
 * (0x10b1e). */
void klok_task(void) {
  /* pps_wait_time's argument to wait for the next 1PPS. */
  static const int32_t WAIT = 1;

  psos_supervisor();
  for (;;) {
    pps_wait_time(&klok_now[0], &klok_now[1], WAIT);
    idle_loops_last_second = klok_take_pending();
    if (klok_entries != NULL) {
      klok_run_due();
    }
    watchdog_clock++;
  }
}

/* Takes and clears the idle loops counted since the last call (0x13210). */
uint32_t klok_take_pending(void) {
  uint32_t loops = idle_loops;

  idle_loops = 0;
  return loops;
}
