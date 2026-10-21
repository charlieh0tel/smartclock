/* The klok task, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): a list of clock entries,
 * klok_entries, kept in time order, a free list, klok_free, of five in
 * klok_pool, and klok_mutex guarding both (docs/firmware/structures.md,
 * "The clock and the log").  Each second klok_task takes the 1PPS's GPS
 * time and runs the entries due.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype.
 *
 * list_unlink, list_push and klok_insert_sorted take each list as an
 * int32_t pointer, by their prototypes; their callers here cast. */

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

/* The time, then the date, of the 1PPS klok_task last received, at
 * 0x1037ac, which the names file does not name: the "now" klok_run_due
 * compares each entry with. */
static uint32_t *const KLOK_NOW = (uint32_t *)0x1037ac;

/* A clock entry's time and date, laid out as a packed_datetime is, which
 * datetime_add and datetime_subtract take; the image passes the entry's
 * +4 (0x10bca) as one. */
static packed_datetime *klok_when(uint32_t *time)
{
	return (packed_datetime *)time;
}

/* Queues a copy of an event on the clock list from the free list; NULL
 * with none free or the mutex not taken (0x10540). */
klok_entry *clk_do(klok_entry *event)
{
	/* log_event's code for "Clk_do: out of event buffers" (0x10598). */
	static const uint8_t LOG_OUT_OF_BUFFERS = 0x19;
	char message[KLOK_MESSAGE_SIZE];
	uint32_t status = mutex_lock(&klok_mutex);

	if (status != 0) {
		debug_sprintf(message, "Clk_do: error %#04x\r\n", status);
		task_dmessage(message);
		return NULL;
	}
	klok_entry *entry = klok_free;

	if (entry == NULL) {
		task_dmessage("Clk_do: out of event buffers");
		log_event(LOG_OUT_OF_BUFFERS);
	} else {
		klok_free = entry->next;
		*entry = *event;
		klok_insert_sorted((int32_t *)&klok_entries, (int32_t *)entry);
	}
	mutex_unlock(&klok_mutex);
	return entry;
}

/* Takes an entry off the clock list and puts it on the free list, under
 * klok_mutex: the mutex's error, NOT_IN_POOL, list_unlink's -2 for an
 * entry not on the list, or 0 (0x10786). */
int32_t klok_cancel(klok_entry *entry)
{
	/* The result for an entry outside klok_pool (0x107b0). */
	static const int32_t NOT_IN_POOL = -3;
	int32_t status = mutex_lock(&klok_mutex);

	if (status != 0)
		return status;
	if (entry < klok_pool || entry > &klok_pool[KLOK_POOL_SIZE - 1]) {
		status = NOT_IN_POOL;
	} else {
		status = list_unlink((int32_t *)&klok_entries, (int32_t *)entry);
		if (status == 0)
			list_push((uint32_t *)&klok_free, (uint32_t *)&entry->next);
	}
	mutex_unlock(&klok_mutex);
	return status;
}

/* print_clk_list: each clock entry's time, period, function and class
 * (0x107ee). */
void klok_print_entries(void)
{
	if (mutex_lock(&klok_mutex) != 0) {
		debug_printf("Sem_open of clk_msg failed\r\n");
		return;
	}
	if (klok_entries == NULL)
		debug_printf("No clock entries\r\n");
	for (klok_entry *entry = klok_entries; entry != NULL; entry = entry->next) {
		uint32_t date = entry->date;
		uint32_t time = entry->time;

		debug_printf("at:\t%02d/%02d/%04d\t%2d:%2d:%2d\r\n", date >> 8 & 0xff, date & 0xff,
			     date >> 16, time >> 16 & 0xff, time >> 8 & 0xff, time & 0xff);
		date = entry->period_date;
		time = entry->period_time;
		debug_printf("period:\t%02d/%02d/%04d\t%2d:%2d:%2d\r\n", date >> 8 & 0xff, date & 0xff,
			     date >> 16, time >> 16 & 0xff, time >> 8 & 0xff, time & 0xff);
		debug_printf("function address:\t%8X\r\n", (uint32_t)entry->function);
		debug_printf("event class:\t%d\r\n", (int32_t)entry->event_class);
	}
	mutex_unlock(&klok_mutex);
}

