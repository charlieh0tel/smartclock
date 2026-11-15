/* The 58503B 1.01.04's own of the debug and fatal output: the
 * exception's name, which names a vector past the table's last as
 * "unknown vector " and its offset in hexadecimal, where common/'s and
 * the Z3817A 3752's give "Oooh! That tickles."; the decompiler took its
 * table for a string before it (docs/firmware/58503b-1.01.04.md, "The
 * function check").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); it keeps its generated
 * prototype. */

/* An exception's name, from its frame's format and vector word: the
 * vector's offset, 4 times its number, indexes exception_names as bytes
 * from vector 2's, the first named, unchecked below it and unaligned as
 * the image's is (0x21bfc); past the last named, "unknown vector " and
 * the offset as four hexadecimal digits in exception_unknown_name
 * (0x21bc6 to 0x21bf8). */
char* exception_name(uint16_t vector) {
  static const uint16_t UNKNOWN_BASE = 16;
  static const uint16_t UNKNOWN_DIGITS = 4;
  uint16_t offset = vector & CPU_VECTOR_OFFSET_MASK;

  if (offset > CPU_VECTOR_OFFSET_LAST_NAMED) {
    libc_copy(exception_unknown_name, "unknown vector ");
    unsigned_to_digits(
        exception_unknown_name + libc_length(exception_unknown_name), offset,
        UNKNOWN_BASE, UNKNOWN_DIGITS);
    return exception_unknown_name;
  }
  return *(char**)((char*)exception_names + offset -
                   CPU_VECTOR_OFFSET_FIRST_NAMED);
}

/* Keeps the message, and the task's name, at crash_message; prints
 * them and "RESET to recover." while debug output is on; then spins
 * until the watchdog restarts the processor (0x2d8e2). */
void fatal_error(char* message) {
  /* The length below which crash_message takes ", " and a task's name. */
  static const uint32_t CRASH_ROOM_FOR_NAME = 74;
  static const uint32_t FATAL_PAUSES = 10;
  uint32_t task;

  strncpy(crash_message, message, MONITORS_CRASH_MESSAGE_SIZE);
  crash_message[MONITORS_CRASH_MESSAGE_SIZE - 1] = 0;
  if (kernel_running && (uint8_t)psos_ident(0, &task, 0) == 0) {
    uint32_t length = 0;

    while (crash_message[length] != 0) {
      length++;
    }
    if (length < CRASH_ROOM_FOR_NAME) {
      strcat(crash_message, ", ");
      strcat(crash_message, task_name((void*)task));
    }
    if (debug_enabled) {
      sci_printf_polled("%s: %s", task_name((void*)task), "FATAL ERROR: ");
      sci_printf_polled("%s\r\nRESET to recover.\r\n", message);
      psos_pause(FATAL_PAUSES);
    }
    set_interrupt_mask(CPU_IPL_4);
  }
  for (;;) {
  }
}

/* Records an exception in last_exception, with the running task's name
 * while the kernel runs; prints it to the SCI while debug_enabled; and
 * stops with the interrupts masked.  The fault address is kept and
 * printed only for a frame whose format is not 0.  The Z3815A's, but that
 * the two lines without arguments go out through console_write_polled
 * (0x21ab2). */
void print_exception(uint16_t sr, uint32_t pc, uint16_t vector,
                     uint32_t address) {
  bool has_address = (vector & CPU_VECTOR_FORMAT_MASK) != 0;

  last_exception.pending = 1;
  last_exception.name[0] = 0;
  last_exception.sr = sr;
  last_exception.pc = pc;
  last_exception.vector = vector;
  last_exception.address = has_address ? address : 0;
  if (kernel_running) {
    const char* name = task_name((void*)psos_my_pid());
    char* to = last_exception.name;

    while ((*to++ = *name++) != 0) {
    }
  }
  if (debug_enabled) {
    console_write_polled("\r\n");
    sci_printf_polled("%s: %s", last_exception.name, exception_name(vector));
    sci_printf_polled(": PC = %#8x, SR = %#04x", pc, (uint32_t)sr);
    if (has_address) {
      sci_printf_polled(", address = %#8x", address);
    }
    console_write_polled("\r\nRESET to recover.");
    if (kernel_running) {
      psos_pause(1);
    }
  }
  set_interrupt_mask(CPU_IPL_7);
  for (;;) {
  }
}
