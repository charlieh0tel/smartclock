/* The mutex, the spool task's routine and the clock task's entry, as the
 * images' smartclock.h declare them (each includes this header where they
 * stand): docs/firmware/rewrite.md, "Shared C". Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came from. */

/* A mutex: an exchange holding one message, its token, which
 * mutex_acquire takes with request_x unless the calling task
 * owns it already, and the owner and its nested holds.  mutex_clear
 * clears all 0x18 bytes; mutex_init creates the
 * exchange and sends the token. */
typedef struct mutex {
  uint32_t field_00;
  uint32_t exchange; /* +0x04: 0 until mutex_init; mutex_acquire fails 1 */
  uint32_t owner;    /* +0x08: the holding task's id, 0 when free */
  int32_t holds;     /* +0x0c: the owner's holds past its first, each 0x80 */
  uint32_t field_10;
  uint32_t field_14;
} mutex;

/* A routine the spool task runs for a caller, with its three arguments. */
typedef void spoo_function(uint32_t a, uint32_t b, uint32_t c);

/* A clock (klok task) entry, which print_clk_list prints: when it fires,
 * its period, the function it calls and its event class.  A date is
 * year << 16 | month << 8 | day, a time hour << 16 | minute << 8 |
 * second.  0x1a bytes; a pool of five. */
typedef struct klok_entry {
  struct klok_entry* next;
  uint32_t time; /* at */
  uint32_t date;
  uint32_t period_time; /* period */
  uint32_t period_date;
  spoo_function* function; /* function address: spoo_run runs it */
  int16_t event_class;     /* event class */
} klok_entry;
