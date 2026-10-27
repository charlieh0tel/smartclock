/* char_slot and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A char argument through a function pointer.  This compiler pushes a
 * char in a 2-byte slot, its value at the slot's start, 4 bytes above
 * the stack pointer at entry (docs/firmware/README.md, "The calling
 * convention"), where Ghidra's convention for a pointer's prototype, and
 * gcc, put a char at the end of a 4-byte slot; custom storage, which
 * places a function's own char there, a pointer's prototype cannot
 * carry.  A 4-byte structure's first byte is at the slot's start in
 * both; gcc pads a 2-byte one to the slot's end. */
typedef struct char_slot {
  uint8_t value;    /* +0x00: the char */
  uint8_t field_01; /* unread: the rest of the original's 2-byte slot */
  uint8_t next;     /* +0x02: unread: the next 2-byte slot's char, a second
                       argument the callee ignores; the self-test callers push
                       it with move.b (selftest_run_all+0xa,
                       selftest_run_all_counted+0x10, selftest_execute+0x24,
                       selftest_execute+0x2a) */
  uint8_t field_03; /* unread: the rest of that slot */
} char_slot;
