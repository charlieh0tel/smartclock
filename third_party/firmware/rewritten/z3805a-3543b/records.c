/* The EEPROM and the records it keeps, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): its lock and
 * writes, the parameter, identification, lifetime and calibration
 * records, the NV register, the event log and the reset-cause log.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  docs/firmware/structures.md describes the
 * records ("EEPROM records", "The NV register", "The lifetime record")
 * and the lock ("The EEPROM's lock"). */

/* An erased EEPROM byte, which every writer fills its record with
 * before it fills the fields. */
static const uint8_t RECORDS_ERASED = 0xff;
static const uint32_t RECORDS_ERASED_LONG = 0xffffffffu;

/* The EEPROM writes a page of 64 bytes at once (eeprom_write_page). */
static const uint32_t RECORDS_EEPROM_PAGE_SIZE = 64u;
/* Tries at a write, and polls of the last byte written for it to read
 * back, on each try. */
static const int32_t RECORDS_EEPROM_TRIES = 5;
static const int16_t RECORDS_EEPROM_POLLS = 5000;

/* The status register's interrupt mask; eeprom_lock and eeprom_unlock
 * take the lock only at mask 0. */
static const uint16_t RECORDS_SR_INTERRUPT_MASK = 0x0700;
/* The length of eeprom_init's, eeprom_lock's and eeprom_unlock's fatal
 * message: an array size, so #define. */
#define RECORDS_MESSAGE_SIZE 80

/* A status_report code (status_report_messages, 0x2bf30). */
static const int16_t RECORDS_STATUS_MEMORY_ERROR = 0x10;
/* The status happening of a failed EEPROM write. */
static const char RECORDS_HAPPENING_EEPROM_WRITE = 'I';

/* The tag the NV register's and the identification record's writers
 * store, and the firmware revision the NV register's must match. */
static const char RECORDS_TAG[] = "S";
static const char RECORDS_FIRMWARE_REVISION[] = "3543B";

/* A date's and a time's fields, a byte each (docs/firmware/structures.md,
 * "A date and time"). */
static const uint8_t RECORDS_FIELD_BITS = 8;
static const uint32_t RECORDS_FIELD_MASK = 0xffu;

/* The reset-cause log's records. */
static const int32_t RECORDS_RESET_RECORDS =
	(int32_t)(sizeof eeprom_reset_records / sizeof eeprom_reset_records[0]);

static const uint8_t RECORDS_LONG_BITS = 32;

/* The sum of so many bytes, as every record checksum takes it. */
static uint8_t records_byte_sum(const uint8_t *bytes, uint32_t count)
{
	uint8_t sum = 0;

	for (uint32_t i = 0; i < count; i++)
		sum += bytes[i];
	return sum;
}

/* A record's checksum word: the sum in its high byte, its complement in
 * the low. */
static uint16_t records_sum_and_complement(uint8_t sum)
{
	return (uint16_t)(sum << RECORDS_FIELD_BITS | (uint8_t)~sum);
}

/* The sum and its complement of a record's bytes after its first two
 * (0x3082e). */
uint16_t record_checksum(void *record, uint16_t length)
{
	/* What a record's checksum word does not cover: the word itself,
	 * its first two bytes. */
	static const uint16_t CHECKSUM_SIZE = 2;

	if (length < CHECKSUM_SIZE)
		return 0;
	return records_sum_and_complement(records_byte_sum((const uint8_t *)record + CHECKSUM_SIZE,
							   (uint16_t)(length - CHECKSUM_SIZE)));
}

/* Reads the lifetime record into lifetime, or its defaults; true when its
 * checksum holds (0x30a3e). */
uint8_t lifetime_record_read(void)
{
	/* The lifetime record's defaults: 1994-01-01 (year << 16 | month << 8
	 * | day) and the EFC at mid-scale. */
	static const uint32_t LIFETIME_DEFAULT_DATE = 1994u << 16 | 1u << 8 | 1u;
	static const float EFC_MID_SCALE = 524288.0f;
	/* An EFC in the record with these bits all set, as an erased
	 * record's 0xffffffff has, is taken as mid-scale. */
	static const uint32_t LIFETIME_EFC_INVALID_BITS = 0xe8000000u;

	if (record_checksum(&eeprom_lifetime_record, sizeof eeprom_lifetime_record) !=
	    eeprom_lifetime_record.checksum) {
		lifetime.date = LIFETIME_DEFAULT_DATE;
		lifetime.count = 0;
		lifetime.efc = EFC_MID_SCALE;
		return 0;
	}
	lifetime.date = eeprom_lifetime_record.date;
	lifetime.count = eeprom_lifetime_record.count;

	/* The image tests the float's bits. */
	uint32_t efc_bits;
	float efc = eeprom_lifetime_record.efc;
	memcpy(&efc_bits, &efc, sizeof efc_bits);
	if ((efc_bits & LIFETIME_EFC_INVALID_BITS) == LIFETIME_EFC_INVALID_BITS)
		lifetime.efc = EFC_MID_SCALE;
	else
		lifetime.efc = efc;
	return 1;
}

/* Writes lifetime to the lifetime record when it differs; 1 when the
 * record holds it (0x30ab4). */
uint32_t lifetime_record_write(void)
{
	lifetime_record record;

	memset(&record, RECORDS_ERASED, sizeof record);
	record.count = lifetime.count;
	record.date = lifetime.date;
	record.efc = lifetime.efc;
	record.checksum = record_checksum(&record, sizeof record);
	if (memcmp(&record, &eeprom_lifetime_record, sizeof record) == 0)
		return 1;
	return eeprom_write((char *)&eeprom_lifetime_record, (char *)&record, sizeof record);
}

