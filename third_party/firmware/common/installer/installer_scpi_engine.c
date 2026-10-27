/* The installer's copies of the SCPI task's start (`applications/scpi/
 * engine`; scpi_engine.c has the primary's), on the installer's task.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Has the installer's spool create its SCPI task, at priority 0x19, and
 * leaves the console to it. */
void installer_scpi_task_start(void) {
  /* spoo_run calls it with three longs; it reads the first. */
  installer_spoo_run((spoo_function*)installer_scpi_task_create,
                     SCPI_TASK_PRIORITY, 0, 0);
  installer_task_console = 0;
}

/* Closes the SCPI port, the console port's close, from its method
 * table. */
void installer_scpi_session_close(void) { installer_scpi_port_close(); }

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them. */
void* installer_arena_alloc(int32_t size, scpi_arena* arena) {
  return scpi_arena_alloc(size, arena);
}

/* The bytes left in an arena, and its next free byte. */
void* installer_arena_remaining(int32_t* remaining, scpi_arena* arena) {
  return scpi_arena_remaining(remaining, arena);
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them. */
void* installer_arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  return scpi_arena_take(buffer, size, arena);
}

/* An arena's next free byte. */
void* installer_arena_current(scpi_arena* arena) { return arena->next; }

/* Sets an arena's next free byte. */
void installer_arena_set(void* next, scpi_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot. */
void installer_arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena_init(buffer, size, slot);
}

/* installer_scpi_interpret's hook at each message's end and error,
 * passed the call and the condition reported (STATUS_NO_CONDITION at an
 * end): 0, no further error. */
uint32_t installer_scpi_message_done(scpi_call* call, int32_t condition) {
  return 0;
}

/* installer_scpi_interpret's hook when the input closes: 0. */
uint32_t installer_scpi_input_closed(scpi_call* call) { return 0; }

/* Fills a parser context, the word from its long's low half. */
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
 * the root. */
int32_t installer_scpi_parse_message_unit(scpi_context* parser,
                                          int16_t* reset_path) {
  return scpi_engine_parse_message_unit(installer_scpi_char_classes,
                                        installer_status_negative_conditions,
                                        parser, reset_path);
}

/* Calls a node's setter or query with the call's parameters, its
 * response and the call. */
int32_t installer_scpi_call_handler(scpi_call* call, void** response) {
  return scpi_engine_call_handler(call, response);
}

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

/* The installer's parser loop, as scpi_interpret's. */
uint32_t installer_scpi_interpret(scpi_port* input, scpi_port* output,
                                  void* buffer, uint32_t size,
                                  scpi_child_list* main_tree,
                                  scpi_child_list* common_list,
                                  status_model* status, uint32_t unused,
                                  uint32_t last_argument) {
  const int32_t handled = CPU_CALLERS_REGISTER(int32_t, "d3");

  return scpi_engine_interpret(&installer_scpi_engine, handled, input, output,
                               buffer, size, main_tree, common_list, status,
                               last_argument);
}
