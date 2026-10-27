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
 * loaded if it took (0x225aa). */
void fpga_setup(void) {
  task_dmessage("setup fpga\r\n");
  if (fpga_program(FPGA_CONFIGURATION_COUNTER, 1)) {
    pll.fpga_configuration = FPGA_CONFIGURATION_COUNTER;
    pll.fpga_loaded = 1;
  }
}

/* The power-up FPGA test: loads the configuration fpga_setup loaded,
 * or the first if that is not below fpga_configurations' last, and
 * verifies it (0x18a82).  The ROM's last is 1, so it is always the
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

/* Each FPGA bit or field's register image in fconf_regs and its mask, by
 * fpga_bit_index: the image's table at fpga_bit_table (0x413ac). */
static const fpga_bit fpga_bits[] = {
    [FPGA_BIT_MODE] = {.reg = 0, .mask = 0x03, .is_flag = 0},
    [FPGA_BIT_PRESET] = {.reg = 0, .mask = 0x40, .is_flag = 1},
    [FPGA_BIT_START] = {.reg = 1, .mask = 0x01, .is_flag = 1},
    [FPGA_BIT_REFERENCE] = {.reg = 1, .mask = 0x02, .is_flag = 1},
    [FPGA_BIT_ADDRESS] = {.reg = 1, .mask = 0xf8, .is_flag = 0},
    [FPGA_BIT_SYNC] = {.reg = 3, .mask = 0x03, .is_flag = 0},
    [FPGA_BIT_ALARM] = {.reg = 3, .mask = 0x04, .is_flag = 1},
};

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

/* Builds the four registers' images from a value for each of
 * fpga_bits's fields: a flag's mask for a nonzero value, any
 * other's value ORed in (0x40b8c).  Seven fields, where the Z3805A's
 * has six. */
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

/* PORTF0's pins the alarm moves (docs/firmware/58503a-3704.md, "The
 * alarm outputs"): PF5, low while the alarm is on, and PF3, cleared
 * either way. */
static const uint8_t ALARM_PORTF_PF3 = 0x08;

static const uint8_t ALARM_PORTF_PF5 = 0x20;

/* Drives the alarm's outputs: PF3 low, PF5 low while on and high while
 * off, under enter_supervisor_mode, then the FPGA's FPGA_BIT_ALARM
 * (0x40828). */
void alarm_outputs_set(char alarm) {
  const bool on = alarm == 1;
  const uint16_t sr = enter_supervisor_mode();
  uint8_t port = sim.portf0 & ~ALARM_PORTF_PF3;

  if (on) {
    port &= ~ALARM_PORTF_PF5;
  } else {
    port |= ALARM_PORTF_PF5;
  }
  sim.portf0 = port;
  set_interrupt_mask(sr);
  fpga_set_bit(FPGA_BIT_ALARM, on);
}
