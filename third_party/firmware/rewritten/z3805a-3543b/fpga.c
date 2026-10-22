/* The FPGA, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `devices/fpga`).  The exported C includes this file after its own, so
 * the types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the FPGA's loading, fpga_setup
 * and fpga_program; its readback, fpga_verify and the frames it is
 * checked in; and its registers, through fconf_regs and fpga_bits.  A
 * configuration is smartclock.h's fpga_configuration_index, into the ROM
 * table fpga_configurations; pll's FPGA fields, fconf_regs, fpga_bits
 * and fpga_readback_mask are docs/firmware/structures.md's; the lines
 * are docs/firmware/outputs.md's, "The FPGA bitstreams"; the registers
 * are mc68331.h's. */

/* The FPGA's lines on the MC68331's ports: PROGRAM, driven on port E;
 * INIT, read as PACTL's PCLKS, the PCLK pin's state (MC68331UM, D.2.7);
 * DONE, on port F, read there and driven low while PROGRAM is; and a
 * chip's readback line on port F (smartclock.h's fpga_chip).  PROGRAM
 * is fpga.h's. */
static const uint8_t FPGA_INIT = 0x08;            /* PACTL bit 3 */
static const uint8_t FPGA_DONE = 0x08;            /* PORTF bit 3 */
static const uint8_t FPGA_READBACK_FIRST = 0x40;  /* PORTF bit 6 */
static const uint8_t FPGA_READBACK_SECOND = 0x80; /* PORTF bit 7 */

/* Pauses, through the kernel when called from a task, else by busy
 * waiting. */
static void fpga_pause(uint32_t ticks, char with_task_name) {
  if (with_task_name) {
    psos_pause(ticks);
  } else {
    busy_wait(ticks);
  }
}

/* Polls INIT up to 1000 times for it to read high or low; true when it
 * does. */
static bool fpga_init_wait(bool high) {
  static const int32_t POLLS = 1000;

  for (int32_t poll = 0; poll < POLLS; poll++) {
    if (((gpt.pactl & FPGA_INIT) != 0) == high) {
      return true;
    }
  }
  return false;
}

/* Loads a configuration into the FPGA over the QSPI: holds DONE low,
 * clears the FPGA with PROGRAM and waits for INIT to fall and rise,
 * sends the bitstream, and checks that DONE rose; then pulses PROGRAM
 * (0x22da4). */
uint8_t fpga_program(fpga_configuration_index configuration,
                     char with_task_name) {
  /* The bitstream goes out 16 words at a time, its last bytes 2 words
   * at a time. */
  static const uint16_t BLOCK_WORDS = 16;
  static const uint16_t TAIL_WORDS = 2;
  const uint32_t BLOCK_BYTES = BLOCK_WORDS * sizeof(uint16_t);
  const uint32_t TAIL_BYTES = TAIL_WORDS * sizeof(uint16_t);

  if (configuration > fpga_configurations.last) {
    return 0;
  }
  fpga_programming = 1;
  sim.porte0 &= ~FPGA_PROGRAM;
  sim.portf0 |= FPGA_READBACK_FIRST | FPGA_READBACK_SECOND;
  sim.portf0 &= ~FPGA_DONE;
  sim.ddrf |= FPGA_DONE;
  if (!fpga_init_wait(false)) {
    debug_message("ERROR: FPGA INIT did not go low\r\n", with_task_name);
    fpga_programming = 0;
    return 0;
  }
  sim.porte0 |= FPGA_PROGRAM;
  const bool init_high = fpga_init_wait(true);
  sim.ddrf &= ~FPGA_DONE;
  if (!init_high) {
    debug_message("ERROR: FPGA INIT did not go high\r\n", with_task_name);
    fpga_programming = 0;
    return 0;
  }

  fpga_pause(1, with_task_name);
  if ((sim.portf0 & FPGA_DONE) != 0) {
    debug_message("ERROR: FPGA done line went high early\r\n", with_task_name);
  }
  uint8_t* next = fpga_configurations.entries[configuration].start;
  uint8_t* const end = fpga_configurations.entries[configuration].end;
  /* qspi_send takes the bitstream's bytes as the words it sends */
  if (with_task_name) {
    for (; next + BLOCK_BYTES < end; next += BLOCK_BYTES) {
      qspi_write(QSPI_FPGA_BLOCK, next, BLOCK_WORDS);
    }
    for (; next < end; next += TAIL_BYTES) {
      qspi_write(QSPI_FPGA_TAIL, next, TAIL_WORDS);
    }
  } else {
    for (; next + BLOCK_BYTES < end; next += BLOCK_BYTES) {
      qspi_send(QSPI_FPGA_BLOCK, (uint16_t*)next, BLOCK_WORDS);
    }
    for (; next < end; next += TAIL_BYTES) {
      qspi_send(QSPI_FPGA_TAIL, (uint16_t*)next, TAIL_WORDS);
    }
  }
  fpga_pause(2, with_task_name);

  const bool programmed = (sim.portf0 & FPGA_DONE) != 0;
  if (!programmed) {
    debug_message("ERROR: FPGA programming failed\r\n", with_task_name);
  } else if (with_task_name) {
    task_dmessage("FPGA programmed\r\n");
  }
  sim.porte0 &= ~FPGA_PROGRAM;
  sim.porte0 |= FPGA_PROGRAM;
  fpga_programming = 0;
  return programmed;
}

