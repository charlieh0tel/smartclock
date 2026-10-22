/* The operating system's small routines, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"): the
 * mutexes and the idle count, the console's process, memory and stack
 * reports, the stacks' use, a task's priority, the busy and sleeping
 * waits, and the exception hook and the exceptions' report.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The kernel's services are
 * docs/firmware/psos.md's. */

#include <stddef.h>

/* Clears the exception hook and its per-task switch (0x118a8). */
void exception_hook_reset(void) {
  exception_hook_per_task = 0;
  exception_hook = NULL;
}

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

/* Packs the name and creates the mutex as an exchange of that name;
 * psos_pack_name reads the name as this function's caller's first
 * argument (0x104e6). */
uint32_t mutex_create(char* name, mutex* lock) {
  return mutex_init(psos_pack_name(), lock);
}

/* mutex_acquire on its argument (0x10502). */
uint32_t mutex_lock(mutex* lock) { return mutex_acquire(lock); }

/* mutex_release on its argument (0x10518). */
uint32_t mutex_unlock(mutex* lock) { return mutex_release(lock); }

/* The count of idle loops in the last second, which os_report_cpu
 * prints (0x10538). */
uint32_t idle_count(void) { return idle_loops_last_second; }

/* stack_measure's user stack's use and size, then its supervisor
 * stack's: array sizes, so #define. */
#define OS_STACK_USAGE_LONGS 4
#define OS_STACK_MESSAGE_SIZE 80

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports (0x12f86, 0x12f92, 0x12f9e). */
void ps_word(void) { os_process_report(0); }

void mem_rep_word(void) { os_memory_report(0); }

void s_rep_word(void) { task_stack_report(0); }

/* Prints a task's user and supervisor stacks' use and sizes
 * (0x132f4). */
void stack_usage_report(void* task) {
  uint32_t usage[OS_STACK_USAGE_LONGS];
  char message[OS_STACK_MESSAGE_SIZE];

  if (stack_measure(task, usage) != 0) {
    return;
  }
  sprintf(message, "User: %5u of %5u b   Supv: %5u of %5u b\r\n", usage[0],
          usage[1], usage[2], usage[3]);
  task_debug_message(message);
}

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had (0x1339e). */
uint8_t task_set_priority(uint32_t task, uint8_t priority) {
  uint8_t old;

  psos_priority(task, 0, &old);
  psos_priority(task, priority - old, &old);
  return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0 (0x13428). */
void busy_wait(uint32_t count) {
  /* busy_wait's empty loops for each count (0x13432). */
  static const uint16_t BUSY_WAIT_SPINS = 0x2d9;

  if (count == 0) {
    return;
  }
  uint16_t remaining = count;

  do {
    for (uint16_t spin = BUSY_WAIT_SPINS; spin != 0; spin--) {
      __asm__ volatile("");
    }
    remaining--;
  } while (remaining != 0);
}

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long; nothing calls
 * it (0x13448). */
void sleep_seconds(float seconds) {
  /* The kernel's ticks a second, and the shortest and longest pauses
   * it takes, a tick and 2^21 seconds. */
  static const float TICKS_PER_SECOND = 1024;
  static const float SLEEP_LONGEST = 1 << 21;

  if (float_compare(0.0f, seconds) > 0) {
    seconds = 1.0f / TICKS_PER_SECOND;
  } else if (float_compare(seconds, SLEEP_LONGEST) > 0) {
    seconds = SLEEP_LONGEST;
  }
  uint32_t ticks = float_to_unsigned(float_multiply(seconds, TICKS_PER_SECOND));

  psos_pause(ticks == 0 ? 1 : ticks);
}
