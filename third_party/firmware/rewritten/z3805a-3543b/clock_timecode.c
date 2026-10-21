/* The time code and the ES message, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The time code").  At each 1PPS
 * timecode_update builds the :PTIMe:TCODe? message (097-z3801-01,
 * "Detailed Description of the Two Time Code Formats") in tcode_message
 * and, while es_enabled, es_message; each goes out a delay after the
 * 1PPS's capture, which an output compare counts off. */

enum {
	/* tcode_format: T1, the GPS seconds; T2 with a test prefix; T2. */
	CLOCK_TCODE_T1 = 0,
	CLOCK_TCODE_T2_TEST = 1,
	CLOCK_TCODE_T2 = 2,
	/* tfom_code's t, 0 to 9: tfom_table_index's exponent moved by 9. */
	CLOCK_TCODE_TFOM_BIAS = 9,
	CLOCK_TCODE_TFOM_MAX = 9,
	/* The delay after the 1PPS capture, in GPT ticks, at which
	 * timecode_update and es_build send their messages (0x13eca,
	 * 0x13c3c). */
	CLOCK_TCODE_DELAY = 0x28f5,
	/* The longest minute; a leap second removed makes it 59. */
	CLOCK_TCODE_MINUTE = 60,
	CLOCK_TCODE_MINUTE_SHORT = 59,
	CLOCK_TCODE_CHECKSUM_SIZE = 3,

	/* The ES message's status bits. */
	CLOCK_ES_TIME_VALID = 0x01,
	CLOCK_ES_LEAP_ADD = 0x02,
	CLOCK_ES_LEAP_TODAY = 0x04,
	CLOCK_ES_HOLDOVER = 0x10,
	/* The ES message as digits, one a byte, and as ASCII. */
	CLOCK_ES_DIGITS_LENGTH = 16,
	CLOCK_ES_ASCII_LENGTH = 18,

	CLOCK_TCODE_TIMER_WRAP = 0x10000,
};

/* The digits of a byte's tens and ones. */
static uint8_t clock_tens(uint32_t value)
{
	return value / 10;
}

static uint8_t clock_ones(uint32_t value)
{
	return value - clock_tens(value) * 10;
}

/* The time code's cc: its characters' sum (0x139e4). */
char tcode_checksum(char *text)
{
	char sum = 0;

	for (; *text != 0; text++)
		sum += *text;
	return sum;
}

/* Asks for a time code of a format, 0 to 2, unless one is pending: the
 * format is kept and tcode_pending set once drtb_release_for_timecode
 * frees the port; its result, 0 for a request refused (0x139f6). */
char tcode_request(uint8_t format)
{
	if (tcode_pending != 0 || format > CLOCK_TCODE_T2)
		return 0;
	tcode_format = format;
	char released = drtb_release_for_timecode();

	if (released != 0)
		tcode_pending = 1;
	return released;
}

/* The time code's t, from pll.tfom's bits: its decimal exponent moved by 9
 * and kept to 0 to 9 (0x13a48). */
int16_t tfom_code(uint32_t tfom)
{
	int16_t code = tfom_table_index(tfom) + CLOCK_TCODE_TFOM_BIAS;

	if (code < 0)
		return 0;
	if (code > CLOCK_TCODE_TFOM_MAX)
		return CLOCK_TCODE_TFOM_MAX;
	return code;
}

/* Builds es_message for a date and time and schedules it: the year, day of
 * the year, time and GPS-UTC offset, as one digit a byte or, while
 * es_ascii, as text; the status bits; and status_summary.  The offset's
 * tens are taken of it as unsigned, as the image takes them (0x13a6a). */