/* Loads the FPGA's first configuration, under the kernel, and marks it
 * loaded if it took (0x22d70). */
void fpga_setup(void) {
  task_dmessage("setup fpga\r\n");
  if (fpga_program(FPGA_CONFIGURATION_COUNTER, 1)) {
    pll.fpga_configuration = FPGA_CONFIGURATION_COUNTER;
    pll.fpga_loaded = 1;
  }
}

/* A frame of the FPGA's readback being compared, byte by byte, with the
 * configuration it was loaded from, under fpga_readback_mask's bits.
 * Each QSPI transfer reads 8 to 16 bits into receive, right-justified
 * and inverted; once a byte differs every byte after it is reported
 * too, in the frame's own formats. */
typedef struct fpga_readback {
  uint16_t transmit;      /* sent while reading: all ones */
  uint16_t receive;       /* the bits read, inverted */
  qspi_buffers buffers;   /* transmit and receive */
  uint8_t* configuration; /* the configuration's next byte */
  uint8_t* mask;          /* fpga_readback_mask's next byte */
  bool matches;           /* every byte so far agrees */
  char with_task_name;    /* debug_message's, and the kernel's transfer */
  char* byte_format;      /* a byte's report */
  char* word_format;      /* a byte's report in a 16-bit transfer */
} fpga_readback;

/* A transfer's width in bits, and all of a byte's. */
static const uint16_t FPGA_WORD_BITS = 16;

static const uint8_t FPGA_BYTE_ALL = 0xff;

/* Starts a frame's readback at its configuration's and mask's bytes;
 * receive is left as the stack has it until a transfer writes it, as
 * the image's is. */
static void fpga_readback_start(fpga_readback* readback, uint8_t* configuration,
                                uint8_t* mask, char with_task_name,
                                char* byte_format, char* word_format) {
  readback->matches = true;
  readback->configuration = configuration;
  readback->mask = mask;
  readback->with_task_name = with_task_name;
  readback->byte_format = byte_format;
  readback->word_format = word_format;
  readback->buffers.transmit = &readback->transmit;
  readback->buffers.receive = &readback->receive;
  readback->transmit = 0xffff;
}

/* Reads the readback's next bits, through the kernel when called from
 * a task. */
static void fpga_readback_read(fpga_readback* readback, uint16_t bits) {
  if (readback->with_task_name) {
    qspi_read(QSPI_FPGA_READBACK, &readback->buffers, bits);
  } else {
    qspi_transfer(QSPI_FPGA_READBACK, &readback->buffers, bits);
  }
}