/* Counts another lifetime and takes the EFC in normal PLL, then writes
 * the record. */
static void records_lifetime_count(void)
{
	lifetime.count++;
	if (pll.stage == PLL_STAGE_NORMAL_PLL)
		lifetime.efc = pll.efc;
	lifetime_record_write();
}

/* lifetime_update's clock event: takes the GPS date when the time is
 * valid and counts another lifetime (0x30b30). */
void lifetime_event_run(void)
{
	if (time_valid())
		lifetime.date = get_gps_date();
	records_lifetime_count();
}

/* On a date unlike lifetime's, counts it and writes the record; then
 * schedules lifetime_event_run every three hours in place of the last
 * (0x30b66). */
void lifetime_update(int32_t date)
{
	/* The clock event's class, and its period, three hours as a packed
	 * time (hour << 16). */
	static const int16_t LIFETIME_EVENT_CLASS = 2;
	static const uint32_t LIFETIME_EVENT_PERIOD = 3u << 16;
	klok_entry event = lifetime_event_template;

	if (lifetime.date != (uint32_t)date) {
		lifetime.date = date;
		records_lifetime_count();
	}
	klok_entry *previous = klok_find(LIFETIME_EVENT_CLASS);
	if (previous != 0 && klok_cancel(previous) != 0)
		task_dmessage("Error in deleting old lifetime event\r\n");

	/* klok_entry's time and date, and its period's, are each a
	 * packed_datetime, which smartclock.h does not say. */
	get_gps_datetime(&event.date, (time_of_day *)&event.time);
	event.period_time = LIFETIME_EVENT_PERIOD;
	datetime_add((packed_datetime *)&event.period_time, (packed_datetime *)&event.time);
	event.function = lifetime_event_run;
	lifetime_event = clk_do(&event);
}

/* Reads the time offset and TC from the calibration block, or zeros;
 * true when its checksum holds (0x30c1e). */
bool calibration_block_read(void)
{
	bool valid = record_checksum(&eeprom_calibration_block, sizeof eeprom_calibration_block) ==
		     eeprom_calibration_block.checksum;

	if (valid) {
		scpi_value_DIAGnostic_TOFFset = eeprom_calibration_block.time_offset;
		scpi_value_DIAGnostic_ROSCillator_TCOefficient = eeprom_calibration_block.tc_coefficient;
	} else {
		scpi_value_DIAGnostic_TOFFset = 0.0f;
		scpi_value_DIAGnostic_ROSCillator_TCOefficient = 0.0f;
	}
	return valid;
}

/* Writes the time offset and TC to the calibration block when they
 * differ from it, a memory error for the source when it fails (0x30c60). */
uint32_t save_calibration_block(uint32_t source)
{
	if (eeprom_calibration_block.time_offset == scpi_value_DIAGnostic_TOFFset &&
	    eeprom_calibration_block.tc_coefficient == scpi_value_DIAGnostic_ROSCillator_TCOefficient)
		return 1;

	calibration_block block;
	memset(&block, RECORDS_ERASED, sizeof block);
	block.time_offset = scpi_value_DIAGnostic_TOFFset;
	block.tc_coefficient = scpi_value_DIAGnostic_ROSCillator_TCOefficient;
	block.checksum = record_checksum(&block, sizeof block);
	if (!eeprom_write((char *)&eeprom_calibration_block, (char *)&block, sizeof block)) {
		status_report_from(RECORDS_STATUS_MEMORY_ERROR, (uint8_t)source);
		return 0;
	}
	return 1;
}

/* Reads DUART channel B's settings from the parameter record, or their
 * defaults; always the SCI's defaults; true when its checksum holds
 * (0x30d2e). */
uint8_t parameter_record_read(void)
{
	bool valid = record_checksum(&eeprom_parameter_record, sizeof eeprom_parameter_record) ==
		     eeprom_parameter_record.checksum;

	if (valid) {
		duartb_settings.baud = eeprom_parameter_record.duartb_baud;
		duartb_settings.parity = eeprom_parameter_record.duartb_parity;
		duartb_settings.xon_xoff = eeprom_parameter_record.duartb_xon_xoff;
		duartb_settings.field_05 = eeprom_parameter_record.duartb_field_05;
		duartb_settings.eight_bits = eeprom_parameter_record.duartb_eight_bits;
		duartb_settings.two_stop_bits = eeprom_parameter_record.duartb_two_stop_bits;
		duartb_settings.echo = eeprom_parameter_record.duartb_echo;
	} else {
		memcpy(&duartb_settings, &duartb_settings_defaults, sizeof duartb_settings);
	}
	memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
	return valid;
}

/* Writes both serial ports' settings to the parameter record, a memory
 * error for the source when it fails (0x30dea). */
