/* From the 58503A 3704's rewritten/ti.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image.
 * Each keeps its generated prototype. */

/* Writes a configuration's registers, 0 to 3, to the FPGA and marks them
 * loaded (0x45944 to 0x45968); fconf_regs' fifth, register 4, is not
 * among them. */
static void ti_registers_load(counter_configuration_fields* configuration) {
  for (uint8_t reg = 0; reg < sizeof configuration->fpga_registers; reg++) {
    fconf_regs[reg] = configuration->fpga_registers[reg];
    fpga_write(reg, fconf_regs[reg]);
  }
  configuration->registers_state = COUNTER_WORK_DONE;
}

/* Writes counter_configuration's four registers to the FPGA.
 * The result is what the loop's last compare leaves in D0, 0; its one
 * caller, counter_reset, ignores it. */
int32_t counter_load_registers(void) {
  ti_registers_load(&counter_configuration);
  return 0;
}

/* The FPGA's self-test: fails unless the preset counter reads zero
 * (0x45a0c). */
void counter_selftest(void) {
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  selftest.fpga = counter_read_raw() != 0;
}

/* Sets fconf_regs[3]'s sync bits, fpga_bits entry 2, to a sync_kind,
 * through 0 (0x4557a). */
void counter_set_sync(uint8_t kind) {
  static const uint8_t SYNC_MASK = 0x03;

  fconf_regs[3] &= ~SYNC_MASK;
  fpga_write(3, fconf_regs[3]);
  fconf_regs[3] |= kind;
  fpga_write(3, fconf_regs[3]);
}

/* Asks counter_arm to sync the counter count times by a kind, the
 * loop's steps and the console's clk_synch and sync_imm (0x4555a). */
void sync_request(counter_sync_kind kind, int32_t count) {
  counter_configuration.sync_kind = kind;
  counter_configuration.sync_count = count;
  counter_configuration.sync_mode = COUNTER_SYNC_WHEN_WANTED;
  counter_configuration.sync_state = COUNTER_WORK_WANTED;
}

/* The FPGA's status bits counter_read waits for, register 0's: bit 1 the
 * count's start, then bit 0 its stop (0x45834, 0x45844; docs/firmware/
 * z3817a-3752.md, "One reading"). */
static const uint8_t TI_STATUS_STARTED = 0x02;

static const uint8_t TI_STATUS_STOPPED = 0x01;

/* The count's top byte's registers, 0x18 and 0x19: the count is 32 bits,
 * four bytes, each two registers' nibbles, from 0x18 down to 0x12
 * (0x45710 to 0x45734), where the other images' is 24. */
static const uint8_t TI_COUNT_TOP_REGISTER = 0x18;

/* The counter's 32-bit count of 100 ns ticks, from FPGA registers 0x18
 * (its top byte) down to 0x12, less its preset (0x456fc). */
int64_t counter_read_raw(void) {
  /* a byte is two registers' nibbles, fpga_read_byte's */
  static const uint8_t REGISTERS_PER_BYTE = 2;
  uint32_t count = 0;

  for (uint8_t reg = TI_COUNT_TOP_REGISTER; reg >= FPGA_REGISTER_COUNT_LOW;
       reg -= REGISTERS_PER_BYTE) {
    count = count << 8 | fpga_read_byte(reg);
  }
  return counter_offset_apply(0, count);
}

/* Reloads the FPGA's registers if the configuration asks, presets the
 * counter, puts the read address at 0, syncs it if asked, and releases it
 * to count: the 58503A's but that it sets no mode, start or reference bit
 * (0x45760); register 4, the start's select, is 0x4538c's alone
 * (docs/firmware/z3817a-3752.md, "P84, register 4 and the status bits"). */
void counter_arm(interval_reading_fields* reading,
                 counter_configuration_fields* configuration) {
  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors != 0) {
    return;
  }
  if (configuration->registers_mode == COUNTER_REGISTERS_WHEN_WANTED &&
      configuration->registers_state == COUNTER_WORK_WANTED) {
    ti_registers_load(configuration);
  }
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  if (configuration->sync_mode == COUNTER_SYNC_WHEN_WANTED &&
      configuration->sync_state == COUNTER_WORK_WANTED) {
    counter_set_sync(configuration->sync_kind);
    configuration->sync_count--;
    if (configuration->sync_count < 1) {
      configuration->sync_state = COUNTER_WORK_DONE;
    }
  }
  fpga_set_bit(FPGA_BIT_PRESET, 0);
}

