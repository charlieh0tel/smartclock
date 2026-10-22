/* Debug and fatal output, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `platform/debug`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype. */

#include <stddef.h>

/* An exception's name, from its frame's format and vector word: the
 * vector's offset, 4 times its number, indexes exception_names as bytes
 * from vector 2's, the first named, unchecked below it and unaligned as
 * the image's is (0x11a2a). */
char* exception_name(uint16_t vector) {
  /* The vector's offset in an exception frame's format and vector
   * word, below the format (CPU_VECTOR_FORMAT_MASK). */
  static const uint16_t VECTOR_OFFSET_MASK = 0x0fff;
  /* exception_names names the vectors from 2, bus error, to 58
   * (offset 0xe8), by an exception frame's vector offset. */
  static const uint16_t VECTOR_OFFSET_FIRST_NAMED = 2 * 4;
  static const uint16_t VECTOR_OFFSET_LAST = 0xe8;
  uint16_t offset = vector & VECTOR_OFFSET_MASK;

  if (offset > VECTOR_OFFSET_LAST) {
    return "Oooh! That tickles. I don't know that vector.";
  }
  return *(char**)((char*)exception_names + offset - VECTOR_OFFSET_FIRST_NAMED);
}

/* Records an exception in last_exception, with the running task's name
 * while the kernel runs; prints it while debug_enabled; and stops with the
 * interrupts masked (0x11942).  The fault address is kept and printed only
 * for a frame whose format is not 0. */
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

/* A length-prefixed message and ", %08X" with a code, to fatal_error;
 * the two arguments before the code are unread (0x1eefc). */
void fatal_error_with_code(char* message, uint32_t unused_1, uint32_t unused_2,
                           uint32_t code) {
  char text[100];

  memcpy(text, message + 1, *message);
  sprintf(text + *message, ", %08X", code);
  fatal_error(text);
}

/* Writes a string by polling while debug output is on; returns its
 * length, 0 when off (0x1ef74). */
int32_t debug_write(char* text) {
  if (!debug_enabled) {
    return 0;
  }
  return console_write_polled(text);
}

/* The calling task's id, from ident with no name. */
static uint32_t spool_my_task(void) {
  uint32_t task;
  uint8_t error;

  if ((uint8_t)psos_ident(0, &task, &error) != 0) {
    fatal_error("dmessage");
  }
  return task;
}

/* Prints "task: message" with the calling task's name, while debug
 * output is on (0x1eff6). */
void task_debug_message(char* message) {
  if (debug_enabled) {
    uint32_t task = spool_my_task();

    debug_printf("%s: %s", task_name((void*)task), message);
  }
}

/* A task and its dmes_ flag, which task_dmessage looks the caller up
 * in. */
typedef struct spool_task_flag {
  uint32_t* task;
  uint32_t* enabled;
} spool_task_flag;

/* The tasks task_dmessage prints for, in the order it checks them. */
static const spool_task_flag spool_task_flags[] = {
    {&task_gpsm, &dmes_gpsm}, {&task_hmon, &dmes_hmon},
    {&task_klok, &dmes_klok}, {&task_pllp, &dmes_pllp},
    {&task_root, &dmes_root}, {&task_scpi, &dmes_scpi},
    {&task_spoo, &dmes_spoo}, {&task_curv, &dmes_curv},
};

/* Prints "task: message" with the calling task's name, while debug
 * output and the task's dmes_ flag are on (0x1f056). */
void task_dmessage(char* message) {
  uint32_t task;
  bool enabled = false;

  if (!debug_enabled) {
    return;
  }
  task = spool_my_task();
  for (uint32_t i = 0; i < sizeof spool_task_flags / sizeof spool_task_flags[0];
       i++) {
    if (task == *spool_task_flags[i].task &&
        *spool_task_flags[i].enabled != 0) {
      enabled = true;
      break;
    }
  }
  if (enabled) {
    debug_printf("%s: %s", task_name((void*)task), message);
  }
}

/* Keeps the message, and the task's name, at crash_message; prints
 * them and "RESET to recover." while debug output is on; then spins
 * until the watchdog restarts the processor (0x1f176). */
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

/* Prints a message through task_debug_message, with the task's name,
 * or else through debug_write by polling (0x22d4a). */
void debug_message(char* message, char with_task_name) {
  if (with_task_name) {
    task_debug_message(message);
  } else {
    debug_write(message);
  }
}