/* The bits read, high byte and low, as the configuration has them. */
static uint8_t fpga_readback_high(const fpga_readback* readback) {
  return ~readback->receive >> 8;
}

static uint8_t fpga_readback_low(const fpga_readback* readback) {
  return ~readback->receive;
}

/* Compares a byte read with the configuration's next, in the mask's
 * bits and those given, and reports it unless all have agreed. */
static void fpga_readback_compare(fpga_readback* readback, uint8_t read,
                                  uint8_t bits, char* format) {
  if ((*readback->mask & (*readback->configuration ^ read) & bits) != 0) {
    readback->matches = false;
  }
  if (!readback->matches) {
    char text[80];

    debug_sprintf(text, format, ~readback->receive, *readback->configuration,
                  *readback->mask);
    debug_message(text, readback->with_task_name);
  }
}

static void fpga_readback_advance(fpga_readback* readback) {
  readback->configuration++;
  readback->mask++;
}

/* Compares both bytes read, in the bits given of each, and moves past
 * them. */
static void fpga_readback_pair(fpga_readback* readback, uint8_t high_bits,
                               uint8_t low_bits, char* format) {
  fpga_readback_compare(readback, fpga_readback_high(readback), high_bits,
                        format);
  fpga_readback_advance(readback);
  fpga_readback_compare(readback, fpga_readback_low(readback), low_bits,
                        format);
  fpga_readback_advance(readback);
}

/* Reads and compares 16-bit words while all agree. */
static void fpga_readback_words(fpga_readback* readback, int32_t words) {
  for (int32_t word = 0; readback->matches && word < words; word++) {
    fpga_readback_read(readback, FPGA_WORD_BITS);
    fpga_readback_pair(readback, FPGA_BYTE_ALL, FPGA_BYTE_ALL,
                       readback->word_format);
  }
}

/* Reads a frame's last bits, 9 to 15, left-justifies them and compares
 * them as two bytes, the second only in its top bits; moves past the
 * first byte only. */
static void fpga_readback_last(fpga_readback* readback, uint16_t bits) {
  const uint16_t unused = FPGA_WORD_BITS - bits;

  fpga_readback_read(readback, bits);
  readback->receive <<= unused;
  fpga_readback_compare(readback, fpga_readback_high(readback), FPGA_BYTE_ALL,
                        readback->byte_format);
  fpga_readback_advance(readback);
  fpga_readback_compare(readback, fpga_readback_low(readback),
                        FPGA_BYTE_ALL << unused, readback->byte_format);
}

/* Reports where a frame's readback stopped, if it differed. */
static void fpga_readback_finish(const fpga_readback* readback, char* format) {
  if (!readback->matches) {
    char text[80];

    debug_sprintf(text, format, readback->configuration, readback->mask);
    debug_message(text, readback->with_task_name);
  }
}

/* Reads back the first frame and compares it, 13 bytes: 11 bits, of
 * which the low 8 are compared, five words, 8 bits and 10 (0x22fc6). */
bool fpga_verify_first(uint8_t* configuration, uint8_t* mask,
                       char with_task_name) {
  static const uint16_t START_BITS = 11;
  static const uint16_t BYTE_BITS = 8;
  static const uint16_t LAST_BITS = 10;
  static const int32_t WORDS = 5;
  fpga_readback readback;

  fpga_readback_start(&readback, configuration, mask, with_task_name,
                      "first: inv read: %02hX config: %02hX mask: %02hX\r\n",
                      "first: inv read: %04hX config: %02hX mask: %02hX\r\n");
  fpga_readback_read(&readback, START_BITS);
  fpga_readback_compare(&readback, fpga_readback_low(&readback), FPGA_BYTE_ALL,
                        readback.byte_format);
  fpga_readback_advance(&readback);
  fpga_readback_words(&readback, WORDS);
  if (readback.matches) {
    fpga_readback_read(&readback, BYTE_BITS);
    fpga_readback_compare(&readback, fpga_readback_low(&readback),
                          FPGA_BYTE_ALL, readback.byte_format);
    fpga_readback_advance(&readback);
  }
  if (readback.matches) {
    fpga_readback_last(&readback, LAST_BITS);
  }
  fpga_readback_finish(&readback, "first: config_ptr: %08X mask_ptr: %08X\r\n");
  return readback.matches;
}

