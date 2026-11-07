/* The installer's copies of the program's exit (`platform/stdlib`; stdlib.c has
 * the primary's). Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Runs the atexit functions and reports the exit value with its own
 * return address; never returns. */
void installer_program_exit(uint32_t value) {
  installer_call_atexit_functions();
  installer_program_end_report(value, (uint32_t)__builtin_return_address(0));
  for (;;) {
  }
}

/* Builds "Prog end, returned N" in program_end_message and stops with
 * fatal_error_with_code, whose code is return_address, program_exit's;
 * never returns (the primary's is program_end_report).  The digits are
 * built backward from a stop byte at the buffer's end, where they stay. */
void installer_program_end_report(uint32_t value, uint32_t return_address) {
  uint8_t* digit = &installer_program_end_message[LIBC_PROGRAM_END_DIGITS_END];
  uint32_t magnitude = value;

  installer_program_end_message[0] = 0;
  for (const char* text = "Prog end, returned "; *text != '\0'; text++) {
    libc_message_append(installer_program_end_message, *text);
  }
  *digit = LIBC_PROGRAM_END_DIGITS_STOP;
  if ((int32_t)value < 0) {
    magnitude = -value;
    libc_message_append(installer_program_end_message, '-');
  }
  do {
    *--digit = magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  for (; *digit != LIBC_PROGRAM_END_DIGITS_STOP; digit++) {
    libc_message_append(installer_program_end_message, '0' + *digit);
  }
  installer_fatal_error_with_code((char*)installer_program_end_message,
                                  (uint32_t)__builtin_return_address(0), value,
                                  return_address);
  for (;;) {
  }
}

/* Calls the functions atexit registered, the last first; the count is
 * signed (installer_call_atexit_functions_entry). */
void installer_call_atexit_functions(void) {
  while ((int16_t)installer_atexit_count > 0) {
    installer_atexit_count--;
    installer_atexit_functions[(int16_t)installer_atexit_count]();
  }
}

/* strtol on the installer's ctype table and errno (libc_strtol). */
int32_t installer_strtol(char* s, char** end, int32_t base) {
  return libc_strtol(s, end, base, installer_ctype_table, &installer_errno);
}

/* Registers a function for installer_call_atexit_functions: 1 for NULL
 * or a full table, else 0; nothing calls it. */
int32_t installer_atexit(atexit_function* function) {
  static const uint16_t FUNCTIONS_MOST =
      sizeof installer_atexit_functions / sizeof installer_atexit_functions[0];

  if (function == NULL || installer_atexit_count == FUNCTIONS_MOST) {
    return 1;
  }
  installer_atexit_functions[(int16_t)installer_atexit_count++] = function;
  return 0;
}

/* Saves its caller's registers, frame, return and stack, and through
 * the installer's trap #1 the status register, in a buffer; returns 0,
 * and again the value installer_longjmp gives. */
uint32_t installer_setjmp(uint32_t* buffer) {
  libc_setjmp_save();
  return 0;
}

/* Restores the registers installer_setjmp saved in a buffer, and
 * through the installer's trap #1 its status register, and returns to
 * installer_setjmp's caller with value in D0. */
uint64_t installer_longjmp(uint32_t* buffer, uint32_t value) {
  libc_longjmp_restore();
}
