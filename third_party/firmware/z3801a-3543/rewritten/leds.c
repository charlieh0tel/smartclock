/* The Z3805A's front-panel lamps, the latch at 0x300000, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `devices/leds`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  The lamps are docs/firmware/outputs.md's "The Z3805A's
 * lamps". */

/* Writes the lamps' flags to the latch, whole; returns the byte written
 * (0x464a8).  pllp_task calls it at the end of each pass, and lamp_test
 * after walking a bit through the latch. */
uint8_t leds_update(void) {
  /* The latch's bits (docs/firmware/outputs.md); 3, 6 and 7 drive
   * nothing the firmware names. */
  static const uint8_t ENABLED = 0x01;
  static const uint8_t ACTIVE = 0x02;
  static const uint8_t ALARM = 0x04;
  static const uint8_t GPS_LOCK = 0x10;
  static const uint8_t HOLDOVER = 0x20;
  uint8_t lamps = led_enabled ? ENABLED : 0;

  if (led_active) {
    lamps |= ACTIVE;
  }
  if (led_alarm) {
    lamps |= ALARM;
  }
  if (pll.led_gps_lock) {
    lamps |= GPS_LOCK;
  }
  if (pll.led_holdover) {
    lamps |= HOLDOVER;
  }
  lamp_latch = lamps;
  return lamps;
}