/* Reads back an odd frame and compares it: six words and 10 bits
 * (0x23304). */
bool fpga_verify_odd(uint8_t* configuration, uint8_t* mask,
                     char with_task_name) {
  static const uint16_t LAST_BITS = 10;
  static const int32_t WORDS = 6;
  fpga_readback readback;

  fpga_readback_start(&readback, configuration, mask, with_task_name,
                      "odd: inv read: %02hX config: %02hX mask: %02hX\r\n",
                      "odd: inv read: %04hX config: %02hX mask: %02hX\r\n");
  fpga_readback_words(&readback, WORDS);
  if (readback.matches) {
    fpga_readback_last(&readback, LAST_BITS);
  }
  fpga_readback_finish(&readback, "odd: config_ptr: %08X mask_ptr: %08X\r\n");
  return readback.matches;
}

/* Reads back an even frame and compares it: 12 bits, five words and 14
 * bits (0x2353a). */
bool fpga_verify_even(uint8_t* configuration, uint8_t* mask,
                      char with_task_name) {
  static const uint16_t START_BITS = 12;
  static const uint16_t LAST_BITS = 14;
  static const int32_t WORDS = 5;
  fpga_readback readback;

  fpga_readback_start(&readback, configuration, mask, with_task_name,
                      "even: inv read: %02hX config: %02hX mask: %02hX\r\n",
                      "even: inv read: %04hX config: %02hX mask: %02hX\r\n");
  fpga_readback_read(&readback, START_BITS);
  fpga_readback_pair(&readback, FPGA_BYTE_ALL >> (FPGA_WORD_BITS - START_BITS),
                     FPGA_BYTE_ALL, readback.byte_format);
  fpga_readback_words(&readback, WORDS);
  if (readback.matches) {
    fpga_readback_last(&readback, LAST_BITS);
    fpga_readback_advance(&readback);
  }
  fpga_readback_finish(&readback, "even: config_ptr: %08X mask_ptr: %08X\r\n");
  return readback.matches;
}

/* Reads back one chip's configuration and compares it, frame by frame,
 * after a pulse on its readback line (0x2384a). */
bool fpga_verify_chip(fpga_configuration_index configuration,
                      char with_task_name, fpga_chip chip) {
  /* The bitstream's header comes before the first chip's frames, and
   * the second chip's start 0xf08 bytes after; the first frame is 13
   * bytes, then frames 2 to 285 alternate, even 14 bytes and odd 13. */
  static const uint32_t HEADER_BYTES = 5;
  static const uint32_t SECOND_CHIP_OFFSET = 0xf08;
  static const uint32_t FIRST_FRAME_BYTES = 13;
  static const uint32_t EVEN_FRAME_BYTES = 14;
  static const uint32_t ODD_FRAME_BYTES = 13;
  static const uint32_t SECOND_FRAME = 2;
  static const uint32_t LAST_FRAME = 285;
  /* How long the readback line is held low. */
  static const int32_t PULSE_LOOPS = 4;
  uint8_t* bits =
      fpga_configurations.entries[configuration].start + HEADER_BYTES;
  uint8_t* mask = fpga_readback_mask;
  uint8_t line;

  if (chip == FPGA_CHIP_SECOND) {
    bits += SECOND_CHIP_OFFSET;
    line = FPGA_READBACK_SECOND;
  } else {
    line = FPGA_READBACK_FIRST;
  }
  sim.portf0 &= ~(line | FPGA_DONE);
  for (volatile int32_t loop = 0; loop < PULSE_LOOPS; loop++) {
  }
  sim.portf0 |= line;
  sim.portf0 &= ~FPGA_DONE;

  bool matches = fpga_verify_first(bits, mask, with_task_name);
  if (matches) {
    char text[80];
    uint32_t frame;

    bits += FIRST_FRAME_BYTES;
    mask += FIRST_FRAME_BYTES;
    for (frame = SECOND_FRAME; matches && frame <= LAST_FRAME; frame++) {
      if (frame % 2 == 0) {
        matches = fpga_verify_even(bits, mask, with_task_name);
        bits += EVEN_FRAME_BYTES;
        mask += EVEN_FRAME_BYTES;
      } else {
        matches = fpga_verify_odd(bits, mask, with_task_name);
        bits += ODD_FRAME_BYTES;
        mask += ODD_FRAME_BYTES;
      }
    }
    if (!matches) {
      debug_sprintf(text, "chip: %1d frame: %d\r\n", chip, frame - 1);
      debug_message(text, with_task_name);
    }
  }
  if (!matches) {
    char text[80];

    debug_sprintf(text, "chip %1d failed compare\r\n", chip);
    debug_message(text, with_task_name);
  }
  return matches;
}

