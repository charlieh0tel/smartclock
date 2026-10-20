/* The SmartClock firmware's structures, as the code that uses them shows
 * them, for SmartClockSetup.java's types= argument.  A field is named
 * only where docs/firmware/structures.md gives the code that shows what
 * it is; the rest are field_NN, by offset.  Packed, so that the layout
 * is the image's whatever the host compiler.  The 68000's own
 * alignment, 2 bytes, gives the same for all but the GPS task's
 * records, which its decoders lay out byte by byte; check-layout.c
 * checks them with an m68k compiler ("make check-types"). */

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
typedef struct oncore_time_fields {
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
} oncore_time_fields;

/* @@Ab, GMT offset. */
typedef struct oncore_gmt_offset_fields {
	unsigned char sign;     /* 0 positive, 0xff negative */
	unsigned char hours;
	signed char minutes;    /* the printer also takes a negative one as negative */
} oncore_gmt_offset_fields;

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

/* @@Ba, the position/status/data message: one channel's status. */
typedef struct oncore_channel_status {
	unsigned char satellite;
	unsigned char mode;             /* 0 code search .. 8 available for position */
	unsigned char signal_strength;
	unsigned char status;           /* bit 7 used for the fix */
} oncore_channel_status;

/* @@Ba, as its decoder's table (0x50fd2) lays it out: a pad byte
 * before the hour, and another before the DOP type, put the longs and
 * words on even addresses. */
typedef struct oncore_status_fields {
	unsigned char month;
	unsigned char day;
	unsigned short year;
	unsigned char pad_04;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
	unsigned long fraction;         /* ns */
	long latitude;                  /* mas */
	long longitude;
	long height_gps;                /* cm */
	long height_msl;
	unsigned short velocity;        /* cm/s */
	unsigned short heading;         /* tenths of a degree */
	unsigned short dop;             /* tenths */
	unsigned char pad_22;
	unsigned char dop_type;         /* 0 PDOP, 1 HDOP */
	unsigned char visible_count;
	unsigned char tracked_count;
	oncore_channel_status channels[6];
	unsigned char receiver_status;
} oncore_status_fields;

/* @@Bb: one visible satellite, padded by the decoder's table (0x50fd8)
 * from 7 bytes to 10. */
typedef struct oncore_visible_satellite {
	unsigned char satellite;
	unsigned char pad_1;
	short doppler;                  /* Hz */
	unsigned char elevation;        /* degrees */
	unsigned char pad_5;
	unsigned short azimuth;         /* degrees */
	unsigned char health;           /* 0 healthy .. 3 unhealthy and removed */
	unsigned char pad_9;
} oncore_visible_satellite;

/* @@Bb, the visibility message. */
typedef struct oncore_visibility_fields {
	unsigned char count;
	unsigned char pad_1;
	oncore_visible_satellite satellites[12];
} oncore_visibility_fields;

/* @@Bc: one combination of satellites and its xDOP. */
typedef struct oncore_xdop_combination {
	unsigned short xdop;            /* tenths */
	unsigned char satellites[6];
} oncore_xdop_combination;

/* @@Bc, the xDOP table, copied as it arrives (0x50fe2). */
typedef struct oncore_xdop_table_fields {
	unsigned char type;             /* 0 GDOP .. 4 TDOP */
	unsigned char count;
	unsigned char mode;             /* best 4, or 6 or 8 in view */
	oncore_xdop_combination combinations[9];
} oncore_xdop_table_fields;

/* @@Bd: one almanac's status. */
typedef struct oncore_almanac {
	unsigned char valid;
	unsigned char week;
	unsigned char time;
	unsigned char satellite_count;
	unsigned long satellites;       /* one bit a satellite, bit 0 SV 1 */
} oncore_almanac;

/* @@Bd, the almanac status message. */
typedef struct oncore_almanac_status_fields {
	oncore_almanac ram;
	oncore_almanac eeprom;
} oncore_almanac_status_fields;

/* @@Ce: one channel's pseudorange correction, as the decoder's table
 * (0x50ff6) widens it. */
typedef struct oncore_correction_channel {
	unsigned char satellite;
	unsigned char pad_1;
	long range_correction;          /* cm */
	short rate_correction;          /* mm/s */
	unsigned char issue_of_data;
	unsigned char pad_9;
} oncore_correction_channel;

/* @@Ce, the pseudorange correction message. */
typedef struct oncore_corrections_fields {
	long time;                      /* tenths of a second of the week */
	oncore_correction_channel channels[6];
} oncore_corrections_fields;

/* @@Bn: one channel's time estimate. */
typedef struct oncore_raim_channel {
	unsigned char satellite;
	unsigned char pad_1;
	unsigned long time_estimate;    /* ns */
} oncore_raim_channel;

/* @@Bn, the Time RAIM setup and status message, as the decoder's table
 * (0x50fe6) lays it out. */
