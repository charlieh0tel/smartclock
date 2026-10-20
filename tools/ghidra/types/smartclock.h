/* The SmartClock firmware's structures, as the code that uses them shows
 * them, for SmartClockSetup.java's types= argument.  A field is named
 * only where docs/firmware/structures.md gives the code that shows what
 * it is; the rest are field_NN, by offset.  Packed: these are byte
 * layouts read from the image, not what a compiler would lay out. */

#pragma pack(1)

/* A numeric SCPI parameter's descriptor, in ROM; its small tables sit
 * just before it.  0x2c bytes. */
typedef struct scpi_numeric_param {
	short field_00;
	void *table_02;
	void *minimum;          /* +0x06: the least value accepted */
	void *maximum;          /* +0x0a: the greatest */
	void *convert;          /* +0x0e: called by scpi_numeric_get */
	void *field_12;
	void *value;            /* +0x16: the current value, in RAM */
	void *field_1a;
	short field_1e;
	short field_20;
	void *field_22;
	char is_integer;        /* +0x26: the value is an int, not a float */
	char field_27;
	void *table_28;
} scpi_numeric_param;

/* One status register of the SCPI status tree.  16 bytes. */
typedef struct status_register {
	short field_00;
	short field_02;
	short field_04;
	short field_06;
	short field_08;
	struct status_register *parent; /* +0x0a: the register it reports into */
	short parent_bit;               /* +0x0e: at this bit */
} status_register;

/* An entry of the status conditions table: which register and bit a
 * condition sets, and how.  6 bytes. */
typedef struct status_condition {
	short kind;
	short register_index;
	short bit;
} status_condition;

/* The SCPI status system: status_create builds it, status_object points
 * at it. */
typedef struct status_model {
	short register_count;          /* +0x00: 7 */
	void *output;                  /* +0x02: set for the top register */
	void *queue;                   /* +0x06: built with room for 30 */
	status_condition *conditions;  /* +0x0a */
	long field_0e;
	status_register *registers[7]; /* +0x12 */
} status_model;

/* The GPS engine's data, as its message decoders store it: each field
 * is a field of the message, in the message's order, as the VP Oncore
 * reference (third_party/motorola/VPCommands.pdf) gives it. */

/* @@Aa, time of day. */
typedef struct oncore_time_of_day {
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
} oncore_time_of_day;

/* @@Ab, GMT offset. */
typedef struct oncore_gmt_offset {
	unsigned char sign;     /* 0 positive, 0xff negative */
	unsigned char hours;
	signed char minutes;    /* the printer also takes a negative one as negative */
} oncore_gmt_offset;

/* @@Ac, date. */
typedef struct oncore_date_fields {
	unsigned char month;
	unsigned char day;
	unsigned short year;
} oncore_date_fields;

/* @@Ad, @@Ae and @@Af, the current position: angles in milliarcseconds,
 * heights in cm. */
typedef struct oncore_position_fields {
	long latitude;
	long longitude;
	long height_gps;        /* above the ellipsoid */
	long height_msl;        /* above mean sea level */
} oncore_position_fields;

/* @@As, the position-hold position, as above. */
typedef struct oncore_hold_position_fields {
	long latitude;
	long longitude;
	long height;
} oncore_hold_position_fields;

/* @@Bk, the position/status/data extension.  DOPs, angles, velocities
 * and the matrix in tenths. */
typedef struct oncore_extension_fields {
	unsigned short gdop;
	unsigned short pdop;
	unsigned short hdop;
	unsigned short vdop;
	unsigned short tdop;
	short magnetic_variation;      /* degrees, east positive */
	short velocity_north;          /* m/s */
	short velocity_east;
	short velocity_up;
	unsigned short correction_age; /* s, of differential corrections */
	long ecef_x;                   /* cm */
	long ecef_y;
	long ecef_z;
	unsigned char ura[6];          /* per channel */
	short least_squares_inverse[10]; /* A-1, upper triangle by rows */
	short clock_bias;              /* m */
	unsigned short oscillator_offset; /* m/s */
} oncore_extension_fields;

/* The EFC history: a ring of 64 averages of the EFC, one each 2700
 * samples, which the holdover prediction draws on.  Its routine's debug
 * message names its arrays: "e_avg= time= weight= log= tfom=".
 * Cleared as a whole, 0x366 bytes. */
typedef struct efc_ring {
	unsigned char start_ptr;  /* +0x000: the oldest entry */
	unsigned char stop_ptr;   /* +0x001: the next to fill */
	unsigned char field_002;
	unsigned char field_003;
	unsigned short sample_count; /* +0x004: samples toward this entry, to 2700 */
	short field_006;
	unsigned short summed_count; /* +0x008: of those, summed */
	float averages[64];       /* +0x00a: e_avg, efc_sum / summed_count */
	unsigned char weights[64]; /* +0x10a: weight, 1 to 3 */
	float times[64];          /* +0x14a: time */
	float logs[64];           /* +0x24a: log */
	float efc_sum;            /* +0x34a: the EFC, less its temperature correction, summed */
	long field_34e;
	float drift;              /* +0x352: pll_rep's current drift, times 5.4e-8 per day */
	long field_356;
	float field_35a;
	long field_35e;
	long field_362;
} efc_ring;
