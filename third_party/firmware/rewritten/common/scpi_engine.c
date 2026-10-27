/* The SCPI interpreter: its parser of message units, the calls to the
 * handlers, the arena and the handlers' helpers, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/engine`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  scpi_engine.h holds the parser's bodies, which the
 * installer's copies share. */

/* Fills a parser context, the word from its long's low half;
 * scpi_interpret calls it (scpi_interpret+0x94, scpi_context_init). */
int32_t scpi_context_init(scpi_context* context, scpi_port* input,
                          uint32_t word, scpi_path* path, scpi_call* call,
                          scpi_arena* arena, uint32_t field_1e,
                          scpi_child_list* main_tree,
                          scpi_child_list* common_list) {
  return scpi_engine_context_init(context, input, word, path, call, arena,
                                  field_1e, main_tree, common_list);
}

/* Parses one program message unit: its header, then its parameters into
 * the call's buffer; *reset_path says whether the next unit starts at
 * the root. */
int32_t scpi_parse_message_unit(scpi_context* parser, int16_t* reset_path) {
  return scpi_engine_parse_message_unit(
      scpi_char_classes, status_negative_conditions, parser, reset_path);
}

/* The routines scpi_interpret calls, and its signals' codes. */
static const scpi_engine_routines scpi_engine_primary = {
    .arena_init = arena_init,
    .arena_current = arena_current,
    .arena_set = arena_set,
    .trg_defined = scpi_trg_defined,
    .context_init = scpi_context_init,
    .path_reset = scpi_path_reset,
    .skip_to_terminator = scpi_skip_to_terminator,
    .parse_message_unit = scpi_parse_message_unit,
    .call_handler = scpi_call_handler,
    .format_response = scpi_format_response,
    .clear_bit = status_clear_bit,
    .is_command_error = condition_is_command_error,
    .message_done = scpi_message_done,
    .input_closed = scpi_input_closed,
    .response_begin_code = &scpi_response_begin_code,
    .message_end_code = &scpi_message_end_code,
    .input_end_code = &scpi_input_end_code,
};

/* The parser's loop: parses each message unit of input, calls its
 * handler, writes a query's response to output and reports errors to
 * status, its arena in buffer, until a handler ends the session; its
 * result then unset, -1 if the port fails.  The eighth
 * argument is unread. */
uint32_t scpi_interpret(scpi_port* input, scpi_port* output, void* buffer,
                        uint32_t size, scpi_child_list* main_tree,
                        scpi_child_list* common_list, status_model* status,
                        uint32_t unused, uint32_t last_argument) {
  const int32_t handled = CPU_CALLERS_REGISTER(int32_t, "d3");

  return scpi_engine_interpret(&scpi_engine_primary, handled, input, output,
                               buffer, size, main_tree, common_list, status,
                               last_argument);
}

/* Calls a node's setter or query with the call's parameters, its
 * response and the call. */
int32_t scpi_call_handler(scpi_call* call, void** response) {
  return scpi_engine_call_handler(call, response);
}

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them. */
void* arena_alloc(int32_t size, scpi_arena* arena) {
  return scpi_arena_alloc(size, arena);
}

/* The bytes left in an arena, and its next free byte. */
void* arena_remaining(int32_t* remaining, scpi_arena* arena) {
  return scpi_arena_remaining(remaining, arena);
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them. */
void* arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  return scpi_arena_take(buffer, size, arena);
}

/* An arena's next free byte. */
void* arena_current(scpi_arena* arena) { return arena->next; }

/* Sets an arena's next free byte. */
void arena_set(void* next, scpi_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot. */
void arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena_init(buffer, size, slot);
}

/* Returns its argument; the LED and DOUGlas queries call it. */
char return_byte(char b) { return b; }

/* Returns its argument; called by nothing. */
uint32_t long_identity(uint32_t value) { return value; }

/* Copies so many longs with interrupts held off. */
void copy_longs_locked(uint32_t* source, uint32_t* destination, uint8_t count) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  for (uint8_t index = 0; index < count; index++) {
    destination[index] = source[index];
  }
  set_interrupt_mask(sr);
}

/* Returns its argument; the EFC queries pass pll.efc through it. */
float float_identity(float x) { return x; }

/* Closes the SCPI port; scpi_task calls it when scpi_interpret returns. */
void scpi_session_close(void) { scpi_port_close(); }

/* scpi_interpret's hook at each message's end and error, passed the
 * call and the condition reported (STATUS_NO_CONDITION at an end): 0, no
 * further error. */
uint32_t scpi_message_done(scpi_call* call, int32_t condition) { return 0; }

/* scpi_interpret's hook when the input closes: 0. */
uint32_t scpi_input_closed(scpi_call* call) { return 0; }

/* Has spoo create the SCPI task, at priority 0x19, and leaves the
 * console to it. */
void scpi_task_start(void) {
  /* spoo_run calls it with three longs; it reads the first. */
  spoo_run((spoo_function*)scpi_task_create, SCPI_TASK_PRIORITY, 0, 0);
  task_console = 0;
}