void es_build(uint32_t date, uint32_t time)
{
	int16_t offset;

	es_length = 0;
	if (!get_gps_utc_offset(&offset))
		offset = 0;
	uint16_t day = day_of_year(date);
	uint16_t status = time_valid() == 0 ? CLOCK_ES_TIME_VALID : 0;

	if (nv_settings.leap_pending && nv_settings.leap_date == date) {
		status |= CLOCK_ES_LEAP_TODAY;
		if (nv_settings.leap_minute_length > CLOCK_TCODE_MINUTE)
			status |= CLOCK_ES_LEAP_ADD;
	}
	if (pll.stage == PLL_STAGE_HOLDOVER || pll.stage == PLL_STAGE_HOLDOVER_RECOVERY)
		status |= CLOCK_ES_HOLDOVER;
	uint8_t hour = time >> 16;
	uint8_t minute = time >> 8;
	uint8_t second = time;
	uint16_t year = date >> 16;

	if (es_ascii == 0) {
		uint32_t day_ones = day % 100;
		uint8_t offset_tens = (uint32_t)(int32_t)offset / 10;

		es_message[0] = clock_tens(year % 100);
		es_message[1] = clock_ones(year % 100);
		es_message[2] = day / 100;
		es_message[3] = clock_tens(day_ones);
		es_message[4] = clock_ones(day_ones);
		es_message[5] = clock_tens(hour);
		es_message[6] = clock_ones(hour);
		es_message[7] = clock_tens(minute);
		es_message[8] = clock_ones(minute);
		es_message[9] = clock_tens(second);
		es_message[10] = clock_ones(second);
		es_message[11] = offset_tens;
		es_message[12] = (uint8_t)offset - offset_tens * 10;
		es_message[13] = status;
		es_message[14] = status_summary;
		es_message[15] = '\r';
		es_length = CLOCK_ES_DIGITS_LENGTH;
	} else {
		sprintf(es_message, "%02d%03d%02d%02d%02d%02d%02X%02X\r", (uint32_t)year % 100,
			(uint32_t)day, (uint32_t)hour, (uint32_t)minute, (uint32_t)second,
			(int32_t)offset, (uint32_t)status, (uint32_t)status_summary);
		es_length = CLOCK_ES_ASCII_LENGTH;
	}
	es_schedule(CLOCK_TCODE_DELAY);
}

/* The time code's length, and its checksum appended. */
static uint8_t clock_tcode_finish(void)
{
	char checksum[CLOCK_TCODE_CHECKSUM_SIZE];
	uint8_t length = 0;

	sprintf(checksum, "%02X", (uint32_t)(uint8_t)tcode_checksum(tcode_message));
	strcat(tcode_message, checksum);
	while (tcode_message[length] != 0)
		length++;
	return length;
}

/* At each 1PPS: the ES message on even seconds while es_enabled, and the
 * time code asked for, T1 of the GPS seconds at the next 1PPS or T2 of the
 * clock's next second, with its t, f, l, r and v and checksum (0x13c50). */
