/* The installer's copies of the exception and fatal error reports
 * (`platform/debug`; debug.c has the primary's), simpler than the
 * primary's: an exception is reported as the boot code's, "Boot: ",
 * and kept nowhere, and a fatal error only stops.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* Formats into a buffer of LIBC_PRINTF_LENGTH through installer_vsprintf
 * and writes it to the console by polling (installer::0x10398e). */
void installer_printf_polled(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  installer_vsprintf(text, format, arguments);
  va_end(arguments);
  installer_console_write_polled(text);
}

/* Prints an exception, "Boot: " and its name, the PC and SR and, for a
 * frame whose format is not 0, the fault address; then stops with the
 * interrupts masked (installer::0x1005d2). */
void installer_print_exception(uint16_t sr, uint32_t pc, uint16_t vector,
                               uint32_t address) {
  installer_printf_polled("\r\n");
  installer_printf_polled("Boot: %s", installer_exception_name(vector));
  installer_printf_polled(": PC = %#8x, SR = %#04x", pc, (uint32_t)sr);
  if ((vector & CPU_VECTOR_FORMAT_MASK) != 0) {
    installer_printf_polled(", address = %#8x", address);
  }
  installer_printf_polled("\r\nRESET to recover.");
  if (installer_kernel_running) {
    installer_psos_pause(1);
  }
  installer_set_interrupt_mask(CPU_IPL_7);
  for (;;) {
  }
}

/* An exception's name, from its frame's format and vector word: the
 * vector's offset, 4 times its number, indexes installer_exception_names as
 * bytes from vector 2's, the first named, unchecked below it and unaligned as
 * the image's is (installer::0x100658). */
char* installer_exception_name(uint16_t vector) {
  uint16_t offset = vector & CPU_VECTOR_OFFSET_MASK;

  if (offset > CPU_VECTOR_OFFSET_LAST_NAMED) {
    return "Oooh! That tickles. I don't know that vector.";
  }
  return *(char**)((char*)installer_exception_names + offset -
                   CPU_VECTOR_OFFSET_FIRST_NAMED);
}

/* Sets the interrupt mask to level 4 and spins until the watchdog
 * restarts the processor; the message is unread (installer::0x103e3c). */
void installer_fatal_error(char* message) {
  installer_set_interrupt_mask(CPU_IPL_4);
  for (;;) {
  }
}

/* A length-prefixed message and ", %08X" with a code, to fatal_error;
 * the two arguments before the code are unread (installer::0x103ff4;
 * fatal_error_with_code, 0x1ed70). */
void installer_fatal_error_with_code(char* message, uint32_t unused_1,
                                     uint32_t unused_2, uint32_t code) {
  char text[100];

  installer_memcpy(text, message + 1, *message);
  installer_sprintf(text + *message, ", %08X", code);
  installer_fatal_error(text);
}
