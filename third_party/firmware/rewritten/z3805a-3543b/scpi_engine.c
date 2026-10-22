/* The SCPI interpreter: its parser of message units, the calls to the
 * handlers, the arena and the handlers' helpers, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/engine`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* A converter of a parameter's token into its place in the call's
 * buffer, by the token's kind (0x26c68): a number's with the node's
 * units, a keyword's with its choices, a non-decimal number's and a
 * string's with neither, a block's with the context's input and arena,
 * and an expression's, from its (, with no token. */
typedef int32_t scpi_engine_table_converter(scpi_token* token, void* call,
                                            void* value, void* table);

typedef int32_t scpi_engine_plain_converter(scpi_token* token, void* call,
                                            void* value);

typedef int32_t scpi_engine_block_converter(scpi_token* token, void* call,
                                            void* value, uint32_t word,
                                            scpi_port* input, scpi_arena* arena,
                                            uint32_t extra);

typedef int32_t scpi_engine_expression_converter(void* call, void* value,
                                                 uint32_t word,
                                                 scpi_port* input,
                                                 scpi_arena* arena);

static const int8_t scpi_engine_token_parameter_kinds[] = {
    SCPI_PARAMETER_NON_DECIMAL,
    SCPI_PARAMETER_DECIMAL,
    SCPI_PARAMETER_STRING,
    SCPI_PARAMETER_EXPRESSION,
    SCPI_NONE,
    SCPI_PARAMETER_BLOCK,
    SCPI_PARAMETER_CHARACTER,
};

/* scpi_parse_message_unit's own results that are not conditions. */
enum {
  SCPI_STATUS_NO_PARAMETERS = -100, /* not a condition: an empty message unit */
  SCPI_STATUS_SKIPPED_END =
      -101, /* not a condition: SCPI_PORT_SKIPPED_END first */
  SCPI_STATUS_UNREPORTED_GREATEST =
      -60, /* the results from which down it returns as they are */
};

/* The next token of the context's input. */
static int32_t scpi_engine_scan(scpi_context* context, scpi_token* token) {
  return scpi_scan_token(context->input, token, context->arena);
}

/* The next token, past white space. */
static int32_t scpi_engine_scan_past_white(scpi_context* context,
                                           scpi_token* token) {
  int32_t kind = scpi_engine_scan(context, token);

  return kind == SCPI_TOKEN_WHITE ? scpi_engine_scan(context, token) : kind;
}

/* Whether a token kind starts a parameter. */
static bool scpi_engine_is_parameter(int32_t kind) {
  return kind >= SCPI_TOKEN_NON_DECIMAL && kind <= SCPI_TOKEN_KEYWORD;
}

/* Converts a parameter's token into its place in the call's buffer, by
 * the converter of the node's next parameter that takes it
 * (0x26c06 to 0x26d3c). */
static int32_t scpi_engine_convert(scpi_context* context, scpi_token* token,
                                   int32_t kind, scpi_parameter** parameter,
                                   int16_t* offset) {
  if (kind == SCPI_TOKEN_DOLLAR) {
    return STATUS_SCPI_PARAMETER_DOLLAR;
  }
  int16_t parameter_kind =
      scpi_engine_token_parameter_kinds[kind - SCPI_TOKEN_NON_DECIMAL];
  int32_t error =
      scpi_parameter_match(parameter, parameter_kind, context->call, offset);

  if (error != 0) {
    return error;
  }
  scpi_call* call = context->call;
  scpi_parameter_kinds* kinds = (*parameter)->kinds;
  void* converter = kinds->converters[parameter_kind];
  void* value = call->parameters + *offset;

  switch (kind) {
    case SCPI_TOKEN_DECIMAL:
      error = ((scpi_engine_table_converter*)converter)(token, call, value,
                                                        kinds->units);
      break;
    case SCPI_TOKEN_KEYWORD:
      error = ((scpi_engine_table_converter*)converter)(token, call, value,
                                                        kinds->choices);
      break;
    case SCPI_TOKEN_NON_DECIMAL:
    case SCPI_TOKEN_STRING:
      error = ((scpi_engine_plain_converter*)converter)(token, call, value);
      break;
    case SCPI_TOKEN_BLOCK:
      error = ((scpi_engine_block_converter*)converter)(
          token, call, value, context->word, context->input, context->arena,
          context->field_1e);
      break;
    case SCPI_TOKEN_OPEN:
      error = ((scpi_engine_expression_converter*)converter)(
          call, value, context->word, context->input, context->arena);
      break;
  }
  if (error != 0) {
    return error;
  }
  *offset += kinds->size;
  *parameter = (*parameter)->next;
  return 0;
}

