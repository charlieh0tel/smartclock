/* The FPGA's registers that are this image's own, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/fpga`): fpga_set_bit and
 * fpga_set_field, through fconf_regs and fpga_bits.  The rest of the
 * module -- the loading, fpga_setup and fpga_program, and the readback,
 * fpga_verify -- is common/fpga.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype.  fconf_regs
 * and fpga_bits are docs/firmware/structures.md's; the registers are
 * mc68331.h's. */

/* Each FPGA bit or field's register image in fconf_regs and its mask, by
 * fpga_bit_index: the image's table at fpga_bit_table (0x49eae), eight
 * entries.  The first five are the shared prefix
 * (common/types/fpga_bit_index.h); the Z3815A's own follow, in registers
 * 2 and 3 where the 58503A's single alarm flag is register 3's.  The
 * last, register 3's low two bits, is what counter_set_sync writes
 * directly; fpga_set_bit and fpga_set_field take only the first seven. */
static const fpga_bit fpga_bits[] = {
    [FPGA_BIT_MODE] = {.reg = 0, .mask = 0x03, .is_flag = 0},
    [FPGA_BIT_PRESET] = {.reg = 0, .mask = 0x40, .is_flag = 1},
    [FPGA_BIT_START] = {.reg = 1, .mask = 0x01, .is_flag = 1},
    [FPGA_BIT_REFERENCE] = {.reg = 1, .mask = 0x02, .is_flag = 1},
    [FPGA_BIT_ADDRESS] = {.reg = 1, .mask = 0xf8, .is_flag = 0},
    [FPGA_BIT_SECONDS_PHASE] = {.reg = 2, .mask = 0x01, .is_flag = 1},
    [FPGA_BIT_START_SOURCE] = {.reg = 2, .mask = 0x0c, .is_flag = 0},
    [FPGA_BIT_SYNC] = {.reg = 3, .mask = 0x03, .is_flag = 0},
};

/* The FPGA's configuration registers this image's counter keeps images
 * of, eight, where the 58503A's keeps four (0x49678). */
static const uint8_t FPGA_REGISTERS = 8;

/* Builds the eight registers' images from a value for each of fpga_bits'
 * eight fields: a flag's mask for a nonzero value, any other's value ORed
 * in, as the 58503A's builds its four from seven (0x4965e). */
void fpga_fields_to_registers(uint8_t* fields, uint8_t* registers) {
  static const uint8_t FIELDS = sizeof(fpga_bits) / sizeof(fpga_bit);

  for (uint8_t reg = 0; reg < FPGA_REGISTERS; reg++) {
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

/* Sets or clears an fpga_bits bit and writes its register. */
void fpga_set_bit(fpga_bit_index bit, uint8_t on) {
  const fpga_bit* entry = &fpga_bits[bit];

  if (on) {
    fconf_regs[entry->reg] |= entry->mask;
  } else {
    fconf_regs[entry->reg] &= ~entry->mask;
  }
  fpga_write(entry->reg, fconf_regs[entry->reg]);
}

/* Sets an fpga_bits field to a value already in its place, clearing the
 * rest of the field, and writes its register. */
void fpga_set_field(fpga_bit_index field, uint8_t value) {
  const fpga_bit* entry = &fpga_bits[field];

  fconf_regs[entry->reg] = (fconf_regs[entry->reg] & ~entry->mask) | value;
  fpga_write(entry->reg, fconf_regs[entry->reg]);
}

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

/* Loads the FPGA's first configuration, under the kernel, and marks it
 * loaded if it took (0x350de). */
void fpga_setup(void) {
  task_dmessage("setup fpga\r\n");
  if (fpga_program(FPGA_CONFIGURATION_COUNTER, 1)) {
    pll.fpga_configuration = FPGA_CONFIGURATION_COUNTER;
    pll.fpga_loaded = 1;
  }
}

/* The power-up FPGA test: loads the configuration fpga_setup loaded,
 * or the first if that is not below fpga_configurations' last, and
 * verifies it (0x290b4).  The ROM's last is 1, so it is always the
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

/* The entries every image's fpga_bit_table starts with, the same in
 * each (common/types/fpga_bit_index.h): the register and mask of each
 * bit or field fpga_read and fpga_address_sweep_debug name, which left
 * common/fpga.c where the Z3815A 4010's fconf_regs holds eight
 * registers, not four.  The whole table is fpga_bits. */
static const fpga_bit fpga_shared_bits[] = {
    [FPGA_BIT_MODE] = {.reg = 0, .mask = 0x03, .is_flag = 0},
    [FPGA_BIT_PRESET] = {.reg = 0, .mask = 0x40, .is_flag = 1},
    [FPGA_BIT_START] = {.reg = 1, .mask = 0x01, .is_flag = 1},
    [FPGA_BIT_REFERENCE] = {.reg = 1, .mask = 0x02, .is_flag = 1},
    [FPGA_BIT_ADDRESS] = {.reg = 1, .mask = 0xf8, .is_flag = 0},
};

/* Sets a shared field's bits in its register's image in fconf_regs,
 * clearing the rest of the field, and writes the register. */
static void fpga_field_store(fpga_bit_index field, uint8_t bits) {
  const fpga_bit* entry = &fpga_shared_bits[field];

  fconf_regs[entry->reg] = (fconf_regs[entry->reg] & ~entry->mask) | bits;
  fpga_write(entry->reg, fconf_regs[entry->reg]);
}

/* The field of fpga_shared_bits that selects the register a read of the
 * FPGA returns, fconf_regs[1]'s top five bits (structures.md). */
static const uint8_t FPGA_ADDRESS_SHIFT = 3;

/* Reads an FPGA register: puts its number in the address field, reads
 * the FPGA, and puts the field back (0x4929e). */
uint8_t fpga_read(uint8_t reg) {
  const uint8_t saved = fconf_regs[fpga_shared_bits[FPGA_BIT_ADDRESS].reg];

  fpga_field_store(FPGA_BIT_ADDRESS, reg << FPGA_ADDRESS_SHIFT);
  /* a read at the FPGA's base returns the register addressed */
  const uint8_t value = fpga_port[0];
  fpga_field_store(FPGA_BIT_ADDRESS, saved);
  return value;
}

/* Steps the address field through its 32 values, reading the seconds
 * at each and printing the field's register; called by nothing
 * (0x49392). */
void fpga_address_sweep_debug(void) {
  static const uint8_t ADDRESSES = 32;
  const uint8_t reg = fpga_shared_bits[FPGA_BIT_ADDRESS].reg;
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
