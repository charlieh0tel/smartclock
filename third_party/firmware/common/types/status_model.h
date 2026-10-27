/* The status model, as the images' smartclock.h declare them (each includes
 * this header where they stand): docs/firmware/rewrite.md, "Shared C". Where a
 * comment cites code, it is the Z3805A 3543B's, which the 58503A's copy of the
 * comment came from. */

/* The SCPI status system: status_create builds it, status_object points
 * at it. */
typedef struct status_model {
  int16_t register_count; /* +0x00: 7 */
  scpi_port* output; /* +0x02: set for the top register; the port whose status
                        byte it keeps (status_condition_set,
                        status_condition_set+0x42) */
  status_error_queue* queue;    /* +0x06: built with room for 30 */
  status_condition* conditions; /* +0x0a */
  int32_t
      field_0e; /* +0x0e: status_create's last argument, 0; nothing reads it */
  status_register* registers[7]; /* +0x12 */
} status_model;