typedef struct oncore_raim_fields {
	unsigned char rate;
	unsigned char raim_on;
	unsigned char pad_02;
	unsigned short alarm_limit;     /* 100 ns */
	unsigned char pps_mode;
	unsigned char pad_06;
	long pps_rate;
	unsigned char next_month;       /* the time to the next pulse */
	unsigned char next_day;
	unsigned short next_year;
	unsigned char pad_0f;
	unsigned char next_hour;
	unsigned char next_minute;
	unsigned char next_second;
	unsigned char pulse_status;
	unsigned char pulse_sync;       /* 0 UTC, 1 GPS */
	unsigned char solution_status;  /* 0 OK, 1 alarm, 2 unknown */
	unsigned char raim_status;
	unsigned short sigma;           /* ns */
	signed char sawtooth;           /* ns, of the next pulse */
	unsigned char pad_1a;
	oncore_raim_channel channels[6];
} oncore_raim_fields;

/* The GPS task's state: what its decoders store of each message, in
 * the order of the message table, and its own state after.  Cleared as
 * one, memset(&gps, 0, 0x55a), and addressed through a base register
 * by 27 functions.  Members are named by their message, field_NNN by
 * offset where none fills them. */
typedef struct gps_state {
	unsigned char field_000;
	oncore_time_fields time;                     /* @@Aa */
	oncore_gmt_offset_fields gmt_offset;         /* @@Ab */
	unsigned char field_007;
	oncore_date_fields date;                     /* @@Ac */
	oncore_position_fields position;             /* @@Ad, @@Ae, @@Af */
	unsigned char mask_angle;                    /* @@Ag */
	unsigned char satellite_select;              /* @@Ah */
	unsigned char manual_satellites[6];          /* @@Ai, by channel */
	unsigned char xdop_type;                     /* @@Aj */
	unsigned char field_025;
	unsigned short xdop_hysteresis;              /* @@Ak */
	unsigned short xdop_threshold;               /* @@Al, 3D to 2D */
	unsigned short hdop_threshold;               /* @@AC, 2D to 0D */
	unsigned long ignored_satellites;            /* @@Am */
	unsigned char field_030[0x8e - 0x30];
	unsigned char almanac_update;                /* @@An */
	unsigned char field_08f[0xa4 - 0x8f];
	unsigned char ionospheric_correction;        /* @@Aq */
	unsigned char fix_algorithm;                 /* @@Ar */
	oncore_hold_position_fields hold_position;   /* @@As */
	unsigned char field_0b2[2];
	unsigned char position_hold;                 /* @@At */
	unsigned char field_0b5[0xbb - 0xb5];
	unsigned char altitude_hold;                 /* @@Av */
	unsigned char utc_time;                      /* @@Aw: 0 GPS, 1 UTC */
	unsigned char field_0bd;
	short epoch_offset;                          /* @@Ax, ms */
	long pps_offset;                             /* @@Ay */
	long pps_cable_delay;                        /* @@Az */
	unsigned char field_0c8;
	unsigned char application_type;              /* @@AB */
	unsigned char position_fix_threshold;        /* @@AD, s */
	unsigned char differential_output_threshold; /* @@AD, s */
	unsigned char field_0cc[2];
	oncore_status_fields status;                 /* @@Ba */
	unsigned char field_10d[3];
	unsigned char visibility_received;           /* set by @@Bb's decoder */
	unsigned char field_111;
	oncore_visibility_fields visibility;         /* @@Bb */
	unsigned char field_18c;
	oncore_xdop_table_fields xdop_table;         /* @@Bc */
	unsigned char field_1d8[2];
	oncore_almanac_status_fields almanac;        /* @@Bd */
	unsigned char field_1ea[0x2a8 - 0x1ea];
	oncore_corrections_fields corrections;       /* @@Ce */
	unsigned char field_2e8[0x2fb - 0x2e8];
	oncore_raim_fields raim;                     /* @@Bn */
	unsigned char leap_second_pending;           /* @@Bj */
	unsigned char field_33b;
	unsigned short self_test;                    /* @@Ca, failed tests */
	unsigned char field_33e[0x344 - 0x33e];
	oncore_extension_fields extension;           /* @@Bk */
	unsigned char field_382[0x3ec - 0x382];
	unsigned char fix_mode;                      /* @@Cg: 0 idle, 1 fix */
	unsigned char field_3ed;
	unsigned char receiver_id_received;          /* set by @@Cj's decoder */
	char receiver_id[277];                       /* @@Cj, ten NUL-terminated lines */
	unsigned char field_504[2];
	unsigned long average_count;                 /* positions averaged */
	double average_latitude;                     /* of @@Ba's, while surveying */
	double average_longitude;
	double average_height_gps;
	double average_height_msl;
	unsigned char field_52a[0x55a - 0x52a];
} gps_state;

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, memset(&pll, 0, 0x3d6). */
typedef struct pll_state {
	unsigned char stage;                         /* 1 powerup .. 6 normal pll */
	unsigned char field_001[0x12 - 0x01];
	unsigned char manual;                        /* +0x12 */
	unsigned char field_013[2];
	unsigned char recovery_state;                /* +0x15 */
	unsigned char field_016;
	unsigned char fine_state;                    /* +0x17 */
	unsigned char field_018[0x32 - 0x18];
	float efc;                                   /* pr_efc's efc */
	float tfom;                                  /* pll_rep's tfom, s */
	unsigned char field_03a[0x4a - 0x3a];
	efc_ring efc_history;
	unsigned char field_3b0[0x3d6 - 0x3b0];
} pll_state;