/* Parses one program message unit: its header, then its parameters into
 * the call's buffer, token by token; *reset_path says whether the next
 * unit starts at the root, after a newline (0x26ab6).  On an error it
 * reads to the unit's end unless the error is a command error. */
int32_t scpi_parse_message_unit(scpi_context* parser, int16_t* reset_path) {
  scpi_call* call = parser->call;
  scpi_token token;
  scpi_parameter* parameter;
  int16_t offset;
  int32_t error;
  int32_t kind;

  call->parameters = 0;
  kind = scpi_engine_scan(parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(parser, &token);
    if (kind == SCPI_TOKEN_SKIPPED_END) {
      error = STATUS_SCPI_GET_NOT_ALLOWED;
      goto failed;
    }
  }
  if (kind == SCPI_TOKEN_COLON) {
    scpi_path_reset(parser->path, parser->main_tree);
    kind = scpi_engine_scan(parser, &token);
    if (kind < SCPI_TOKEN_KEYWORD) {
      error = STATUS_SCPI_SYNTAX_ERROR;
      goto failed;
    }
  }
  if (kind >= SCPI_TOKEN_KEYWORD) {
    /* A header, the path's keywords each with a colon after it, then
     * the node's. */
    while (kind == SCPI_TOKEN_KEYWORD_COLON) {
      /* The callees pass on, and do not read, their last
       * argument, which the image gives from D5 unset. */
      error = scpi_header_step(&token.keyword, parser->path, 0);
      if (error != 0) {
        goto failed;
      }
      kind = scpi_engine_scan(parser, &token);
    }
    if (kind < SCPI_TOKEN_KEYWORD) {
      error = STATUS_SCPI_SYNTAX_ERROR;
      goto failed;
    }
    /* scpi_header_resolve and scpi_common_header_resolve keep the
     * decompiler's prototypes, which take the call, the path and the
     * arena as pointers to integers. */
    error = scpi_header_resolve(&token.keyword.length, (int32_t*)call, &offset,
                                (uint32_t*)&parameter, (uint32_t*)parser->path,
                                0, kind == SCPI_TOKEN_QUERY,
                                (int32_t*)parser->arena, 0);
  } else if (kind == SCPI_TOKEN_STAR) {
    kind = scpi_engine_scan(parser, &token);
    if (kind != SCPI_TOKEN_KEYWORD && kind != SCPI_TOKEN_QUERY) {
      error = STATUS_SCPI_SYNTAX_ERROR;
      goto failed;
    }
    error = scpi_common_header_resolve(
        &token.keyword, (int32_t*)call, (uint16_t*)&offset,
        (int32_t*)&parameter, 0, kind == SCPI_TOKEN_QUERY,
        (int32_t*)parser->arena, parser->common_list);
  } else if (kind == SCPI_TOKEN_SKIPPED_END) {
    return SCPI_STATUS_SKIPPED_END;
  } else {
    goto terminator;
  }
  if (error != 0) {
    goto failed;
  }
  /* The parameters, after white space, separated by commas. */
  kind = scpi_engine_scan(parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(parser, &token);
    if (kind != SCPI_TOKEN_END && kind != SCPI_TOKEN_SEMICOLON) {
      if (!scpi_engine_is_parameter(kind)) {
        error = STATUS_SCPI_SYNTAX_ERROR;
        goto failed;
      }
      for (;;) {
        error = scpi_engine_convert(parser, &token, kind, &parameter, &offset);
        if (error != 0) {
          goto failed;
        }
        kind = scpi_engine_scan_past_white(parser, &token);
        if (kind != SCPI_TOKEN_COMMA) {
          break;
        }
        kind = scpi_engine_scan_past_white(parser, &token);
        if (!scpi_engine_is_parameter(kind)) {
          break;
        }
      }
    }
  }
terminator:
  if (kind == SCPI_TOKEN_END) {
    *reset_path = 1;
  } else if (kind == SCPI_TOKEN_SEMICOLON) {
    *reset_path = 0;
  } else {
    error = STATUS_SCPI_INVALID_SEPARATOR;
    goto failed;
  }
  if (call->parameters == 0) {
    return SCPI_STATUS_NO_PARAMETERS;
  }
  if (parameter != 0) {
    error = scpi_parameters_finish(&parameter, call, &offset);
    if (error != 0) {
      goto failed;
    }
  }
  return 0;

failed:
  /* A lexer's error is the result in place of what it broke. */
  if (kind < SCPI_TOKEN_SEMICOLON || kind > SCPI_TOKEN_KEYWORD_COLON) {
    error = kind;
  }
  if (error <= SCPI_STATUS_UNREPORTED_GREATEST ||
      error == STATUS_SCPI_QUERY_UNTERMINATED ||
      error == STATUS_SCPI_QUERY_INTERRUPTED) {
    return error;
  }
  if (condition_is_command_error(error)) {
    if (kind == SCPI_TOKEN_END || kind == SCPI_TOKEN_SKIPPED_END) {
      parser->input->methods->unread(parser->input);
    }
    return error;
  }
  while (kind > SCPI_TOKEN_SKIPPED_END || kind < SCPI_TOKEN_SEMICOLON) {
    kind = scpi_engine_scan(parser, &token);
  }
  *reset_path = kind != SCPI_TOKEN_SEMICOLON;
  return error;
}

