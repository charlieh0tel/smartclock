/* eeprom.c's functions that are this image's own: the rest are
 * common/eeprom.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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
  for (int32_t tries = EEPROM_TRIES; !written && tries > 0; tries--) {
    *cell = value;
    for (int16_t polls = EEPROM_POLLS; *cell != value && polls > 0; polls--) {
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

/* Writes bytes to the EEPROM a page at a time, raising
 * STATUS_EEPROM_WRITE_FAILURE on a failure (0x3124a). */
uint32_t eeprom_write(char* destination, char* source, uint32_t length) {
  while ((int32_t)length > 0) {
    uint32_t chunk =
        EEPROM_PAGE_SIZE - ((uint32_t)destination & (EEPROM_PAGE_SIZE - 1));
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
