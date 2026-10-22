/* The installer's copies of the SCPI task's start (`applications/scpi/
 * engine`; scpi_engine.c has the primary's), on the installer's task.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Has the installer's spool create its SCPI task, at priority 0x19, and
 * leaves the console to it (installer::0x100d20). */
void installer_scpi_task_start(void) {
  static const uint32_t SCPI_TASK_PRIORITY = 0x19;

  /* spoo_run calls it with three longs; it reads the first. */
  installer_spoo_run((spoo_function*)installer_scpi_task_create,
                     SCPI_TASK_PRIORITY, 0, 0);
  installer_task_console = 0;
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (installer::0x107886). */
uint32_t installer_scpi_task_create(uint32_t priority) {
  static const uint32_t ERROR_BITS = 0xff;
  /* The SCPI task's stacks in words, supervisor and user (psos.md,
   * "spawn"). */
  static const uint32_t TASK_SUPERVISOR_STACK_WORDS = 0x300;
  static const uint32_t TASK_USER_STACK_WORDS = 0x400;
  uint32_t existing;

  if ((uint8_t)installer_psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error =
      installer_psos_create("scpi", 0, priority, TASK_SUPERVISOR_STACK_WORDS,
                            TASK_USER_STACK_WORDS, 0, &installer_task_scpi) &
      ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return installer_psos_start(installer_task_scpi, installer_scpi_task) &
         ERROR_BITS;
}

/* Clears the SCPI input, opens the port and prompts; the console port
 * (installer::0x10742a). */
scpi_port* installer_scpi_session_init(void) {
  installer_scpi_echo_length = 0;
  installer_scpi_input_repeat = 0;
  installer_scpi_input_end = installer_scpi_input_line;
  installer_scpi_input_next = installer_scpi_input_line;
  installer_scpi_input_line[0] = 0;
  installer_scpi_port_open();
  installer_scpi_prompt(0);
  installer_status_update_rqs();
  return &installer_console_port;
}

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them (installer::0x10735a). */
void* installer_arena_alloc(int32_t size, scpi_arena* arena) {
  return scpi_arena_alloc(size, arena);
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them (installer::0x10739c). */
void* installer_arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  return scpi_arena_take(buffer, size, arena);
}

/* An arena's next free byte (installer::0x1073b8). */
void* installer_arena_current(scpi_arena* arena) { return arena->next; }

/* Sets an arena's next free byte (installer::0x1073c0). */
void installer_arena_set(void* next, scpi_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot
 * (installer::0x1073ca). */
void installer_arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena_init(buffer, size, slot);
}

/* installer_scpi_interpret's hook at each message's end and error: 0,
 * no further error (installer::0x1076ec). */
uint32_t installer_scpi_message_done(void) { return 0; }

/* installer_scpi_interpret's hook when the input closes: 0
 * (installer::0x1076f0). */
uint32_t installer_scpi_input_closed(void) { return 0; }

/* Fills a parser context, the word from its long's low half
 * (installer::0x10472e). */
int32_t installer_scpi_context_init(scpi_context* context, scpi_port* input,
                                    uint32_t word, scpi_path* path,
                                    scpi_call* call, scpi_arena* arena,
                                    uint32_t field_1e,
                                    scpi_child_list* main_tree,
                                    scpi_child_list* common_list) {
  return scpi_engine_context_init(context, input, word, path, call, arena,
                                  field_1e, main_tree, common_list);
}

/* Parses one program message unit: its header, then its parameters into
 * the call's buffer; *reset_path says whether the next unit starts at
 * the root (installer::0x104764). */
int32_t installer_scpi_parse_message_unit(scpi_context* parser,
                                          int16_t* reset_path) {
  return scpi_engine_parse_message_unit(installer_scpi_char_classes,
                                        installer_status_negative_conditions,
                                        parser, reset_path);
}

/* Calls a node's setter or query with the call's parameters, its
 * response and the call (installer::0x10729c). */
int32_t installer_scpi_call_handler(scpi_call* call, void** response) {
  return scpi_engine_call_handler(call, response);
}