bool save_parameter_record(uint8_t source)
{
	parameter_record record;

	memset(&record, RECORDS_ERASED, sizeof record);
	record.duartb_baud = duartb_settings.baud;
	record.duartb_parity = duartb_settings.parity;
	record.duartb_xon_xoff = duartb_settings.xon_xoff;
	record.duartb_field_05 = duartb_settings.field_05;
	record.duartb_eight_bits = duartb_settings.eight_bits;
	record.duartb_two_stop_bits = duartb_settings.two_stop_bits;
	record.duartb_echo = duartb_settings.echo;
	record.sci_baud = sci_settings.baud;
	record.sci_parity = sci_settings.parity;
	record.sci_xon_xoff = sci_settings.xon_xoff;
	record.sci_echo = sci_settings.echo;
	record.checksum = record_checksum(&record, sizeof record);

	bool written = eeprom_write((char *)&eeprom_parameter_record, (char *)&record, sizeof record) != 0;
	if (!written)
		status_report_from(RECORDS_STATUS_MEMORY_ERROR, source);
	return written;
}

/* :SYSTem:COMMunicate:SER1:PRESet's action for channel B: its defaults,
 * taken and saved (0x30ea0). */
void duartb_settings_preset(int32_t index, int32_t source)
{
	(void)index;
	memcpy(&duartb_settings, &duartb_settings_defaults, sizeof duartb_settings);
	duartb_settings_done();
	save_parameter_record((uint8_t)source);
}

/* The same for the SCI (0x30ed4). */
void sci_settings_preset(int32_t index, int32_t source)
{
	(void)index;
	memcpy(&sci_settings, &sci_settings_defaults, sizeof sci_settings);
	sci_settings_done();
	save_parameter_record((uint8_t)source);
}

/* Creates eepx and sends it the lock's one message (0x30f10). */
void eeprom_init(void)
{
	char message[RECORDS_MESSAGE_SIZE];

	uint32_t error = psos_create_x("eepx", 0, 0, &eepx_exchange);
	if ((uint8_t)error != 0) {
		sprintf(message, "Unable to create eepx: %X", error & RECORDS_FIELD_MASK);
		fatal_error(message);
	}
	eepx_token.status = 0;
	eepx_token.exchange = eepx_exchange;
	eepx_token.owner = 0;
	eepx_token.depth = 0;
	error = psos_send_x(eepx_exchange, &eepx_token);
	if ((uint8_t)error != 0) {
		sprintf(message, "Unable to initialize eepx: %X", error & RECORDS_FIELD_MASK);
		fatal_error(message);
	}
}

/* Takes the EEPROM's recursive lock; 0 when held, 1 while a write runs at
 * a raised interrupt mask, or request_x's error (0x30fae). */
char eeprom_lock(void)
{
	/* request_x's second long as eeprom_lock passes it; psos.md does
	 * not give its meaning. */
	static const uint32_t EEPX_REQUEST_WAIT = 0x2800u;
	/* The one error of request_x eeprom_lock returns to its caller
	 * rather than calling fatal_error; not traced. */
	static const uint8_t EEPX_ERROR_RETURNED = 0x81;
	int32_t sr = enter_supervisor();

	if (!kernel_running)
		return 0;
	if ((sr & RECORDS_SR_INTERRUPT_MASK) != 0)
		return eeprom_writing != 0;

	uint32_t task = psos_my_pid();
	if (task == eepx_token.owner) {
		eepx_token.depth++;
		return 0;
	}
	uint32_t error = psos_request_x(eepx_exchange, &eepx_token, 0, EEPX_REQUEST_WAIT);
	uint8_t code = (uint8_t)error;
	if (code == 0) {
		eepx_token.owner = task;
		eepx_token.depth = 0;
		return 0;
	}
	if (code != EEPX_ERROR_RETURNED) {
		char message[RECORDS_MESSAGE_SIZE];
		sprintf(message, "Unable to acquire eepx token: %X", error & RECORDS_FIELD_MASK);
		fatal_error(message);
	}
	return (char)code;
}

/* Releases a level of the EEPROM's lock, sending eepx its message back at
 * the last (0x3105e). */
void eeprom_unlock(void)
{
	int32_t sr = enter_supervisor();

	if (!kernel_running || (sr & RECORDS_SR_INTERRUPT_MASK) != 0)
		return;
	if (psos_my_pid() != eepx_token.owner) {
		task_debug_message("eepx_close and not owner\r\n");
		return;
	}
	if (eepx_token.depth < 0) {
		fatal_error("eepx semaphore count is < 0");
	} else if (eepx_token.depth > 0) {
		eepx_token.depth--;
	} else {
		eepx_token.owner = 0;
		if (eepx_token.exchange != eepx_exchange)
			fatal_error("eepx semaphore has been corrupted");
		uint32_t error = psos_send_x(eepx_exchange, &eepx_token);
		if ((uint8_t)error != 0) {
			char message[RECORDS_MESSAGE_SIZE];
			sprintf(message, "Unable to release eepx token: %X", error & RECORDS_FIELD_MASK);
			fatal_error(message);
		}
	}
}

/* Sets or clears eeprom_writing.  eeprom_lock reads it at a raised
 * interrupt mask, so the store must happen where the image makes it,
 * though the flag is cleared again before anything else here reads it. */
static void records_set_writing(bool writing)
{
	*(volatile uint8_t *)&eeprom_writing = writing;
}

/* Writes a byte to the EEPROM and polls until it reads back, five tries,
 * happening 'I' on a failure (0x31128). */
bool eeprom_write_byte(char *address, char value)
{
	volatile char *cell = address;

	if (eeprom_lock() != 0) {
		status_happening(RECORDS_HAPPENING_EEPROM_WRITE);
		return false;
	}
	records_set_writing(true);
	bool written = false;
	for (int32_t tries = RECORDS_EEPROM_TRIES; !written && tries > 0; tries--) {
		*cell = value;
		for (int16_t polls = RECORDS_EEPROM_POLLS; *cell != value && polls > 0; polls--)
			;
		if (*cell == value)
			written = true;
	}
	records_set_writing(false);
	eeprom_unlock();
	if (!written)
		status_happening(RECORDS_HAPPENING_EEPROM_WRITE);
	return written;
}

