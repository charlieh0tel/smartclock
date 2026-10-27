/* The 58503A's own of the operating system (`platform/os`; common/os.c
 * has the rest): psos_config_init, whose I/O table has three devices,
 * and task_name and object_name, whose names end in a NUL of their own.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

#include <stddef.h>

/* Copies the kernel's configuration and I/O table from ROM
 * (0x53972). */
void psos_config_init(void) {
  memcpy(&psos_config, &psos_config_defaults, sizeof psos_config);
  memcpy(io_table, io_table_defaults, sizeof io_table);
}

/* A task's name in task_name_text, or null for a record whose name's
 * address, task + 0x24, is 0; the copy's NUL is task_name_text's fifth
 * byte (0x160fa, 0x16124). */
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
 * object_name_text's fifth byte (0x16138, 0x16160). */
char* object_name(void* object) {
  static const size_t NAME_LENGTH = sizeof((psos_exchange*)0)->name;

  if ((uint32_t)object + offsetof(psos_exchange, name) == 0) {
    return NULL;
  }
  strncpy(object_name_text, ((psos_exchange*)object)->name, NAME_LENGTH);
  object_name_text[NAME_LENGTH] = '\0';
  return object_name_text;
}