/* Verifies a configuration against the FPGA's readback, the first chip;
 * false for one past fpga_configurations' last (0x23980). */
uint8_t fpga_verify(fpga_configuration_index configuration,
                    char with_task_name) {
  if (configuration > fpga_configurations.last) {
    return 0;
  }
  return fpga_verify_chip(configuration, with_task_name, FPGA_CHIP_FIRST);
}

/* The power-up FPGA test: loads the configuration fpga_setup loaded,
 * or the first if that is not below fpga_configurations' last, and
 * verifies it (0x18918).  The ROM's last is 1, so it is always the
 * first. */
uint8_t fpga_load_test(void) {
  const fpga_configuration_index configuration =
      pll.fpga_configuration < fpga_configurations.last
          ? pll.fpga_configuration
          : FPGA_CONFIGURATION_COUNTER;

  if (!fpga_program(configuration, 0)) {
    return 0;
  }
  return fpga_verify(configuration, 0);
}

/* Writes an FPGA register (0x40756). */
void fpga_write(uint8_t reg, uint8_t value) { fpga_port[reg] = value; }

/* Each FPGA bit or field's register image in fconf_regs and its mask, by
 * fpga_bit_index: the image's table at 0x41310, fpga_bit_table. */
static const fpga_bit fpga_bits[] = {
    [FPGA_BIT_MODE] = {.reg = 0, .mask = 0x03, .is_flag = 0},
    [FPGA_BIT_PRESET] = {.reg = 0, .mask = 0x40, .is_flag = 1},
    [FPGA_BIT_START] = {.reg = 1, .mask = 0x01, .is_flag = 1},
    [FPGA_BIT_REFERENCE] = {.reg = 1, .mask = 0x02, .is_flag = 1},
    [FPGA_BIT_ADDRESS] = {.reg = 1, .mask = 0xf8, .is_flag = 0},
    [FPGA_BIT_SYNC] = {.reg = 3, .mask = 0x03, .is_flag = 0},
};

/* Sets an fpga_bits field's bits in its register's image in
 * fconf_regs, clearing the rest of the field, and writes the
 * register. */
static void fpga_field_store(uint8_t field, uint8_t bits) {
  const fpga_bit* entry = &fpga_bits[field];

  fconf_regs[entry->reg] = (fconf_regs[entry->reg] & ~entry->mask) | bits;
  fpga_write(entry->reg, fconf_regs[entry->reg]);
}

/* Sets or clears an fpga_bits bit and writes its register
 * (0x409a4). */
void fpga_set_bit(fpga_bit_index bit, uint8_t on) {
  const fpga_bit* entry = &fpga_bits[bit];

  if (on) {
    fconf_regs[entry->reg] |= entry->mask;
  } else {
    fconf_regs[entry->reg] &= ~entry->mask;
  }
  fpga_write(entry->reg, fconf_regs[entry->reg]);
}

/* Sets an fpga_bits field to a value already in its place and
 * writes its register (0x40a0e). */
