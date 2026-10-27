/* The SCPI error queue, as the images' smartclock.h declare them (each includes
 * this header where they stand): docs/firmware/rewrite.md, "Shared C". Where a
 * comment cites code, it is the Z3805A 3543B's, which the 58503A's copy of the
 * comment came from. */

/* An entry of the error queue: an error code and what status_set_bit is
 * given with it, status_report's message (error_queue_push, error_queue_push).
 */
typedef struct status_error_entry {
  int16_t code;   /* +0x00 */
  uint32_t value; /* +0x02 */
} status_error_entry;

/* The error queue, which error_queue_create allocates and
 * status_model.queue points at: a ring of entries, one more than it
 * holds, so that head equal to tail is empty. */
typedef struct status_error_queue {
  int16_t head;                 /* +0x00: the newest entry */
  int16_t tail;                 /* +0x02: the entry before the oldest */
  struct status_model* status;  /* +0x04: the model it reports into */
  int16_t size;                 /* +0x08: entries, the capacity plus one */
  status_error_entry entries[]; /* +0x0a */
} status_error_queue;
