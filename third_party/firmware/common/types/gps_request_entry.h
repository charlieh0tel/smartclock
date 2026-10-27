/* gps_request_entry and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A GPS request: the message pll_scpi_send_to_loop, gpsx_send and
 * gps_request_post build in 26 bytes of their frames and send
 * gpsx_exchange, which gpsm_task receives into 26 bytes, keeps in its
 * ring, and gps_count_response sends back to plla_exchange with the
 * result, for pllp_task and gps_selftest_run.  The kernel copies +0x04
 * to +0x17 (kernel_copy_message). */
typedef struct gps_request_entry {
  uint32_t
      field_00; /* +0x00: the senders clear it; the kernel does not copy it */
  uint32_t
      field_04; /* +0x04: the senders clear it; kernel_send_for_task refuses a
                   nonzero one other than the exchange's id, error 4 */
  gps_request_sender sender;  /* +0x08: one with the same sender waiting is not
                                 queued again; the monitors' may be replaced */
  gps_request_result result;  /* +0x09: 1 as sent; gps_count_response's kind as
                                 it comes back, which pllp_task takes as
                                 gps_ack_code or pll_scpi_complete's success */
  gps_request_code_word code; /* +0x0a: the request, gps_request_class's index;
                                 0x45 empties the ring before it is queued */
  gps_argument_mode argument_mode; /* +0x0c: gps_request_class's second
                                      argument, copied to gps_request_step's */
  uint8_t field_0d;
  int32_t* arguments; /* +0x0e: four longs, which gpsm_task copies into
                         gps_request_step's */
  uint8_t sent; /* +0x12: gps_request_post's last argument, 1 for the status
                   monitor's 0x4b; a newest waiting request with it set is not
                   replaced */
  uint8_t field_13[7];
} gps_request_entry;
