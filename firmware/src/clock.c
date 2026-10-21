/* The clock's conversions between a time of day and seconds, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C").  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  A time is a long, hour << 16 |
 * minute << 8 | second (docs/firmware/structures.md, "A date and
 * time"). */

enum {
	SECONDS_PER_MINUTE = 60,
	MINUTES_PER_HOUR = 60,
	TIME_FIELD_BITS = 8,
	TIME_FIELD_MASK = 0xff,
};

/* A time of day as seconds since midnight (0x1111e). */
int hms_to_seconds(uint32_t time)
{
	uint32_t hour = time >> (2 * TIME_FIELD_BITS) & TIME_FIELD_MASK;
	uint32_t minute = time >> TIME_FIELD_BITS & TIME_FIELD_MASK;
	uint32_t second = time & TIME_FIELD_MASK;

	return (hour * MINUTES_PER_HOUR + minute) * SECONDS_PER_MINUTE + second;
}

/* The same, a second copy in the image (0x11152). */
int time_to_seconds(uint32_t time)
{
	return hms_to_seconds(time);
}

/* The same, a third copy, which nothing calls (0x10e3a). */
int time_packed_to_seconds(uint32_t time)
{
	return hms_to_seconds(time);
}
