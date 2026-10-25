/* The installer's copies of the SCPI task's side of pllc_exchange
 * (`applications/scpi/requests`; scpi_requests.c has the primary's), on
 * the installer's kernel calls and its installer_pllc_exchange, which
 * nothing in the installer sets; nothing calls the requests.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* send_x, pausing so many ticks between tries until it succeeds
 * (installer::0x1077a4). */
void installer_send_x_retry(uint32_t exchange, void* message, uint32_t ticks) {
  while ((uint8_t)installer_psos_send_x(exchange, message) != 0) {
    installer_psos_pause(ticks);
  }
}

/* Sends a request and waits for the pllc task's done event, clearing a
 * stale one first (installer::0x1077cc).  get_v's 1 and wait_v's 0s are
 * as the image passes them. */
void installer_request_and_wait(uint32_t exchange, void* message) {
  uint32_t events = 0;

  installer_psos_get_v(PLLC_DONE_EVENT, 1, &events);
  installer_send_x_retry(exchange, message, PLLC_RETRY_TICKS);
  installer_psos_wait_v(PLLC_DONE_EVENT, 0, 0, &events);
}

/* installer_pllc_request_a to _e: a pllc_request to
 * installer_pllc_exchange, by installer_request_and_wait.  Each leaves
 * +0x00 unset, as the image does. */

/* The descriptor, index, source and a setting's new value
 * (installer::0x107810). */
void installer_pllc_request_a(void* param, uint8_t index, uint8_t setting,
                              request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.setting = setting;
  request.source = source;
  installer_request_and_wait(installer_pllc_exchange, &request);
}

/* The descriptor, index and source (installer::0x107844). */
void installer_pllc_request_b(void* param, uint8_t index,
                              request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  installer_request_and_wait(installer_pllc_exchange, &request);
}

/* The descriptor, index, source, values[0] and values[1] through a
 * pointer (installer::0x107872). */
void installer_pllc_request_c(void* param, uint8_t index, uint32_t first,
                              uint32_t* second, request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = first;
  request.values[1] = *second;
  installer_request_and_wait(installer_pllc_exchange, &request);
}

/* The descriptor, index, source and values[0] (installer::0x1078ae). */
void installer_pllc_request_d(void* param, uint8_t index, uint32_t value,
                              request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = value;
  installer_request_and_wait(installer_pllc_exchange, &request);
}

/* The descriptor, index, source and both values through a pointer
 * (installer::0x1078e2). */
void installer_pllc_request_e(void* param, uint8_t index, uint32_t* values,
                              request_source source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = values[0];
  request.values[1] = values[1];
  installer_request_and_wait(installer_pllc_exchange, &request);
}
