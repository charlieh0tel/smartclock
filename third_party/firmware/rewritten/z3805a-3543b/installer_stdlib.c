/* The installer's copies of the program's exit (`platform/stdlib`; stdlib.c has
 * the primary's). Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Runs the atexit functions and reports the exit value with its own
 * return address; never returns (installer::0x1013ae). */
void installer_program_exit(uint32_t value) {
  installer_call_atexit_functions();
  installer_program_end_report(value, (uint32_t)__builtin_return_address(0));
  for (;;) {
  }
}

/* Builds "Prog end, returned N" in program_end_message and stops with
 * fatal_error_with_code, whose code is return_address, program_exit's;
 * never returns (0x149bc).  The digits are built backward from a stop
 * byte at the buffer's end, where they stay. */
void installer_program_end_report(uint32_t value, uint32_t return_address) {
  static const int32_t DIGITS_END = 0x2d;
  static const uint8_t DIGITS_STOP = 0xff;
  uint8_t* digit = &installer_program_end_message[DIGITS_END];
  uint32_t magnitude = value;

  installer_program_end_message[0] = 0;
  for (const char* text = "Prog end, returned "; *text != '\0'; text++) {
    libc_message_append(installer_program_end_message, *text);
  }
  *digit = DIGITS_STOP;
  if ((int32_t)value < 0) {
    magnitude = -value;
    libc_message_append(installer_program_end_message, '-');
  }
  do {
    *--digit = magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  for (; *digit != DIGITS_STOP; digit++) {
    libc_message_append(installer_program_end_message, '0' + *digit);
  }
  installer_fatal_error_with_code((char*)installer_program_end_message,
                                  (uint32_t)__builtin_return_address(0), value,
                                  return_address);
  for (;;) {
  }
}

/* Calls the functions atexit registered, the last first; the count is
 * signed (installer::0x109036). */
void installer_call_atexit_functions(void) {
  while ((int16_t)installer_atexit_count > 0) {
    installer_atexit_count--;
    installer_atexit_functions[(int16_t)installer_atexit_count]();
  }
}
