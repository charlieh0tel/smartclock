/* debug.c's functions that are this image's own: the rest are
 * common/debug.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Formats into a buffer of LIBC_PRINTF_LENGTH through vsprintf and
 * writes it to the console by polling (0x1dc50). */
void printf_polled(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  console_write_polled(text);
}

/* Keeps the message, and the task's name, at crash_message; prints
 * them and "RESET to recover." while debug output is on; then spins
 * until the watchdog restarts the processor (0x1ee7e). */
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
      printf_polled("%s: %s", task_name((void*)task), "FATAL ERROR: ");
      printf_polled("%s\r\nRESET to recover.\r\n", message);
      psos_pause(FATAL_PAUSES);
    }
    set_interrupt_mask(CPU_IPL_4);
  }
  for (;;) {
  }
}

/* Records an exception in last_exception, with the running task's name
 * while the kernel runs; prints it while debug_enabled; and stops with the
 * interrupts masked.  The fault address is kept and printed only
 * for a frame whose format is not 0 (0x11942). */
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
    printf_polled("\r\n");
    printf_polled("%s: %s", last_exception.name, exception_name(vector));
    printf_polled(": PC = %#8x, SR = %#04x", pc, (uint32_t)sr);
    if (has_address) {
      printf_polled(", address = %#8x", address);
    }
    printf_polled("\r\nRESET to recover.");
    if (kernel_running) {
      psos_pause(1);
    }
  }
  set_interrupt_mask(CPU_IPL_7);
  for (;;) {
  }
}