/* A size rounded up to even, as the arena hands out room. */
static uint32_t scpi_engine_even(int32_t size) { return (size + 1U) & ~1U; }

/* Calls a node's setter or query with the call's parameters, its
 * response and the call, first taking room in the arena for a query's
 * response (0x29642). */
int32_t scpi_call_handler(scpi_call* call, void** response) {
  scpi_handler_function* handler = call->node->handlers[call->query].function;

  if (call->query != 0) {
    *response = arena_alloc(call->node->response_size, call->arena);
    if (*response == 0) {
      return STATUS_SCPI_NO_MEMORY;
    }
  }
  return handler(call->parameters, *response, call);
}

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them (0x29700). */
void* arena_alloc(int32_t size, scpi_arena* arena) {
  uint8_t* start = arena->next;
  uint8_t* end = start + scpi_engine_even(size);

  if (arena->last < end) {
    return 0;
  }
  arena->next = end;
  return start;
}

/* The bytes left in an arena, and its next free byte (0x2972c). */
void* arena_remaining(int32_t* remaining, scpi_arena* arena) {
  *remaining = arena->last - arena->next + 1;
  return arena->next;
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them (0x29742). */
void* arena_take(void* buffer, int32_t size, scpi_arena* arena) {
  uint8_t* start = buffer;

  arena->next = start + scpi_engine_even(size);
  return start;
}

/* An arena's next free byte (0x2975e). */
void* arena_current(scpi_arena* arena) { return arena->next; }

/* Sets an arena's next free byte (0x29766). */
void arena_set(void* next, scpi_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot
 * (0x29770). */
void arena_init(void* buffer, uint32_t size, scpi_arena** slot) {
  scpi_arena* arena = buffer;
  uint8_t* bytes = buffer;

  *slot = arena;
  arena->next = bytes + sizeof *arena;
  arena->last = bytes + (size & ~1U) - 1;
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

/* Returns 0; most SCPI query handlers call it (0x29ada). */
int32_t return_zero(void) { return 0; }

/* Reports a query's failure, the condition code, for the source
 * return_zero gives, and empties its text response (0x29ade). */
int32_t scpi_query_failed(status_condition_code code, void* response) {
  scpi_engine_string_value* text = response;

  status_report_from(code, return_zero());
  text->length = 0;
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
