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
