/* The functions of gps_task.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_task.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Idles the task, resets the response framing and empties the request
 * ring. */
void gps_link_reset(void) {
  gps_task_state = GPS_PROCESS_TASK_IDLE;
  gps_response_reset();
  request_ring_clear();
}