/* The first entry on the clock list of an event class, under klok_mutex;
 * NULL for none or the mutex not taken (0x108fe). */
klok_entry *klok_find(int16_t event_class)
{
	int32_t status = mutex_lock(&klok_mutex);
	klok_entry *entry = klok_entries;

	if (status != 0)
		return NULL;
	while (entry != NULL && entry->event_class != event_class)
		entry = entry->next;
	mutex_unlock(&klok_mutex);
	return entry;
}

/* Whether an entry is on the clock list, on the free list or on neither
 * (0x10946). */
int32_t klok_entry_state(klok_entry *entry)
{
	if (entry == NULL || mutex_lock(&klok_mutex) != 0)
		return KLOK_ON_NEITHER;
	int32_t state = KLOK_ON_NEITHER;

	for (klok_entry *on = klok_entries; on != NULL; on = on->next)
		if (on == entry)
			state = KLOK_ON_LIST;
	if (state == KLOK_ON_NEITHER)
		for (klok_entry *on = klok_free; on != NULL; on = on->next)
			if (on == entry)
				state = KLOK_ON_FREE_LIST;
	mutex_unlock(&klok_mutex);
	return state;
}

/* Starts the klok task, unless one is already there: the 1PPS exchange,
 * the kernel's clock at midnight of the lifetime record's date, the free
 * list, the task and klok_mutex.  The message it ends with is unset when
 * nothing failed, as the image's is (0x109ae). */
uint32_t klok_task_create(uint32_t priority)
{
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
		for (klok_entry *entry = klok_pool; entry < &klok_pool[KLOK_POOL_SIZE]; entry++) {
			entry->period_date = 0;
			entry->date = 0;
			entry->period_time = 0;
			entry->time = 0;
			entry->next = entry + 1;
		}
		klok_pool[KLOK_POOL_SIZE - 1].next = NULL;
	}
	if (status == 0)
		status = (uint8_t)psos_create("klok", 0, priority, SUPERVISOR_STACK,
					      USER_STACK, NULL, &task_klok);
	if (status == 0)
		status = (uint8_t)psos_start(task_klok, klok_task);
	if (status == 0)
		status = mutex_create("klok", &klok_mutex);
	else
		debug_sprintf(message, "Clk_go error %#04x\r\n", status);
	task_dmessage(message);
	return status;
}

/* The klok task: in supervisor mode, at each 1PPS, the idle count of the
 * second past, the entries due, and the watchdog's clock count
 * (0x10b1e). */
void klok_task(void)
{
	/* pps_wait_time's argument to wait for the next 1PPS. */
	static const int32_t WAIT = 1;

	psos_supervisor();
	for (;;) {
		pps_wait_time(&KLOK_NOW[0], &KLOK_NOW[1], WAIT);
		idle_loops_last_second = klok_take_pending();
		if (klok_entries != NULL)
			klok_run_due();
		watchdog_clock++;
	}
}

/* Runs the clock list's entries due now, under klok_mutex: an entry's
 * function runs, through spoo, only at its very second, and one past is
 * dropped unrun; a periodic one goes back on the list a period on, the
 * rest on the free list (0x10b62). */
void klok_run_due(void)
{
	klok_entry *entry = klok_entries;
	int32_t order;

	mutex_lock(&klok_mutex);
	while (entry != NULL && (order = datetime_compare(&entry->time, KLOK_NOW)) <= 0) {
		if (order == 0 && entry->function != NULL)
			spoo_run((spoo_function *)entry->function, 0, 0, 0);
		list_unlink((int32_t *)&klok_entries, (int32_t *)entry);
		if (entry->period_time == 0 && entry->period_date == 0) {
			list_push((uint32_t *)&klok_free, (uint32_t *)&entry->next);
		} else {
			datetime_add(klok_when(&entry->period_time), klok_when(&entry->time));
			klok_insert_sorted((int32_t *)&klok_entries, (int32_t *)entry);
		}
		entry = klok_entries;
	}
	mutex_unlock(&klok_mutex);
}

/* Takes and clears the idle loops counted since the last call (0x1303c). */
uint32_t klok_take_pending(void)
{
	uint32_t loops = idle_loops;

	idle_loops = 0;
	return loops;
}