/* The EEPROM page an address is in. */
static uint32_t records_eeprom_page(const char *address)
{
	return (uint32_t)address & ~(uint32_t)(RECORDS_EEPROM_PAGE_SIZE - 1);
}

/* Writes bytes within one EEPROM page, its interrupts masked, and checks
 * them, five tries; false for none or a span across pages (0x3119e). */
bool eeprom_write_page(char *destination, char *source, uint16_t length)
{
	if (length == 0)
		return false;
	volatile char *last = destination + (length - 1);
	if (records_eeprom_page(destination) != records_eeprom_page((const char *)last))
		return false;
	if (eeprom_lock() != 0)
		return false;

	bool written = false;
	records_set_writing(true);
	for (int32_t tries = RECORDS_EEPROM_TRIES; !written && tries > 0; tries--) {
		uint16_t sr = enter_supervisor_mode();
		volatile char *to = destination;
		const char *from = source;
		while (to < last)
			*to++ = *from++;
		*to = *from;
		set_interrupt_mask(sr);
		for (int16_t polls = RECORDS_EEPROM_POLLS; *to != *from && polls > 0; polls--)
			;

		written = true;
		to = destination;
		from = source;
		while (written && to <= last)
			if (*to++ != *from++)
				written = false;
	}
	records_set_writing(false);
	eeprom_unlock();
	return written;
}

/* Writes bytes to the EEPROM a page at a time, happening 'I' on a failure
 * (0x3124a). */
uint32_t eeprom_write(char *destination, char *source, uint32_t length)
{
	while ((int32_t)length > 0) {
		uint32_t chunk = RECORDS_EEPROM_PAGE_SIZE - ((uint32_t)destination & (RECORDS_EEPROM_PAGE_SIZE - 1));
		if (length < chunk)
			chunk = length;
		if (!eeprom_write_page(destination, source, (uint16_t)chunk)) {
			status_happening(RECORDS_HAPPENING_EEPROM_WRITE);
			return 0;
		}
		length -= chunk;
		destination += chunk;
		source += chunk;
	}
	return 1;
}

/* A value of so many bits sign-extended to a long (0x3145a). */
uint32_t sign_extend_bits(uint32_t value, uint8_t width)
{
	if (width == 8)
		return (uint32_t)(int32_t)(int8_t)value;
	if (width == 16)
		return (uint32_t)(int32_t)(int16_t)value;
	if (width == 0 || width >= RECORDS_LONG_BITS)
		return value;
	if (value & (uint32_t)1 << (width - 1))
		value |= ~(uint32_t)0 << width;
	return value;
}

/* A log entry's heading, its number and its date and time. */
static void records_log_heading(char *heading, int32_t number, uint32_t date, uint32_t hour, uint32_t minute,
				uint32_t second)
{
	sprintf(heading, "Log %03d:%04d%02d%02d.%02d:%02d:%02d", number, date >> (2 * RECORDS_FIELD_BITS),
		date >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK, date & RECORDS_FIELD_MASK, hour, minute, second);
}

/* An unused log entry's text and heading, either pointer 0 for none. */
static void records_log_unused(char *text, char *heading, int32_t number)
{
	if (text != 0) {
		const char *unused = "Unused";
		while ((*text++ = *unused++) != '\0')
			;
	}
	if (heading != 0)
		sprintf(heading, "Log %03d:00000000.00:00:00", number);
}

/* The event log: RECORDS_LOG_PAGES pages of three entries, 64 bytes each,
 * in the EEPROM from 0x400d40 (log_page_read). */
static const uint8_t RECORDS_LOG_PAGES = 75;
static const int32_t RECORDS_LOG_PAGE_ENTRIES =
	(int32_t)(sizeof log_write_page.entries / sizeof log_write_page.entries[0]);
/* Past this many entries log_init and log_append report happening ' '. */
static const uint8_t RECORDS_LOG_ENTRIES_REPORTED = 180;
static const char RECORDS_HAPPENING_LOG_ENTRIES = ' ';
/* The one format log_read gives a date and a time, not a byte and a
 * long; log_read knows it by its address, so log_first_satellite and
 * log_read share the one copy.  The casts drop const for the exported
 * prototypes, which have none. */
static const char records_first_satellite_format[] = "First satellite tracked at %04d%02d%02d.%02d:%02d:%02d";

/* The sum and its complement of an event log page's entries (0x3137a). */
uint16_t log_page_checksum(log_page *page)
{
	return records_sum_and_complement(records_byte_sum((const uint8_t *)page->entries, sizeof page->entries));
}

/* log_erase under log_mutex (0x315e8). */
int32_t log_erase_locked(uint8_t source)
{
	int32_t error = mutex_lock(&log_mutex);

	if (error != 0)
		return error;
	int32_t status = log_erase(source);
	mutex_unlock(&log_mutex);
	return status;
}

