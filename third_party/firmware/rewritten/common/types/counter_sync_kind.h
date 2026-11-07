/* counter_sync_kind and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* sync_request's kind, counter_configuration_fields.sync_kind: a step
 * against a positive TI, against a negative one, and a sync
 * (pll_stages.c's steps; sync_imm, diag_sync_imm diag_sync_imm, asks for a
 * sync). */
typedef enum __attribute__((packed)) counter_sync_kind {
  COUNTER_SYNC_POSITIVE = 1,
  COUNTER_SYNC_NEGATIVE = 2,
  COUNTER_SYNC_FULL = 3,
} counter_sync_kind;

/* The fault the console's crash word raises, its argument, by its jump
 * table at diag_crash+0x1e (diag_crash, diag_crash); any other does nothing. */
typedef enum __attribute__((packed)) console_crash_kind {
  CONSOLE_CRASH_BUS_ERROR = 0,     /* a long written to 0x700000, unmapped */
  CONSOLE_CRASH_ADDRESS_ERROR = 1, /* a long written to an odd address */
  CONSOLE_CRASH_ZERO_DIVIDE = 2,   /* divul by zero */
  CONSOLE_CRASH_FLOAT_DIVIDE = 3,  /* a double divided by zero */
  CONSOLE_CRASH_FATAL_ERROR = 4,   /* fatal_error */
} console_crash_kind;
