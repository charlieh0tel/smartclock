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

/* The installer's signal codes, its copies of scpi_response_begin_code,
 * scpi_message_end_code and scpi_input_end_code, 8, 7 and 6, as at
 * installer::0x107290 (the primary's 0x29636). */
static const uint32_t installer_scpi_response_begin_code = 8;
static const uint32_t installer_scpi_message_end_code = 7;
static const uint32_t installer_scpi_input_end_code = 6;

/* The installer's copies of the routines scpi_interpret calls. */
static const scpi_engine_routines installer_scpi_engine = {
    .arena_init = installer_arena_init,
    .arena_current = installer_arena_current,
    .arena_set = installer_arena_set,
    .trg_defined = installer_scpi_trg_defined,
    .context_init = installer_scpi_context_init,
    .path_reset = installer_scpi_path_reset,
    .skip_to_terminator = installer_scpi_skip_to_terminator,
    .parse_message_unit = installer_scpi_parse_message_unit,
    .call_handler = installer_scpi_call_handler,
    .format_response = installer_scpi_format_response,
    .clear_bit = installer_status_clear_bit,
    .is_command_error = installer_condition_is_command_error,
    .message_done = installer_scpi_message_done,
    .input_closed = installer_scpi_input_closed,
    .response_begin_code = &installer_scpi_response_begin_code,
    .message_end_code = &installer_scpi_message_end_code,
    .input_end_code = &installer_scpi_input_end_code,
};

/* The installer's parser loop, as scpi_interpret's (installer::0x106f30). */
uint32_t installer_scpi_interpret(int32_t input, int32_t output, void* buffer,
                                  uint32_t size, scpi_child_list* main_tree,
                                  scpi_child_list* common_list,
                                  status_model* status, uint32_t unused,
                                  uint32_t last_argument) {
  const int32_t handled = CPU_CALLERS_REGISTER(int32_t, "d3");

  return scpi_engine_interpret(
      &installer_scpi_engine, handled, (scpi_port*)input, (scpi_port*)output,
      buffer, size, main_tree, common_list, status, last_argument);
}

/* The installer's SCPI task: time sliced, takes a 1500-byte segment for
 * the interpreter's arena and runs the interpreter on the console port
 * until its session ends; then restarts the processor, whatever
 * :SYSTem:LANGuage chose, and deletes itself (installer::0x107902). */
void installer_scpi_task(void) {
  /* installer_scpi_interpret's last argument, unread, is the long above
   * its others: the task's D2, as it saved it. */
  const uint32_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  static const uint32_t ARENA_SIZE = 1500;
  static const uint32_t ERROR_BITS = 0xff;
  void* arena;

  installer_psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  const uint32_t error =
      installer_psos_service_8(ARENA_SIZE, 0, 1, 0, (uint32_t*)&arena) &
      ERROR_BITS;

  if (error != 0) {
    installer_printf("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  scpi_port* port = installer_scpi_session_init();

  if (port == NULL) {
    installer_console_write("scpi: error opening stream\n");
    return;
  }
  installer_scpi_interpret((int32_t)port, (int32_t)port, arena, ARENA_SIZE,
                           installer_scpi_trees.main,
                           installer_scpi_trees.common, installer_status_object,
                           0, caller_d2);
  installer_psos_free_segment(arena);
  port->methods->close(port);
  installer_restart();
  installer_psos_delete(0);
}