/* log_erase under log_mutex when the log holds count entries (0x3161a). */
int32_t log_erase_checked(int32_t count, uint8_t source)
{
	/* The result for a count not the log's. */
	static const uint8_t LOG_COUNT_DIFFERS = 0xff;
	int32_t error = mutex_lock(&log_mutex);

	if (error != 0)
		return error;
	int32_t status = count == log_entries ? log_erase(source) : LOG_COUNT_DIFFERS;
	mutex_unlock(&log_mutex);
	return status;
}

/* The event log's entries, and through full whether it is full
 * (0x31662). */
int32_t log_entry_total(uint8_t *full)
{
	if (full != 0)
		*full = log_full;
	return log_entries;
}

/* Creates log_mutex and finds the event log's page to write next, the
 * first with a free entry or whose checksum fails (0x3167c). */
int32_t log_init(void)
{
	/* log_page_read's result for a page whose checksum fails. */
	static const uint8_t LOG_PAGE_BAD = 0xff;
	int32_t error = mutex_create("logr", &log_mutex);

	if (error != 0 || (error = mutex_lock(&log_mutex)) != 0)
		return error;
	log_write_page_open = 0;
	log_entries = -1;
	log_page_number = -1;

	int32_t page = 0;
	for (;;) {
		/* log_page_read's prototype takes the page as longs. */
		uint32_t status = log_page_read((uint8_t)page, (uint32_t *)&log_write_page);
		if (status == LOG_PAGE_BAD) {
			log_cursor = log_write_page.entries;
			break;
		}
		if (status != 0)
			return status;
		log_cursor = log_page_free_entry(&log_write_page);
		if (log_cursor != 0 || ++page >= RECORDS_LOG_PAGES)
			break;
	}
	log_write_page_number = (int16_t)page;
	log_full = log_cursor == 0;
	log_entries = (int16_t)(log_write_page_number * RECORDS_LOG_PAGE_ENTRIES);
	if (!log_full)
		log_entries += (int16_t)(log_cursor - log_write_page.entries);
	if (log_entries > RECORDS_LOG_ENTRIES_REPORTED)
		status_happening(RECORDS_HAPPENING_LOG_ENTRIES);
	return mutex_unlock(&log_mutex);
}

/* Event log entry number (from 1, clamped to the log): its text and a
 * "Log NNN:YYYYMMDD.HH:MM:SS" heading, either pointer 0 for none; 0, or
 * mutex_lock's error (0x31762). */
int32_t log_read(int32_t number, char *text, char *heading)
{
	int32_t index = number - 1;
	int32_t error = mutex_lock(&log_mutex);

	if (error != 0)
		return error;
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
	if (page == log_write_page_number && log_write_page_open)
		log_flush();

	uint32_t status = 0;
	if (page != log_page_number) {
		/* log_page_read's prototype takes the page as longs. */
		status = log_page_read((uint8_t)page, (uint32_t *)&log_page_buffer);
		log_page_number = (int16_t)page;
		if (status != 0)
			log_page_clear(&log_page_buffer);
	}

	log_entry *found = &log_page_buffer.entries[entry];
	if (status != 0 || found->format == 0) {
		records_log_unused(text, heading, index + 1);
	} else {
		if (text != 0) {
			if (found->format == records_first_satellite_format) {
				uint32_t date = found->argument_1;
				uint32_t time = found->argument_2;
				sprintf(text, (char *)records_first_satellite_format, date >> (2 * RECORDS_FIELD_BITS),
					date >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK, date & RECORDS_FIELD_MASK,
					time >> (2 * RECORDS_FIELD_BITS) & RECORDS_FIELD_MASK,
					time >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK, time & RECORDS_FIELD_MASK);
			} else {
				/* The image passes the first argument's low byte. */
				sprintf(text, found->format, (int8_t)found->argument_1, found->argument_2);
			}
		}
		if (heading != 0) {
			uint32_t time = found->time;
			records_log_heading(heading, index + 1, found->date, time >> (2 * RECORDS_FIELD_BITS) & RECORDS_FIELD_MASK,
					    time >> RECORDS_FIELD_BITS & RECORDS_FIELD_MASK, time & RECORDS_FIELD_MASK);
		}
	}
	mutex_unlock(&log_mutex);
	return 0;
}

/* Logs when the first satellite was tracked, a date and a time
 * (0x31ba2). */
void log_first_satellite(uint32_t date, uint32_t time)
{
	log_write((char *)records_first_satellite_format, date, time);
}

