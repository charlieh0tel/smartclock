/* The SCPI interpreter: its parser of message units, the calls to the
 * handlers, the arena and the handlers' helpers, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/engine`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  scpi_engine.h holds the parser's bodies, which the
 * installer's copies share. */

/* Fills a parser context, the word from its long's low half; nothing
 * calls it (0x26a80). */
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
 * the root (0x26ab6). */
int32_t scpi_parse_message_unit(scpi_context* parser, int16_t* reset_path) {
  return scpi_engine_parse_message_unit(
      scpi_char_classes, status_negative_conditions, parser, reset_path);
}

/* Calls a node's setter or query with the call's parameters, its
 * response and the call (0x29642). */
int32_t scpi_call_handler(scpi_call* call, void** response) {
  return scpi_engine_call_handler(call, response);
}

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them (0x29700). */
void* arena_alloc(int32_t size, scpi_arena* arena) {
  return scpi_arena_alloc(size, arena);
}

/* The bytes left in an arena, and its next free byte (0x2972c). */
void* arena_remaining(int32_t* remaining, scpi_arena* arena) {
  *remaining = arena->last - arena->next + 1;
  return arena->next;
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them (0x29742). */
void* arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  return scpi_arena_take(buffer, size, arena);
}

/* An arena's next free byte (0x2975e). */
void* arena_current(scpi_arena* arena) { return arena->next; }

/* Sets an arena's next free byte (0x29766). */
void arena_set(void* next, scpi_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot
 * (0x29770). */
void arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena_init(buffer, size, slot);
}

/* Returns its argument; the LED and DOUGlas queries call it (0x29a18). */
char return_byte(char b) { return b; }

/* Returns its argument; called by nothing (0x29a1e). */
uint32_t long_identity(uint32_t value) { return value; }

/* Copies so many longs with interrupts held off (0x29a24). */
void copy_longs_locked(uint32_t* source, uint32_t* destination, uint8_t count) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  for (uint8_t index = 0; index < count; index++) {
    destination[index] = source[index];
  }
  set_interrupt_mask(sr);
}

/* Returns its argument; the EFC queries pass pll.efc through it
 * (0x29a62). */
float float_identity(float x) { return x; }

/* The source a command's input port gives: always 0 (0x29ada). */
int32_t return_zero(scpi_port* input) { return 0; }

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x29ade). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, return_zero(call->input));
  call->query = 0;
  return 0;
}

/* Clears the SCPI input, opens the port and prompts; the console port
 * (0x29b42). */
scpi_port* scpi_session_init(void) {
  scpi_echo_length = 0;
  scpi_input_repeat = 0;
  scpi_input_end = scpi_input_line;
  scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* Closes the SCPI port; scpi_task calls it when scpi_interpret returns
 * (0x29b8a). */
void scpi_session_close(void) { scpi_port_close(); }

/* scpi_interpret's hook at each message's end and error: 0, no further
 * error (0x29e34). */
uint32_t scpi_message_done(void) { return 0; }

/* scpi_interpret's hook when the input closes: 0 (0x29e38). */
uint32_t scpi_input_closed(void) { return 0; }

/* The SCPI task: time sliced, takes a 2000-byte segment for the
 * interpreter's arena and runs the interpreter on the console port until
 * its session ends; then gives the console to pForth, at priority 0x33,
 * or enters the installer, as :SYSTem:LANGuage chose, and deletes
 * itself (0x2a04a). */
void scpi_task(void) {
  /* scpi_interpret's last argument, unread, is the long above its
   * others: the task's D2, as it saved it. */
  const uint32_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  static const uint32_t ARENA_SIZE = 2000;
  static const uint32_t ERROR_BITS = 0xff;
  static const uint32_t CONSOLE_PRIORITY = 0x33;
  void* arena;

  psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  const uint32_t error =
      psos_service_8(ARENA_SIZE, 0, 1, 0, (uint32_t*)&arena) & ERROR_BITS;

  if (error != 0) {
    printf("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  scpi_prompt_enabled = 1;
  scpi_port* port = scpi_session_init();

  if (port == NULL) {
    console_write("scpi: error opening stream\n");
    return;
  }
  scpi_interpret((int32_t)port, (int32_t)port, arena, ARENA_SIZE,
                 scpi_trees.main, scpi_trees.common, status_object, 0,
                 caller_d2);
  psos_free_segment(arena);
  port->methods->close(port);
  if (scpi_language == SCPI_LANGUAGE_PFORTH) {
    psos_priority(0, CONSOLE_PRIORITY, NULL);
    spoo_run((spoo_function*)console_init, 0, 0, 0);
  } else if (scpi_language == SCPI_LANGUAGE_INSTALL) {
    enter_installer();
  }
  psos_delete(0);
}

/* Has spoo create the SCPI task, at priority 0x19, and leaves the
 * console to it (0x12f30). */
void scpi_task_start(void) {
  static const uint32_t SCPI_TASK_PRIORITY = 0x19;

  /* spoo_run calls it with three longs; it reads the first. */
  spoo_run((spoo_function*)scpi_task_create, SCPI_TASK_PRIORITY, 0, 0);
  task_console = 0;
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (0x29fce). */
uint32_t scpi_task_create(uint32_t priority) {
  static const uint32_t ERROR_BITS = 0xff;
  /* The SCPI task's stacks in words, supervisor and user (psos.md,
   * "spawn"). */
  static const uint32_t TASK_SUPERVISOR_STACK_WORDS = 0x300;
  static const uint32_t TASK_USER_STACK_WORDS = 0x400;
  uint32_t existing;

  if ((uint8_t)psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error = psos_create("scpi", 0, priority, TASK_SUPERVISOR_STACK_WORDS,
                               TASK_USER_STACK_WORDS, 0, &task_scpi) &
                   ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return psos_start(task_scpi, scpi_task) & ERROR_BITS;
}
