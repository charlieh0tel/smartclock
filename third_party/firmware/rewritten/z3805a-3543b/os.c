/* The Z3805A's own of the operating system (`platform/os`; common/os.c
 * has the rest): psos_config_init, which differs in the 58503A, and
 * task_name and object_name, which only this image writes by hand.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

#include <stddef.h>

/* Copies the kernel's configuration and I/O table from ROM
 * (0x51038). */
void psos_config_init(void) {
  memcpy(&psos_config, &psos_config_defaults, sizeof psos_config);
  memcpy(io_table, io_table_defaults, sizeof io_table);
}

/* A task's name in task_name_text, or null for a record whose name's
 * address, task + 0x24, is 0; the copy's NUL lands on object_name_text
 * (0x160a0). */
char* task_name(void* task) {
  if ((uint32_t)task + offsetof(psos_task, name) == 0) {
    return NULL;
  }
  strncpy(task_name_text, ((psos_task*)task)->name, sizeof task_name_text);
  object_name_text[0] = '\0';
  return task_name_text;
}

/* An exchange's name in object_name_text, or null for a record whose
 * name's address, object + 0x14, is 0; the copy's NUL lands on
 * task_table[0].state (0x160de). */
char* object_name(void* object) {
  if ((uint32_t)object + offsetof(psos_exchange, name) == 0) {
    return NULL;
  }
  strncpy(object_name_text, ((psos_exchange*)object)->name,
          sizeof object_name_text);
  task_table[0].state = 0;
  return object_name_text;
}

/* :DIAG:OS:PROC?'s report: the CPU, the tasks, the exchanges and the
 * regions; the source is unused. */
void os_process_report(request_source source) {
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  printf("%s", "\r\n");
  os_report_cpu((printf_function*)printf);
  task_count = task_table_build();
  printf("%s", "\r\n");
  os_report_processes((printf_function*)printf);
  os_report_process_summary((printf_function*)printf);
  printf("%s", "\r\n");
  os_report_exchanges((printf_function*)printf);
  os_report_exchange_summary((printf_function*)printf);
  printf("%s", "\r\n");
  os_report_regions((printf_function*)printf);
  printf("%s", os_report_rule);
}
/* :DIAG:OS:MEM?'s report: each task's segments, each region's free
 * segments, then the regions; the source is unused. */
void os_memory_report(request_source source) {
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  task_count = task_table_build();
  printf("%s", "\r\n");
  printf("%s",
         "Process  Memseg size(bytes)     next        last    neighbor   "
         "key\r\n");
  for (int32_t entry = 0; entry < task_count; entry++) {
    psos_task* task = task_table[entry].task;

    for (psos_segment* segment = task->segments; segment != NULL;
         segment = segment->next) {
      os_report_region_line(task_name(task), (uint32_t*)segment,
                            (printf_function*)printf);
    }
  }
  for (int32_t region = 0; region < OS_REGIONS; region++) {
    psos_segment* segment = os_kernel_data()->region_free[region];

    if (segment != NULL) {
      printf(">> Free memory - region: %d\r\n", region);
    }
    for (; segment != NULL; segment = segment->next) {
      os_report_region_line("****", (uint32_t*)segment,
                            (printf_function*)printf);
    }
  }
  printf("%s", "\r\n");
  os_report_regions((printf_function*)printf);
  printf("%s", os_report_rule);
}
/* :DIAG:OS:STAC?'s report: each task's user and supervisor stacks, their
 * saved pointers, sizes, use and tops, then the kernel's own supervisor
 * stack; the source is unused.  The kernel's stack's use is
 * the last printed task's supervisor use when its words are all zero,
 * and, when no task was printed either, D2 as the caller left it
 * (task_stack_report+0x1c2; docs/firmware/quirks.md, "Reads of what nothing
 * set"). */
void task_stack_report(request_source source) {
  uint16_t supervisor_use = CPU_CALLERS_REGISTER(uint32_t, "d2");
  /* The kernel's supervisor stack starts this far into its data area
   * (structures.md, "The tasks' stacks"). */
  static const uint32_t SYSTEM_STACK_OFFSET = 200;
  static char* const ROW = "$%08X %4s $%08X $%-5X %3u%% $%08X $%-5X %3u%%\r\n";
  static char* const TOPS = "               $%08X (TOS)       $%08X (TOS)\r\n";

  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  printf("%s", "\r\n");
  task_count = task_table_build();
  printf("%s", "    pid   name     USP   size            SSP   size\r\n");
  for (int16_t index = 0; index < task_count; index++) {
    psos_task* task = task_table[index].task;
    uint32_t user_top;
    uint32_t user_pointer;
    uint32_t user_size;
    uint32_t supervisor_top;
    uint32_t supervisor_pointer;
    uint32_t supervisor_size;

    task_stack_info(task, &user_top, &user_pointer, &user_size, &supervisor_top,
                    &supervisor_pointer, &supervisor_size);
    if (user_size == 0 && supervisor_size == 0) {
      continue;
    }
    const uint16_t user_use =
        user_size != 0 ? os_stack_use(user_top, user_size, user_pointer) : 0;
    supervisor_use =
        supervisor_size != 0
            ? os_stack_use(supervisor_top, supervisor_size, supervisor_pointer)
            : 0;
    /* task_stack_info's user size leaves out the gap, which the report
     * puts back. */
    printf(ROW, (uint32_t)task, task_name(task), user_pointer,
           user_size + OS_USER_STACK_GAP, (uint32_t)user_use,
           supervisor_pointer, supervisor_size, (uint32_t)supervisor_use);
    printf(TOPS, user_top, supervisor_top);
  }

  const uint32_t system_size = psos_config.supervisor_stack_size;

  if (system_size != 0) {
    const uint32_t bottom =
        (uint32_t)psos_config.data_area + SYSTEM_STACK_OFFSET;
    const uint32_t top = bottom + system_size;

    /* Up from the bottom to the first word in use, its use the rest. */
    for (const uint16_t* word = (const uint16_t*)bottom; (uint32_t)word < top;
         word++) {
      if (*word != 0) {
        supervisor_use =
            OS_PERCENT - os_stack_percent((uint32_t)word, bottom, system_size);
        break;
      }
    }
    printf("\r\n>> pSOS system supervisor stack:\r\n");
    printf(ROW, 0, "pSOS", 0, 0, 0, bottom, system_size,
           (uint32_t)supervisor_use);
    printf(TOPS, 0, top);
  }
  printf("%s", "\r\n");
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
}