/* The interval's reading, into the reading: the count in seconds, plus
 * half a tick, less the configuration's calibration, plus
 * :DIAGnostic:TOFFset; on an error SCPI's NaN, and each error printed
 * (0x45810).  No interpolator term: the Z3805A's but for the half tick
 * in its place (docs/firmware/z3817a-3752.md, "One reading"). */
void counter_read(interval_reading_fields* reading,
                  counter_configuration_fields* configuration) {
  static const double SECONDS_PER_TICK = 1e-7;
  static const double HALF_TICK = 5e-8;
  static const uint16_t READING_TAG = 0xfff6;

  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors == 0) {
    if (counter_wait_ready(TI_STATUS_STARTED) &&
        counter_wait_ready(TI_STATUS_STOPPED)) {
      reading->value = int64_to_double(counter_read_raw());
      double_multiply_in_place(&reading->value, SECONDS_PER_TICK);
      double_add_in_place(&reading->value, HALF_TICK);
      double_subtract_in_place(&reading->value, configuration->calibration);
      double_add_in_place(&reading->value,
                          float_to_double(scpi_value_DIAGnostic_TOFFset));
      reading->tag = READING_TAG;
      reading->valid = 1;
    } else {
      reading->errors |= COUNTER_GATE_FAILURE;
      reading->errors |= COUNTER_MEASUREMENT_ABORTED;
    }
  }
  if (reading->errors == 0) {
    return;
  }
  reading->value = TI_NOT_A_NUMBER;
  reading->valid = 0;
  if (reading->errors & COUNTER_GATE_FAILURE) {
    task_dmessage("Gate failure\r\n");
  }
  if (reading->errors & COUNTER_RESULT_UNCOMPUTABLE) {
    task_dmessage("Result uncomputable\r\n");
  }
  if (reading->errors & COUNTER_MEASUREMENT_ABORTED) {
    task_dmessage("Measurement aborted\r\n");
  }
  if (reading->errors & COUNTER_CALIBRATION_FAILURE) {
    task_dmessage("Interpolator calibration failure\r\n");
  }
}

/* The counter's configuration's defaults, its registers into fconf_regs
 * too, and the reading marked invalid: the registers' fields the preset
 * alone, the calibration the Z3805A's 0.4000003 s, and the registers to
 * load; the sync's pair is left as it is (0x45976). */
void counter_configuration_init(counter_configuration_fields* configuration) {
  /* The fields by fpga_bit_index, fpga_fields_to_registers' first four:
   * the preset flagged, the address, the sync and the start's select 0.
   * Even, as the Z3815A's, since gcc may fill it by words. */
  uint8_t __attribute__((aligned(2))) fields[FPGA_BIT_START] = {
      [FPGA_BIT_PRESET] = TI_PRESET_ON};

  fpga_fields_to_registers(fields, configuration->fpga_registers);
  fpga_fields_to_registers(fields, fconf_regs);
  configuration->calibration = 0.4000003; /* the image's, 0x3fd9999adbb8f8da */
  configuration->registers_mode = COUNTER_REGISTERS_WHEN_WANTED;
  configuration->registers_state = COUNTER_WORK_WANTED;
  interval_reading.value = TI_NOT_A_NUMBER;
  interval_reading.valid = 0;
}

/* The counter's configuration at its defaults and the reading armed; no
 * interpolator to calibrate (0x459f6). */
void counter_init(void) {
  counter_configuration_init(&counter_configuration);
  interval_reading.armed = 1;
}

/* The counter's count less 0x55555555, which the FPGA presets it to,
 * the constant at 0x45a92, where the other images' 24-bit counter takes
 * 0x555555 (0x456d0). */
uint64_t counter_offset_apply(int32_t high, uint32_t low) {
  static const uint64_t PRESET = 0x55555555;

  return ((uint64_t)high << 32 | low) - PRESET;
}