/* log_event's messages, by code. */
static char *const records_log_events[] = {
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
int32_t log_event(uint8_t code)
{
	/* The result for a code it has no message for. */
	static const uint8_t LOG_EVENT_UNKNOWN = 0xfd;

	if (code >= sizeof records_log_events / sizeof records_log_events[0])
		return LOG_EVENT_UNKNOWN;
	return log_write(records_log_events[code]);
}

/* The first entry of a page with no format, or 0 (0x31bd0). */
log_entry *log_page_free_entry(log_page *page)
{
	for (log_entry *entry = page->entries; entry < page->entries + RECORDS_LOG_PAGE_ENTRIES; entry++)
		if (entry->format == 0)
			return entry;
	return 0;
}

/* Writes the page being filled, and copies it to log_page_buffer when that
 * holds the same page (0x31bf6). */
uint8_t log_flush(void)
{
	log_write_page_open = 0;
	if (log_page_number == log_write_page_number)
		memcpy(&log_page_buffer, &log_write_page, sizeof log_page_buffer);
	/* log_page_write's prototype takes the page as words. */
	return (uint8_t)log_page_write((uint8_t)log_write_page_number, (int16_t *)&log_write_page);
}

/* Adds an entry, its format, arguments, date and time, to the event log
 * and writes its page, moving to the next page after its last entry
 * (0x31c42). */
uint32_t log_append(char *format, int32_t argument_1, int32_t argument_2)
{
	/* The result when the log is full. */
	static const uint8_t LOG_FULL = 0xfc;

	if (log_full)
		return LOG_FULL;
	log_cursor->format = format;
	log_cursor->argument_1 = argument_1;
	log_cursor->argument_2 = argument_2;
	/* log_entry's time is a time_of_day, which smartclock.h does not say. */
	get_date_time(&log_cursor->date, (time_of_day *)&log_cursor->time);

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
	if (log_entries > RECORDS_LOG_ENTRIES_REPORTED)
		status_happening(RECORDS_HAPPENING_LOG_ENTRIES);
	return status;
}

/* The NV register's settings, packed into longs most significant bit
 * first; a value that would cross a long's end starts the next. */
typedef struct records_bit_stream {
	uint32_t *next; /* the long to store or load next */
	uint32_t bits;  /* the long being filled or emptied */
	uint8_t used;   /* its bits filled or emptied */
} records_bit_stream;

/* Stores the long being filled, its bits moved to the top. */
static void records_pack_flush(records_bit_stream *stream)
{
	if (stream->used > 0 && stream->used < RECORDS_LONG_BITS)
		stream->bits <<= RECORDS_LONG_BITS - stream->used;
	*stream->next++ = stream->bits;
	stream->bits = 0;
	stream->used = 0;
}

/* Packs a value's low bits. */
static void records_pack(records_bit_stream *stream, uint32_t value, uint8_t width)
{
	if ((uint8_t)(width + stream->used) > RECORDS_LONG_BITS)
		records_pack_flush(stream);
	stream->bits = (width >= RECORDS_LONG_BITS ? 0 : stream->bits << width) | (value & low_bit_masks[width]);
	stream->used += width;
}

/* Unpacks a value of so many bits.  For 32 the image takes the long as it
 * is; rotating it a whole turn is the same. */
static uint32_t records_unpack(records_bit_stream *stream, uint8_t width)
{
	if ((uint8_t)(width + stream->used) > RECORDS_LONG_BITS) {
		stream->used = 0;
		stream->bits = *stream->next++;
	}
	stream->bits = rotate_left(stream->bits, width);
	stream->used += width;
	return stream->bits & low_bit_masks[width];
}

/* A value's bits in the NV register, from a descriptor's widths. */
static uint8_t records_nv_width(const int8_t *widths, int32_t index)
{
	return (uint8_t)abs(widths[index]);
}

static const uint32_t RECORDS_NV_NUMERIC_PARAMS =
	(uint32_t)(sizeof nv_numeric_params / sizeof nv_numeric_params[0]);
static const uint32_t RECORDS_NV_SETTING_PARAMS =
	(uint32_t)(sizeof nv_setting_params / sizeof nv_setting_params[0]);

/* Erases an NV register slot's tag, if it is not erased (0x327f4). */
void nv_slot_erase(uint8_t slot)
{
	if ((uint8_t)nv_records[slot].tag[0] != RECORDS_ERASED)
		eeprom_write_byte(nv_records[slot].tag, (char)RECORDS_ERASED);
}

/* Erases both NV register slots; the clear_nv word's (0x3281a). */
void clear_nv(void)
{
	nv_slot_erase(0);
	nv_slot_erase(1);
}

/* Loads the settings from slot 0, else slot 1; else takes their defaults,
 * saves them to slot 0 when it is erased or no newer than slot 1, else to
 * slot 1, erasing the other, and reports the configuration lost
 * (0x3282c). */
void nv_restore(void)
{
	/* A status_report code (status_report_messages, 0x2bf30). */
	static const int16_t STATUS_CONFIGURATION_LOST = 0x11;
	/* The source nv_restore gives nv_save and status_report_from. */
	static const uint8_t NV_RESTORE_SOURCE = 2;

	selftest.eeprom = 0;
	nv_slot = 0;
	if (nv_load_slot())
		return;
	nv_slot = 1;
	if (nv_load_slot()) {
		nv_slot_erase(0);
		return;
	}

	selftest.eeprom = 1;
	memcpy(&nv_settings, &nv_settings_defaults, sizeof nv_settings);
	uint32_t sequence_0 = (uint32_t)nv_records[0].sequence;
	uint32_t sequence_1 = (uint32_t)nv_records[1].sequence;
	if (sequence_0 == RECORDS_ERASED_LONG || (sequence_0 <= sequence_1 && sequence_1 != RECORDS_ERASED_LONG))
		nv_slot = 0;
	else
		nv_slot = 1;
	nv_slot_erase(1 - nv_slot);
	nv_save(NV_RESTORE_SOURCE);
	status_report_from(STATUS_CONFIGURATION_LOST, NV_RESTORE_SOURCE);
}

/* The sum and its complement of an NV register record's bytes from its
 * revision on (0x328e0). */
uint16_t nv_checksum(nv_record *record)
{
	return records_sum_and_complement(records_byte_sum((const uint8_t *)record->revision,
							   sizeof *record - __builtin_offsetof(nv_record, revision)));
}

/* Nonzero when two NV register records, taken as longs, differ other than
 * in their sequence numbers (0x3290e). */
uint32_t nv_record_differs(int32_t *first, int32_t *second)
{
	/* nv_record's sequence number, as nv_record_differs takes it, a
	 * long. */
	static const int32_t NV_SEQUENCE_LONG = 1;
	static const int32_t NV_LONGS = (int32_t)(sizeof(nv_record) / sizeof(int32_t));

	if (first[0] != second[0])
		return 1;
	for (int32_t i = NV_SEQUENCE_LONG + 1; i < NV_LONGS; i++)
		if (first[i] != second[i])
			return 1;
	return 0;
}

/* Packs the settings into an NV register record and writes it to slot
 * nv_slot when it differs; a memory error for the source when the write
 * fails (0x3293a). */
char nv_save(uint8_t source)
{
	/* log_event's "EEPROM save failed". */
	static const uint8_t LOG_EEPROM_SAVE_FAILED = 0x18;
	static const uint32_t NV_PACKED_LONGS =
		(uint32_t)(sizeof nv_records[0].packed / sizeof nv_records[0].packed[0]);
	nv_record record;
	records_bit_stream stream = { .next = record.packed };

	strncpy(record.tag, (char *)RECORDS_TAG, sizeof record.tag);
	record.sequence = nv_records[nv_slot].sequence + 1;
	strncpy(record.revision, (char *)RECORDS_FIRMWARE_REVISION, sizeof record.revision);
	for (uint32_t i = 0; i < RECORDS_NV_NUMERIC_PARAMS; i++) {
		scpi_numeric_param *param = nv_numeric_params[i];
		uint32_t *values = param->value;
		for (int32_t v = 0; v <= (int8_t)param->last_index; v++)
			records_pack(&stream, values[v], records_nv_width(param->nv_widths, v));
	}
	for (uint32_t i = 0; i < RECORDS_NV_SETTING_PARAMS; i++) {
		scpi_setting_param *param = nv_setting_params[i];
		for (int32_t v = 0; v <= (int8_t)param->last_index; v++)
			records_pack(&stream, (uint32_t)(int8_t)param->value[v], records_nv_width(param->nv_widths, v));
	}
	if (stream.used != 0)
		records_pack_flush(&stream);

	uint32_t *end = record.packed + NV_PACKED_LONGS;
	if (stream.next >= end && kernel_running)
		task_debug_message("Warning: Entire nv register is being used\r\n");
	while (stream.next < end)
		*stream.next++ = RECORDS_ERASED_LONG;
	record.checksum = nv_checksum(&record);

	/* nv_record_differs takes the records as longs. */
	char saved = 1;
	if (nv_record_differs((int32_t *)&nv_records[nv_slot], (int32_t *)&record))
		saved = eeprom_write(nv_records[nv_slot].tag, record.tag, sizeof record);
	if (!saved) {
		if (kernel_running)
			log_event(LOG_EEPROM_SAVE_FAILED);
		status_report_from(RECORDS_STATUS_MEMORY_ERROR, source);
	}
	return saved;
}

/* 0 when slot nv_slot holds the tag, a good checksum and this firmware's
 * revision, else 1 (0x32b5e). */
uint8_t nv_slot_valid(void)
{
	nv_record *record = &nv_records[nv_slot];

	if (strncmp(record->tag, (char *)RECORDS_TAG, sizeof record->tag) == 0 && nv_checksum(record) == record->checksum &&
	    strncmp(record->revision, (char *)RECORDS_FIRMWARE_REVISION, sizeof record->revision) == 0)
		return 0;
	return 1;
}

/* Loads the settings from slot nv_slot when its record is good; 1 when it
 * is (0x32bc2). */
uint32_t nv_load_slot(void)
{
	if (nv_slot_valid() != 0)
		return 0;

	nv_record record;
	memcpy(&record, &nv_records[nv_slot], sizeof record);
	records_bit_stream stream = { .next = record.packed + 1, .bits = record.packed[0] };

	for (uint32_t i = 0; i < RECORDS_NV_NUMERIC_PARAMS; i++) {
		scpi_numeric_param *param = nv_numeric_params[i];
		uint32_t *values = param->value;
		for (int32_t v = 0; v <= (int8_t)param->last_index; v++) {
			uint8_t width = records_nv_width(param->nv_widths, v);
			uint32_t value = records_unpack(&stream, width);
			values[v] = param->nv_widths[v] < 0 ? sign_extend_bits(value, width) : value;
		}
	}
	nv_numeric_bytes_loaded = (int16_t)((char *)stream.next - (char *)record.packed);
	for (uint32_t i = 0; i < RECORDS_NV_SETTING_PARAMS; i++) {
		scpi_setting_param *param = nv_setting_params[i];
		for (int32_t v = 0; v <= (int8_t)param->last_index; v++)
			param->value[v] = (uint8_t)records_unpack(&stream, records_nv_width(param->nv_widths, v));
	}
	nv_bytes_loaded = (int16_t)((char *)stream.next - (char *)record.packed);
	return 1;
}

/* Sets the *IDN? model to its default (0x32e40). */
void idn_model_default(void)
{
	/* The identification record's default model. */
	static const char DEFAULT_MODEL[] = "Z3805A";

	strncpy(idn_model, (char *)DEFAULT_MODEL, sizeof idn_model);
}

/* Reads the model, serial and suffix from the identification record, or
 * their defaults; true when its checksum holds (0x32e5c). */
uint8_t identification_record_read(void)
{
	/* The identification record's defaults. */
	static const char DEFAULT_SERIAL[] = "0";
	static const char DEFAULT_SUFFIX = 'A';

	if (record_checksum(&eeprom_identification_record, sizeof eeprom_identification_record) !=
	    eeprom_identification_record.checksum) {
		idn_model_default();
		strncpy(idn_serial, (char *)DEFAULT_SERIAL, sizeof idn_serial);
		idn_suffix = DEFAULT_SUFFIX;
		return 0;
	}
	if (strncmp(eeprom_identification_record.tag, (char *)RECORDS_TAG, sizeof eeprom_identification_record.tag) == 0)
		strncpy(idn_model, eeprom_identification_record.model, sizeof idn_model);
	else
		idn_model_default();
	strncpy(idn_serial, eeprom_identification_record.serial, sizeof idn_serial);
	idn_suffix = eeprom_identification_record.suffix;
	return 1;
}

/* Writes the model, serial and suffix to the identification record when
 * they differ from it; 1 when it holds them (0x32f00). */
uint32_t identification_record_write(void)
{
	if (strncmp(eeprom_identification_record.model, idn_model, sizeof idn_model) == 0 &&
	    strncmp(eeprom_identification_record.serial, idn_serial, sizeof idn_serial) == 0 &&
	    strncmp(eeprom_identification_record.tag, (char *)RECORDS_TAG, sizeof eeprom_identification_record.tag) == 0 &&
	    eeprom_identification_record.suffix == idn_suffix)
		return 1;

	identification_record record;
	memset(&record, RECORDS_ERASED, sizeof record);
	strncpy(record.model, idn_model, sizeof record.model);
	strncpy(record.serial, idn_serial, sizeof record.serial);
	record.suffix = idn_suffix;
	strncpy(record.tag, (char *)RECORDS_TAG, sizeof record.tag);
	record.checksum = record_checksum(&record, sizeof record);
	return eeprom_write((char *)&eeprom_identification_record, (char *)&record, sizeof record);
}

/* The sum and its complement of a reset-cause record's bytes from its
 * time on (0x32ffa). */
uint16_t reset_record_checksum(reset_record *record)
{
	return records_sum_and_complement(records_byte_sum((const uint8_t *)&record->time,
							   sizeof *record - __builtin_offsetof(reset_record, time)));
}

/* 1 when a reset-cause record's checksum holds (0x33028). */
uint8_t reset_record_valid(uint8_t index)
{
	return reset_record_checksum(&eeprom_reset_records[index]) == eeprom_reset_records[index].checksum;
}

/* Clears the reset-cause log, zeroing each valid record's checksum, and
 * logs "Log cleared" when every write took; 1 when one failed (0x3305e). */
int32_t log_clear(void)
{
	int32_t failed = 0;
	char zero_checksum[sizeof eeprom_reset_records[0].checksum] = { 0 };

	for (int32_t i = 0; i < RECORDS_RESET_RECORDS; i++)
		if (reset_record_valid((uint8_t)i) &&
		    !eeprom_write((char *)&eeprom_reset_records[i].checksum, zero_checksum, sizeof zero_checksum))
			failed = 1;
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
int32_t log_count(uint8_t *full)
{
	if (full != 0)
		*full = log_entry_count >= RECORDS_RESET_RECORDS;
	return log_entry_count;
}

/* Counts the valid reset-cause records from the first into
 * log_entry_count (0x33114). */
void reset_record_count(void)
{
	log_entry_count = 0;
	for (uint8_t index = 0; index < RECORDS_RESET_RECORDS && reset_record_valid(index); index++)
		log_entry_count++;
}

/* Reset-cause log entry number (from 1, clamped to 1 to 32): its text and
 * a "Log NNN:YYYYMMDD.HH:MM:SS" heading, either pointer 0 for none; 1
 * when the number was clamped or the entry is unused (0x3313c). */
uint32_t log_entry_text(int32_t number, char *text, char *heading)
{
	/* The text each reset-cause record keeps. */
	static const int32_t RESET_TEXT_SIZE = (int32_t)sizeof eeprom_reset_records[0].text;
	uint32_t clamped = 0;
	int32_t index = number - 1;

	if (index >= RECORDS_RESET_RECORDS) {
		index = RECORDS_RESET_RECORDS - 1;
		clamped = 1;
	} else if (index < 0) {
		index = 0;
		clamped = 1;
	}

	if (!reset_record_valid((uint8_t)index)) {
		records_log_unused(text, heading, index + 1);
		return 1;
	}

	reset_record *record = &eeprom_reset_records[index];
	if (text != 0) {
		strncpy(text, record->text, RESET_TEXT_SIZE);
		text[RESET_TEXT_SIZE - 1] = '\0';
	}
	if (heading != 0)
		records_log_heading(heading, index + 1, record->date, record->time.hour, record->time.minute,
				    record->time.second);
	return clamped;
}

/* Writes the next reset-cause record, its date, time and text, unless the
 * log is full; 0 when written (0x33238). */
uint32_t reset_record_write(uint32_t date, time_of_day time, char *text)
{
	uint8_t full;
	uint8_t index = (uint8_t)log_count(&full);

	if (full)
		return 1;
	reset_record record;
	record.date = date;
	record.time = time;
	strncpy(record.text, text, sizeof record.text);
	record.checksum = reset_record_checksum(&record);
	record.sequence = eeprom_reset_records[index].sequence + 1;
	if (!eeprom_write((char *)&eeprom_reset_records[index], (char *)&record, sizeof record))
		return 1;
	log_entry_count++;
	return 0;
}
