/* The printf family's types, as the images' smartclock.h declare them
 * (each includes this header): docs/firmware/rewrite.md, "Shared C". */

/* What sprintf and vsprintf hand doprnt to pass to sprintf_put, on
 * their own stacks: sprintf_put writes at cursor and advances it.
 * Nothing reads unused. */
typedef struct sprintf_output {
  uint32_t unused;
  char* cursor;
} sprintf_output;

/* What doprnt writes its output through, as fwrite is called: the
 * text, the size of an element, the count, and the output; sprintf's
 * is sprintf_put.  The count written comes back. */
typedef int32_t sprintf_put_function(char* text, int32_t size, int32_t count,
                                     sprintf_output* output);
