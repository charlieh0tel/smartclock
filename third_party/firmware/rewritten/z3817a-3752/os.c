/* os.c's functions that are this image's own: the rest are common/os.c's,
 * the same in every image (docs/firmware/rewrite.md, "Shared C").  From
 * the 58503A 3704's os.c, the process and memory reports, which this
 * image's are but for their addresses, and the region report, whose
 * heading's columns this image spaces wider.  Its comments' addresses are
 * the Z3817A's. */

#include <stddef.h>

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

/* Copies the kernel's configuration and I/O table from ROM
 * (0x58bd8). */
void psos_config_init(void) {
  memcpy(&psos_config, &psos_config_defaults, sizeof psos_config);
  memcpy(io_table, io_table_defaults, sizeof io_table);
}

/* A task's name in task_name_text, or null for a record whose name's
 * address, task + 0x24, is 0; the copy's NUL is task_name_text's fifth
 * byte (0x26098, 0x260c2). */
char* task_name(void* task) {
  static const size_t NAME_LENGTH = sizeof((psos_task*)0)->name;

  if ((uint32_t)task + offsetof(psos_task, name) == 0) {
    return NULL;
  }
  strncpy(task_name_text, ((psos_task*)task)->name, NAME_LENGTH);
  task_name_text[NAME_LENGTH] = '\0';
  return task_name_text;
}

/* An exchange's name in object_name_text, or null for a record whose
 * name's address, object + 0x14, is 0; the copy's NUL is
 * object_name_text's fifth byte (0x260d6, 0x260fe). */
char* object_name(void* object) {
  static const size_t NAME_LENGTH = sizeof((psos_exchange*)0)->name;

  if ((uint32_t)object + offsetof(psos_exchange, name) == 0) {
    return NULL;
  }
  strncpy(object_name_text, ((psos_exchange*)object)->name, NAME_LENGTH);
  object_name_text[NAME_LENGTH] = '\0';
  return object_name_text;
}

/* Prints the region report: each region's bounds and limits, the memory
 * the tasks hold and that is free, and the tasks waiting for memory.  A
 * segment's region indexes the sums unchecked, as in the image.  The
 * other images' but for its heading, whose columns this image spaces
 * wider, its text at 0x590b0 (0x26820). */
void os_report_regions(printf_function* print) {
  /* A waiter's region is its record's bits 3 to 0, less 1. */
  static const uint32_t REGION_WAIT_MASK = 0xf;
  static const uint16_t SEGMENT_REGION_MASK = 3;
  psos_kernel* kernel = os_kernel_data();
  int32_t held_count[OS_REGIONS] = {0, 0};
  int32_t free_bytes[OS_REGIONS];
  int32_t held_bytes[OS_REGIONS] = {0, 0};
  void* start[OS_REGIONS] = {psos_config.data_area,
                             (void*)psos_config.region_2_start};

  print("%s",
        "Region  strt        end  min      max     pend   USED  cnt  AVAIL "
        "cnt\r\n");
  for (int32_t entry = 0; entry < task_count; entry++) {
    for (psos_segment* segment = ((psos_task*)task_table[entry].task)->segments;
         segment != NULL; segment = segment->next) {
      int32_t region = (segment->flags & SEGMENT_REGION_MASK) - 1;

      held_bytes[region] += segment->size;
      held_count[region]++;
    }
  }
  for (int32_t region = 0; region < OS_REGIONS; region++) {
    int32_t free_count = 0;
    uint32_t end = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    uint32_t pending = 0;

    free_bytes[region] = 0;
    for (psos_segment* segment = kernel->region_free[region]; segment != NULL;
         segment = segment->next) {
      free_bytes[region] += segment->size;
      free_count++;
    }
    if (start[region] != NULL) {
      end = kernel->region_end[region];
      minimum = kernel->region_minimum[region];
      maximum = kernel->region_maximum[region];
      pending = kernel->region_pending[region];
    }
    print("%1d 0x%08x 0x%08x %4d %8d %8d %4dkB %4d %4dkB %3d\r\n", region + 1,
          start[region], end, minimum, maximum, pending,
          os_kilobytes(held_bytes[region]), held_count[region],
          os_kilobytes(free_bytes[region]), free_count);
  }
  /* The queue ends at its head's field. */
  psos_task* end = (psos_task*)&kernel->region_waiting;
  psos_task* task = kernel->region_waiting;

  if (task != end) {
    print("   >> BLOCKED: ");
    while (task != end) {
      int8_t region = (task->region_wait & REGION_WAIT_MASK) + 1;

      print("%s %d(%d)", task_name(task), task->region_request, region);
      task = task->next;
      if (task != end) {
        print(",");
      }
    }
    print("%s", "\r\n");
  }
  print("%s", "\r\n");
}
