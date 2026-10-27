/* The functions of gps_requests.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_requests.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The senders whose requests the task takes, ending in GPS_SENDER_NONE:
 * the image's table. */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE, GPS_SENDER_SETTING, GPS_SENDER_COMMAND_MONITOR,
    GPS_SENDER_STATUS_MONITOR, GPS_SENDER_NONE};

/* 1 for a sender the task takes requests from. */
uint32_t request_kind_valid(gps_request_sender sender) {
  const gps_request_sender* known = gps_requests_senders;

  while (*known != GPS_SENDER_NONE) {
    if (*known++ == sender) {
      return 1;
    }
  }
  return 0;
}

/* 1 if the newest waiting request is a monitor's not yet sent, which a
 * new request may replace. */
uint32_t request_ring_newest_replaceable(void) {
  gps_request_entry newest;

  if (request_ring_unput(&newest) == 0) {
    return 0;
  }
  request_ring_put(&newest);
  return (newest.sender == GPS_SENDER_COMMAND_MONITOR ||
          newest.sender == GPS_SENDER_STATUS_MONITOR) &&
         newest.sent == 0;
}
