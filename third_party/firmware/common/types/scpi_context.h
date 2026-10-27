/* The SCPI engine's context, as the images' smartclock.h declare them (each
 * includes this header where they stand): docs/firmware/rewrite.md, "Shared C".
 * Where a comment cites code, it is the Z3805A 3543B's, which the
 * 58503A's copy of the comment came from. */

/* The parser's context, which scpi_context_init fills for
 * scpi_interpret. */
typedef struct scpi_context {
  struct scpi_port* input; /* +0x00 */
  uint16_t word;     /* +0x04: 0 from scpi_interpret; the block and expression
                        converters' */
  scpi_path* path;   /* +0x06 */
  scpi_call* call;   /* +0x0a */
  scpi_arena* arena; /* +0x0e */
  scpi_child_list* main_tree;   /* +0x12: the main tree's root list */
  scpi_child_list* common_list; /* +0x16: the common commands' list */
  uint8_t field_1a[4];
  uint32_t field_1e; /* +0x1e: what scpi_interpret's frame holds there, unset;
                        the block converter's last */
} scpi_context;
