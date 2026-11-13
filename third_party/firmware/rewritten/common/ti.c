/* The time-interval counter, built on the FPGA, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"): the
 * counter's reset, its wait, its count's offset and the interpolator's
 * reading; each image's own ti.c holds the rest, the interpolator's
 * calibration among them, and the one-second reading the loop takes from
 * it is pll_reading.c's.  The exported C includes this file after its
 * own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.  interval_reading and
 * counter_configuration are docs/firmware/structures.md's "The
 * time-interval counter's records". */

/* Sets the FPGA up again and reloads the counter's registers. */
void counter_reset(void) {
  fpga_setup();
  counter_load_registers();
}

/* Polls the FPGA's status register for a bit, up to 512 times 10 ticks;
 * the reading is disarmed if it never comes or a 1PPS dies. */
uint8_t counter_wait_ready(uint8_t mask) {
  static const uint32_t POLL_TICKS = 10;
  uint16_t polls = 0x200;
  bool ready = false;
  uint8_t status = fpga_read(FPGA_REGISTER_STATUS);

  while (!gps_pps_dead && !internal_pps_dead && interval_reading.armed &&
         polls != 0) {
    if (status & mask) {
      ready = true;
      break;
    }
    psos_pause(POLL_TICKS);
    polls--;
    status = fpga_read(FPGA_REGISTER_STATUS);
  }
  if (polls == 0 || gps_pps_dead || internal_pps_dead) {
    interval_reading.armed = 0;
  }
  return ready;
}

/* The interpolator's reading, the complement of FPGA register 0x10's
 * byte. */
uint32_t interpolator_read(void) {
  return ~(uint32_t)fpga_read_byte(FPGA_REGISTER_INTERPOLATOR);
}
