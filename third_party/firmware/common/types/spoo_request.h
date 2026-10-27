/* spoo_request and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A request on the spoo exchange, which spoo_run builds on its stack and
 * spoo_task receives at spoo_message: a function and its three arguments. */
typedef struct spoo_request {
  uint32_t field_00;       /* +0x00: spoo_run does not set it */
  uint32_t field_04;       /* +0x04: spoo_run clears it */
  spoo_function* function; /* +0x08 */
  uint32_t arguments[3];   /* +0x0c */
} spoo_request;
