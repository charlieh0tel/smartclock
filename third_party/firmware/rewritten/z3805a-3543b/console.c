/* The pForth console's words, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/console`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

#include <stddef.h>

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports (0x12f86, 0x12f92, 0x12f9e). */
void ps_word(void) { os_process_report(0); }

void mem_rep_word(void) { os_memory_report(0); }

void s_rep_word(void) { task_stack_report(0); }
