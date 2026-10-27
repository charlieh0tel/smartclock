/* scpi_error_message and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* An error code's message, one of scpi_error_messages, which
 * error_message_lookup walks to the entry whose code is -1. */
typedef struct scpi_error_message {
  int16_t code; /* +0x00 */
  char* text;   /* +0x02 */
} scpi_error_message;
