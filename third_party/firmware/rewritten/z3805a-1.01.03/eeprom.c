/* From the 58503B 1.01.04's eeprom.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those common/ does not hold, each identical
 * or relocated in image_diff's matches (docs/firmware/z3805a-1.01.03.md,
 * "The export and the checks"); their comments' addresses are the Z3805A
 * 1.01.03's.  The 58503B's note follows. */

/* The Z3817A 3752's eeprom.c, copied for the 58503B 1.01.04, each of its
 * functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses were the 58503B's.  The Z3817A's own: from
 * common/eeprom.c, the Z3817A 3752's until it shares common/: the
 * EEPROM's lock, which reads the status register as the decompiler cannot
 * write, and its page write, whose polls of the last byte written the
 * decompiler's C drops, with the byte write and eeprom_write from the
 * Z3815A 4010's eeprom.c, each function the same in both images
 * (docs/firmware/z3817a-3752.md, "The function check"). Each keeps its
 * generated prototype. */

/* Writes a byte to the EEPROM and polls until it reads back, five tries,
 * raising STATUS_EEPROM_WRITE_FAILURE on a failure (0x32e60). */
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
 * STATUS_EEPROM_WRITE_FAILURE on a failure (0x32f82). */
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
