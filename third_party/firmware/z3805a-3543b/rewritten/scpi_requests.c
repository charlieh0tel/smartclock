/* The SCPI task's side of pllc_exchange, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/requests`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* send_x, pausing so many ticks between tries until it succeeds
 * (0x29e56). */
void send_x_retry(uint32_t exchange, void* message, uint32_t ticks) {
  while ((uint8_t)psos_send_x(exchange, message) != 0) {
    psos_pause(ticks);
  }
}

/* Sends a request and waits for the pllc task's done event, clearing a
 * stale one first (0x29e7e).  get_v's 1 and wait_v's 0s are as the
 * image passes them. */
void request_and_wait(uint32_t exchange, void* message) {
  uint32_t events = 0;

  psos_get_v(PLLC_DONE_EVENT, 1, &events);
  send_x_retry(exchange, message, PLLC_RETRY_TICKS);
  psos_wait_v(PLLC_DONE_EVENT, 0, 0, &events);
}

/* pllc_request_a to _e: a pllc_request to pllc_exchange, by
 * request_and_wait (structures.md, "The pllc task's SCPI requests").
 * Each leaves +0x00 unset, as the image does. */

/* The descriptor, index, source and a setting's new value (0x29ec2). */
void pllc_request_a(void* param, uint8_t index, uint8_t setting,
                    request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.setting = setting;
  request.source = source;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index and source (0x29ef6). */
void pllc_request_b(void* param, uint8_t index, request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source, values[0] and values[1] through a
 * pointer (0x29f24). */
void pllc_request_c(void* param, uint8_t index, uint32_t first,
                    uint32_t* second, request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = first;
  request.values[1] = *second;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source and values[0] (0x29f60). */
void pllc_request_d(void* param, uint8_t index, uint32_t value,
                    request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = value;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source and both values through a pointer
 * (0x29f94). */
void pllc_request_e(void* param, uint8_t index, uint32_t* values,
                    request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = values[0];
  request.values[1] = values[1];
  request_and_wait(pllc_exchange, &request);
}