void fpga_set_field(fpga_bit_index field, uint8_t value) {
  fpga_field_store(field, value);
}

/* Builds the four registers' images from a value for each of
 * fpga_bits's fields: a flag's mask for a nonzero value, any
 * other's value ORed in (0x40afe). */
void fpga_fields_to_registers(uint8_t* fields, uint8_t* registers) {
  static const uint8_t REGISTERS = sizeof(fconf_regs);
  static const uint8_t FIELDS = sizeof(fpga_bits) / sizeof(fpga_bit);

  for (uint8_t reg = 0; reg < REGISTERS; reg++) {
    registers[reg] = 0;
  }
  for (uint8_t field = 0; field < FIELDS; field++) {
    const fpga_bit* entry = &fpga_bits[field];

    if (entry->is_flag == 1) {
      if (fields[field] != 0) {
        registers[entry->reg] |= entry->mask;
      } else {
        registers[entry->reg] &= ~entry->mask;
      }
    } else {
      registers[entry->reg] &= ~entry->mask;
      registers[entry->reg] |= fields[field];
    }
  }
}

/* The field of fpga_bits that selects the register a read of the
 * FPGA returns, fconf_regs[1]'s top five bits (structures.md). */
static const uint8_t FPGA_ADDRESS_SHIFT = 3;

/* Reads an FPGA register: puts its number in the address field, reads
 * the FPGA, and puts the field back (0x4076a). */
uint8_t fpga_read(uint8_t reg) {
  const uint8_t saved = fconf_regs[fpga_bits[FPGA_BIT_ADDRESS].reg];

  fpga_field_store(FPGA_BIT_ADDRESS, reg << FPGA_ADDRESS_SHIFT);
  /* a read at the FPGA's base returns the register addressed */
  const uint8_t value = fpga_port[0];
  fpga_field_store(FPGA_BIT_ADDRESS, saved);
  return value;
}

/* A byte from two FPGA registers' low nibbles, the next register's the
 * high one (0x40ba6). */
uint8_t fpga_read_byte(fpga_register reg) {
  static const uint8_t NIBBLE = 0xf;
  static const uint8_t NIBBLE_BITS = 4;
  const uint8_t high = fpga_read(reg + 1);
  const uint8_t low = fpga_read(reg);

  return (low & NIBBLE) | high << NIBBLE_BITS;
}

/* Whether any of the bits given are set in an FPGA register; called by
 * nothing (0x407d4). */
bool fpga_bit_test(uint8_t bits, char reg) {
  return (bits & fpga_read(reg)) != 0;
}

/* FPGA register 0's high nibble, a count of seconds modulo 16, read
 * until two reads agree or 20 more pairs have not (0x407fa). */
char fpga_read_seconds(void) {
  static const int32_t RETRIES = 20;
  static const uint8_t SECONDS_SHIFT = 4;
  /* fpga_read's register 0, the counter's status */
  uint8_t first = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
  uint8_t second = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;

  for (int32_t retries = RETRIES; first != second && retries > 0; retries--) {
    first = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
    second = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
  }
  return first;
}

/* Steps the address field through its 32 values, reading the seconds
 * at each and printing the field's register; called by nothing
 * (0x4085e). */
void fpga_address_sweep_debug(void) {
  static const uint8_t ADDRESSES = 32;
  const uint8_t reg = fpga_bits[FPGA_BIT_ADDRESS].reg;
  char text[80];

  for (uint8_t address = 0; address < ADDRESSES; address++) {
    fpga_field_store(FPGA_BIT_ADDRESS, address << FPGA_ADDRESS_SHIFT);
    debug_sprintf(text, "old_addr, fconf_regs[1] = %x %x \r\n", address,
                  fconf_regs[reg]);
    task_dmessage(text);
    fpga_read_seconds();
    debug_sprintf(text, "old_addr, fconf_regs[1] = %x %x \r\n", address,
                  fconf_regs[reg]);
    task_dmessage(text);
    task_dmessage(" \r\n");
  }
}
