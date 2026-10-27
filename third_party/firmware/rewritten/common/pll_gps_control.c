/* The functions of pll_gps_control.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_gps_control.c describes the module and holds the rest.  Written
 * by hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* Sends a request to gpsx_exchange, its four arguments copied to
 * gpsx_arguments, unless an acknowledgment is awaited; whether it was
 * sent, and then ack_awaited awaits its acknowledgment. */
uint8_t gpsx_send(int16_t code, gps_argument_mode argument_mode,
                  int32_t* arguments, uint8_t ack_awaited) {
  gps_request_entry request;

  if (gps_ack_awaited != GPS_ACK_NONE) {
    return 0;
  }
  for (int32_t i = 0; i < 4; i++) {
    gpsx_arguments[i] = arguments[i];
  }
  request.field_00 = 0;
  request.field_04 = 0;
  request.code = code;
  request.argument_mode = argument_mode;
  request.result = GPS_RESULT_REJECT;
  request.sender = GPS_SENDER_SEQUENCE;
  request.arguments = (int32_t*)gpsx_arguments;
  if ((uint8_t)psos_send_x(gpsx_exchange, &request) != 0) {
    return 0;
  }
  gps_ack_awaited = ack_awaited;
  gps_ack_ready = 0;
  gps_ack_code = 1;
  gpsx_sent_count++;
  gps_ack_wait_count = 0;
  return 1;
}

/* Whether the awaited acknowledgment has come, no longer awaiting it
 * once it has. */
uint8_t gps_ack_received(void) {
  if (gps_ack_ready != 0) {
    gps_ack_awaited = GPS_ACK_NONE;
  }
  return gps_ack_ready;
}