void timecode_update(void)
{
	uint32_t date = get_clock_date();
	uint32_t time = get_clock_time();

	if (es_enabled != 0 && (time & 1) == 0)
		es_build(date, time);
	if (tcode_pending == 0)
		return;
	tcode_length = 0;
	/* tfom_code takes the float's bits. */
	union {
		float value;
		uint32_t bits;
	} tfom = { .value = pll.tfom };
	uint8_t tfom_digit = tfom_code(tfom.bits);
	uint8_t ffom;

	get_ffom(&ffom);
	char leap = '0';

	if (nv_settings.leap_pending)
		leap = nv_settings.leap_minute_length == CLOCK_TCODE_MINUTE_SHORT ? '-' : '+';
	char request = status_request_service ? '1' : '0';
	char valid = time_valid() ? '0' : '1';

	if (tcode_format == CLOCK_TCODE_T1) {
		uint32_t gps_date = get_gps_date();
		uint32_t gps_time = get_gps_time();

		sprintf(tcode_message, "T1#H%08X%01d%01d%1c%1c%1c", gps_seconds(gps_date, gps_time) + 1,
			(uint32_t)tfom_digit, (uint32_t)ffom, (int32_t)leap, (int32_t)request,
			(int32_t)valid);
		tcode_length = clock_tcode_finish();
	} else if (tcode_format == CLOCK_TCODE_T2_TEST || tcode_format == CLOCK_TCODE_T2) {
		uint32_t next_date = get_clock_date();
		time_of_day next = clock_time_of(get_clock_time());

		time_next_second(&next_date, &next);
		uint32_t year = next_date >> 16;
		uint32_t month = (uint8_t)(next_date >> 8);
		uint32_t day = (uint8_t)next_date;

		if (tcode_format == CLOCK_TCODE_T2_TEST)
			sprintf(tcode_message, "rtcm? %u,T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c",
				0xffffffff, year, month, day, (uint32_t)next.hour,
				(uint32_t)next.minute, (uint32_t)next.second, (uint32_t)tfom_digit,
				(uint32_t)ffom, (int32_t)leap, (int32_t)request, (int32_t)valid);
		else
			sprintf(tcode_message, "T2%04d%02d%02d%02d%02d%02d%01d%01d%1c%1c%1c", year,
				month, day, (uint32_t)next.hour, (uint32_t)next.minute,
				(uint32_t)next.second, (uint32_t)tfom_digit, (uint32_t)ffom,
				(int32_t)leap, (int32_t)request, (int32_t)valid);
		tcode_length = clock_tcode_finish();
	} else {
		tcode_length = 0;
	}
	if (tcode_length != 0)
		timecode_schedule(CLOCK_TCODE_DELAY);
	tcode_pending = 0;
}

/* Clears the time code's and the ES message's state (0x13ee4). */
void timecode_init(void)
{
	tcode_length = 0;
	tcode_pending = 0;
	es_length = 0;
	es_ascii = 0;
	es_enabled = 0;
}

/* Whether an output compare at a delay after the 1PPS capture, input
 * capture 1, is already past: the free-running count, taken past the
 * capture across its wrap, has reached it. */
static bool clock_compare_passed(uint32_t compare)
{
	uint32_t now = gpt.tcnt;

	if (now < gpt.tic[0])
		now += CLOCK_TCODE_TIMER_WRAP;
	return compare <= now;
}

/* Sets output compare 3 to send the time code a delay, in GPT ticks, after
 * the 1PPS capture: the delay's high word the compare's wraps to let pass
 * (timecode_compare_wraps), which primary_user_86 counts down.  A compare
 * already past sends it at once, or takes one wrap off (0x13fa6). */
void timecode_schedule(uint32_t delay)
{
	timecode_compare_wraps = delay >> 16;
	uint32_t compare = gpt.tic[0] + (delay & 0xffff);

	gpt.toc[2] = compare;
	gpt.tflg &= ~TFLG_OCF3;
	if (clock_compare_passed(compare)) {
		if (timecode_compare_wraps == 0) {
			if (tcode_length != 0)
				drtb_send_async(tcode_message, tcode_length);
			return;
		}
		timecode_compare_wraps--;
		gpt.tflg &= ~TFLG_OCF3;
	}
	gpt.tmsk |= TMSK_OCI3;
}

/* The same for the ES message, output compare 4, es_compare_wraps and
 * primary_user_87 (0x1406a). */
void es_schedule(uint32_t delay)
{
	es_compare_wraps = delay >> 16;
	uint32_t compare = gpt.tic[0] + (delay & 0xffff);

	gpt.toc[3] = compare;
	gpt.tflg &= ~TFLG_OCF4;
	if (clock_compare_passed(compare)) {
		if (es_compare_wraps == 0) {
			if (es_length != 0)
				sci_send_async(es_message, es_length);
			return;
		}
		es_compare_wraps--;
		gpt.tflg &= ~TFLG_OCF4;
	}
	gpt.tmsk |= TMSK_OCI4;
}
