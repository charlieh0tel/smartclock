/* The EEPROM: its lock, its byte and page writes, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/eeprom`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The EEPROM writes a page of 64 bytes at once (eeprom_write_page). */
static const uint32_t RECORDS_EEPROM_PAGE_SIZE = 64u;

/* Tries at a write, and polls of the last byte written for it to read
 * back, on each try. */
static const int32_t RECORDS_EEPROM_TRIES = 5;

static const int16_t RECORDS_EEPROM_POLLS = 5000;

/* The status register's interrupt mask; eeprom_lock and eeprom_unlock
 * take the lock only at mask 0. */
static const uint16_t RECORDS_SR_INTERRUPT_MASK = 0x0700;

/* The length of eeprom_init's, eeprom_lock's and eeprom_unlock's fatal
 * message: an array size, so #define. */
#define RECORDS_MESSAGE_SIZE 80

/* Creates eepx and sends it the lock's one message (0x30f10). */
void eeprom_init(void) {
  char message[RECORDS_MESSAGE_SIZE];

  uint32_t error = psos_create_x("eepx", 0, 0, &eepx_exchange);
  if ((uint8_t)error != 0) {
    sprintf(message, "Unable to create eepx: %X", error & RECORDS_FIELD_MASK);
    fatal_error(message);
  }
  eepx_token.status = 0;
  eepx_token.exchange = eepx_exchange;
  eepx_token.owner = 0;
  eepx_token.depth = 0;
  error = psos_send_x(eepx_exchange, &eepx_token);
  if ((uint8_t)error != 0) {
    sprintf(message, "Unable to initialize eepx: %X",
            error & RECORDS_FIELD_MASK);
    fatal_error(message);
  }
}

/* Takes the EEPROM's recursive lock; 0 when held, 1 while a write runs at
 * a raised interrupt mask, or request_x's error (0x30fae). */
char eeprom_lock(void) {
  /* request_x's second long as eeprom_lock passes it; psos.md does
   * not give its meaning. */
  static const uint32_t EEPX_REQUEST_WAIT = 0x2800u;
  /* The one error of request_x eeprom_lock returns to its caller
   * rather than calling fatal_error; not traced. */
  static const uint8_t EEPX_ERROR_RETURNED = 0x81;
  int32_t sr = enter_supervisor();

  if (!kernel_running) {
    return 0;
  }
  if ((sr & RECORDS_SR_INTERRUPT_MASK) != 0) {
    return eeprom_writing != 0;
  }

  uint32_t task = psos_my_pid();
  if (task == eepx_token.owner) {
    eepx_token.depth++;
    return 0;
  }
  uint32_t error =
      psos_request_x(eepx_exchange, &eepx_token, 0, EEPX_REQUEST_WAIT);
  uint8_t code = (uint8_t)error;
  if (code == 0) {
    eepx_token.owner = task;
    eepx_token.depth = 0;
    return 0;
  }
  if (code != EEPX_ERROR_RETURNED) {
    char message[RECORDS_MESSAGE_SIZE];
    sprintf(message, "Unable to acquire eepx token: %X",
            error & RECORDS_FIELD_MASK);
    fatal_error(message);
  }
  return (char)code;
}

/* Releases a level of the EEPROM's lock, sending eepx its message back at
 * the last (0x3105e). */
void eeprom_unlock(void) {
  int32_t sr = enter_supervisor();

  if (!kernel_running || (sr & RECORDS_SR_INTERRUPT_MASK) != 0) {
    return;
  }
  if (psos_my_pid() != eepx_token.owner) {
    task_debug_message("eepx_close and not owner\r\n");
    return;
  }
  if (eepx_token.depth < 0) {
    fatal_error("eepx semaphore count is < 0");
  } else if (eepx_token.depth > 0) {
    eepx_token.depth--;
  } else {
    eepx_token.owner = 0;
    if (eepx_token.exchange != eepx_exchange) {
      fatal_error("eepx semaphore has been corrupted");
    }
    uint32_t error = psos_send_x(eepx_exchange, &eepx_token);
    if ((uint8_t)error != 0) {
      char message[RECORDS_MESSAGE_SIZE];
      sprintf(message, "Unable to release eepx token: %X",
              error & RECORDS_FIELD_MASK);
      fatal_error(message);
    }
  }
}

/* Sets or clears eeprom_writing.  eeprom_lock reads it at a raised
 * interrupt mask, so the store must happen where the image makes it,
 * though the flag is cleared again before anything else here reads it. */
static void records_set_writing(bool writing) {
  *(volatile uint8_t*)&eeprom_writing = writing;
}

/* Writes a byte to the EEPROM and polls until it reads back, five tries,
 * raising STATUS_EEPROM_WRITE_FAILURE on a failure (0x31128). */
bool eeprom_write_byte(char* address, char value) {
  volatile char* cell = address;

  if (eeprom_lock() != 0) {
    status_happening(STATUS_EEPROM_WRITE_FAILURE);
    return false;
  }
  records_set_writing(true);
  bool written = false;
  for (int32_t tries = RECORDS_EEPROM_TRIES; !written && tries > 0; tries--) {
    *cell = value;
    for (int16_t polls = RECORDS_EEPROM_POLLS; *cell != value && polls > 0;
         polls--) {
    }
    if (*cell == value) {
      written = true;
    }
  }
  records_set_writing(false);
  eeprom_unlock();
  if (!written) {
    status_happening(STATUS_EEPROM_WRITE_FAILURE);
  }
  return written;
}

/* The EEPROM page an address is in. */
static uint32_t records_eeprom_page(const char* address) {
  return (uint32_t)address & ~(uint32_t)(RECORDS_EEPROM_PAGE_SIZE - 1);
}

/* Writes bytes within one EEPROM page, its interrupts masked, and checks
 * them, five tries; false for none or a span across pages (0x3119e). */
bool eeprom_write_page(char* destination, char* source, uint16_t length) {
  if (length == 0) {
    return false;
  }
  volatile char* last = destination + (length - 1);
  if (records_eeprom_page(destination) !=
      records_eeprom_page((const char*)last)) {
    return false;
  }
  if (eeprom_lock() != 0) {
    return false;
  }

  bool written = false;
  records_set_writing(true);
  for (int32_t tries = RECORDS_EEPROM_TRIES; !written && tries > 0; tries--) {
    uint16_t sr = enter_supervisor_mode();
    volatile char* to = destination;
    const char* from = source;
    while (to < last) {
      *to++ = *from++;
    }
    *to = *from;
    set_interrupt_mask(sr);
    for (int16_t polls = RECORDS_EEPROM_POLLS; *to != *from && polls > 0;
         polls--) {
    }

    written = true;
    to = destination;
    from = source;
    while (written && to <= last) {
      if (*to++ != *from++) {
        written = false;
      }
    }
  }
  records_set_writing(false);
  eeprom_unlock();
  return written;
}

/* Writes bytes to the EEPROM a page at a time, raising
 * STATUS_EEPROM_WRITE_FAILURE on a failure (0x3124a). */
uint32_t eeprom_write(char* destination, char* source, uint32_t length) {
  while ((int32_t)length > 0) {
    uint32_t chunk = RECORDS_EEPROM_PAGE_SIZE -
                     ((uint32_t)destination & (RECORDS_EEPROM_PAGE_SIZE - 1));
    if (length < chunk) {
      chunk = length;
    }
    if (!eeprom_write_page(destination, source, (uint16_t)chunk)) {
      status_happening(STATUS_EEPROM_WRITE_FAILURE);
      return 0;
    }
    length -= chunk;
    destination += chunk;
    source += chunk;
  }
  return 1;
}
