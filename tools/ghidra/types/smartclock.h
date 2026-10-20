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
