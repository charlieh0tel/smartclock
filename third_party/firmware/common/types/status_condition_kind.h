/* status_condition_kind and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* How a status condition changes its register (status_condition.kind),
 * by status_set_bit's jump table at status_set_bit+0x52. */
typedef enum __attribute__((packed)) status_condition_kind {
  STATUS_KIND_EVENT_SET = 0, /* sets the bit in the event word */
  STATUS_KIND_SET = 1,       /* sets the bit in the condition word */
  STATUS_KIND_CLEAR = 2,     /* clears it */
  STATUS_KIND_ERROR = 3,     /* queues register_index as an error code */
  STATUS_KIND_NOTHING = 4,
} status_condition_kind;
