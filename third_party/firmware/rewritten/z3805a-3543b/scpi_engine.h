/* What the SCPI interpreter's parser shares with the installer's copy
 * of it (scpi_engine.c, installer_scpi_engine.c): its bodies, which both
 * copies' routines call, each with its image's tables.  The exported C
 * includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* The other headers' bodies it calls, which the exported C includes
 * after this one, the headers going in sorted order. */
static int32_t scpi_engine_scan_token(const scpi_char_class* classes,
                                      scpi_port* input, scpi_token* token,
                                      scpi_arena* arena);
static int32_t scpi_parameters_match(scpi_parameter** next, int32_t kind,
                                     scpi_call* call, int16_t* offset);
static uint32_t scpi_parameters_left_out(scpi_parameter** next, scpi_call* call,
                                         int16_t* offset);
static void scpi_tree_path_reset(scpi_path* path, scpi_child_list* list);
static uint32_t scpi_tree_header_step(scpi_keyword* keyword, scpi_path* header,
                                      void* unused);
static uint32_t scpi_tree_header_resolve(scpi_keyword* keyword, scpi_call* call,
                                         int16_t* offset,
                                         scpi_parameter** parameters,
                                         const scpi_path* header, int16_t query,
                                         scpi_arena* arena, void* unused);
static uint32_t scpi_tree_common_header_resolve(
    scpi_keyword* keyword, scpi_call* call, int16_t* offset,
    scpi_parameter** parameters, uint16_t unused, int16_t query,
    scpi_arena* arena, scpi_child_list* list);
static uint32_t status_model_command_error(const status_condition* negatives,
                                           int32_t condition);

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
static int32_t scpi_engine_scan(const scpi_char_class* classes,
                                scpi_context* context, scpi_token* token) {
  return scpi_engine_scan_token(classes, context->input, token, context->arena);
}

/* The next token, past white space. */
static int32_t scpi_engine_scan_past_white(const scpi_char_class* classes,
                                           scpi_context* context,
                                           scpi_token* token) {
  int32_t kind = scpi_engine_scan(classes, context, token);

  return kind == SCPI_TOKEN_WHITE ? scpi_engine_scan(classes, context, token)
                                  : kind;
}

/* Whether a token kind starts a parameter. */
static bool scpi_engine_is_parameter(int32_t kind) {
  return kind >= SCPI_TOKEN_NON_DECIMAL && kind <= SCPI_TOKEN_KEYWORD;
}

/* Converts a parameter's token into its place in the call's buffer, by
 * the converter of the node's next parameter that takes it
 * (0x26c06 to 0x26d3c in the primary). */
static int32_t scpi_engine_convert(scpi_context* context, scpi_token* token,
                                   int32_t kind, scpi_parameter** parameter,
                                   int16_t* offset) {
  if (kind == SCPI_TOKEN_DOLLAR) {
    return STATUS_SCPI_PARAMETER_DOLLAR;
  }
  int16_t parameter_kind =
      scpi_engine_token_parameter_kinds[kind - SCPI_TOKEN_NON_DECIMAL];
  int32_t error =
      scpi_parameters_match(parameter, parameter_kind, context->call, offset);

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

/* scpi_context_init's and its installer copy's body: fills a parser
 * context, the word from its long's low half. */
static int32_t scpi_engine_context_init(scpi_context* context, scpi_port* input,
                                        uint32_t word, scpi_path* path,
                                        scpi_call* call, scpi_arena* arena,
                                        uint32_t field_1e,
                                        scpi_child_list* main_tree,
                                        scpi_child_list* common_list) {
  context->input = input;
  context->word = word;
  context->path = path;
  context->call = call;
  context->arena = arena;
  context->field_1e = field_1e;
  context->main_tree = main_tree;
  context->common_list = common_list;
  return 0;
}

/* scpi_parse_message_unit's and its installer copy's body: parses one
 * program message unit: its header, then its parameters into
 * the call's buffer, token by token; *reset_path says whether the next
 * unit starts at the root, after a newline.  On an error it
 * reads to the unit's end unless the error is a command error.  classes
 * and negatives are its image's tables. */
static int32_t scpi_engine_parse_message_unit(const scpi_char_class* classes,
                                              const status_condition* negatives,
                                              scpi_context* parser,
                                              int16_t* reset_path) {
  scpi_call* call = parser->call;
  scpi_token token;
  scpi_parameter* parameter;
  int16_t offset;
  int32_t error;
  int32_t kind;

  call->parameters = 0;
  kind = scpi_engine_scan(classes, parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(classes, parser, &token);
    if (kind == SCPI_TOKEN_SKIPPED_END) {
      error = STATUS_SCPI_GET_NOT_ALLOWED;
      goto failed;
    }
  }
  if (kind == SCPI_TOKEN_COLON) {
    scpi_tree_path_reset(parser->path, parser->main_tree);
    kind = scpi_engine_scan(classes, parser, &token);
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
      error = scpi_tree_header_step(&token.keyword, parser->path, 0);
      if (error != 0) {
        goto failed;
      }
      kind = scpi_engine_scan(classes, parser, &token);
    }
    if (kind < SCPI_TOKEN_KEYWORD) {
      error = STATUS_SCPI_SYNTAX_ERROR;
      goto failed;
    }
    error = scpi_tree_header_resolve(&token.keyword, call, &offset, &parameter,
                                     parser->path, kind == SCPI_TOKEN_QUERY,
                                     parser->arena, 0);
  } else if (kind == SCPI_TOKEN_STAR) {
    kind = scpi_engine_scan(classes, parser, &token);
    if (kind != SCPI_TOKEN_KEYWORD && kind != SCPI_TOKEN_QUERY) {
      error = STATUS_SCPI_SYNTAX_ERROR;
      goto failed;
    }
    error = scpi_tree_common_header_resolve(
        &token.keyword, call, &offset, &parameter, 0, kind == SCPI_TOKEN_QUERY,
        parser->arena, parser->common_list);
  } else if (kind == SCPI_TOKEN_SKIPPED_END) {
    return SCPI_STATUS_SKIPPED_END;
  } else {
    goto terminator;
  }
  if (error != 0) {
    goto failed;
  }
  /* The parameters, after white space, separated by commas. */
  kind = scpi_engine_scan(classes, parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(classes, parser, &token);
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
        kind = scpi_engine_scan_past_white(classes, parser, &token);
        if (kind != SCPI_TOKEN_COMMA) {
          break;
        }
        kind = scpi_engine_scan_past_white(classes, parser, &token);
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
    error = scpi_parameters_left_out(&parameter, call, &offset);
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
  if (status_model_command_error(negatives, error)) {
    if (kind == SCPI_TOKEN_END || kind == SCPI_TOKEN_SKIPPED_END) {
      parser->input->methods->unread(parser->input);
    }
    return error;
  }
  while (kind > SCPI_TOKEN_SKIPPED_END || kind < SCPI_TOKEN_SEMICOLON) {
    kind = scpi_engine_scan(classes, parser, &token);
  }
  *reset_path = kind != SCPI_TOKEN_SEMICOLON;
  return error;
}

/* scpi_call_handler's and its installer copy's body: calls a node's
 * setter or query with the call's parameters, its response and the
 * call, first taking room in the arena for a query's response. */
static int32_t scpi_engine_call_handler(scpi_call* call, void** response) {
  scpi_handler_function* handler = call->node->handlers[call->query].function;

  if (call->query != 0) {
    *response = scpi_arena_alloc(call->node->response_size, call->arena);
    if (*response == 0) {
      return STATUS_SCPI_NO_MEMORY;
    }
  }
  return handler(call->parameters, *response, call);
}

/* The output's value after a message's responses, above a character's,
 * which the port's put writes as CR LF (0x29544). */
static const int32_t SCPI_ENGINE_RESPONSE_END = 0x10a;

/* The call record scpi_interpret keeps on its stack is smartclock.h's
 * scpi_call, to its last field (0x29344). */
_Static_assert(__builtin_offsetof(scpi_call, last_argument) == 0x36,
               "scpi_call is the image's call record");

/* What scpi_interpret calls, the primary's routines or the installer's
 * copies of them, and the codes it signals its ports with: a message's
 * first response's begin, a message's end and the input's end. */
typedef struct scpi_engine_routines {
  void (*arena_init)(void* buffer, uint32_t size, scpi_arena** slot);
  void* (*arena_current)(scpi_arena* arena);
  void (*arena_set)(void* next, scpi_arena* arena);
  scpi_node* (*trg_defined)(scpi_child_list* list, uint16_t* no_setter);
  int32_t (*context_init)(scpi_context* context, scpi_port* input,
                          uint32_t word, scpi_path* path, scpi_call* call,
                          scpi_arena* arena, uint32_t field_1e,
                          scpi_child_list* main_tree,
                          scpi_child_list* common_list);
  void (*path_reset)(scpi_path* path, scpi_child_list* list);
  void (*skip_to_terminator)(scpi_port* input);
  int32_t (*parse_message_unit)(scpi_context* parser, int16_t* reset_path);
  int32_t (*call_handler)(scpi_call* call, void** response);
  int32_t (*format_response)(scpi_port* output, scpi_response_item* items,
                             scpi_call* call);
  int16_t (*clear_bit)(status_model* status, int32_t condition);
  uint32_t (*is_command_error)(int32_t condition);
  uint32_t (*message_done)(scpi_call* call, int32_t condition);
  uint32_t (*input_closed)(scpi_call* call);
  uint32_t* response_begin_code;
  uint32_t* message_end_code;
  uint32_t* input_end_code;
} scpi_engine_routines;

/* Reports a condition to the status model; whether it is a command
 * error, after which the rest of the message is skipped. */
static bool scpi_engine_report(const scpi_engine_routines* routines,
                               status_model* status, int32_t condition) {
  routines->clear_bit(status, condition);
  return routines->is_command_error(condition) != 0;
}

/* A port's signal method on a code. */
static int32_t scpi_engine_signal(scpi_port* port, uint32_t* code) {
  return port->methods->signal(port, code);
}

/* Writes a query's response, after the begin signal or a ';', unless an
 * indefinite one came before it in the message; false when formatting
 * it fails, which it reports (0x29436 to 0x294ca). */
static bool scpi_engine_respond(const scpi_engine_routines* routines,
                                scpi_call* call) {
  scpi_port* output = call->output;

  if (call->responding == SCPI_CALL_INDEFINITE) {
    routines->clear_bit(call->status, STATUS_SCPI_QUERY_AFTER_INDEFINITE);
    return true;
  }
  if (call->responding == SCPI_CALL_NO_RESPONSE) {
    call->responding = SCPI_CALL_RESPONDING;
    scpi_engine_signal(output, routines->response_begin_code);
  } else {
    output->methods->put(output, ';');
  }
  const int32_t formatted =
      routines->format_response(output, call->node->response_items, call);

  if (formatted == SCPI_STATUS_INDEFINITE_RESPONSE) {
    call->responding = SCPI_CALL_INDEFINITE;
  } else if (formatted != 0) {
    routines->clear_bit(call->status, formatted);
    routines->message_done(call, formatted);
    return false;
  }
  return true;
}

/* At a message's end: the done hook's and the end signal's errors
 * reported, and the responses ended with SCPI_ENGINE_RESPONSE_END;
 * whether a command error among them has the next message skipped to
 * its end (0x294f2 to 0x2957e). */
static bool scpi_engine_message_end(const scpi_engine_routines* routines,
                                    scpi_call* call) {
  scpi_port* output = call->output;
  status_model* status = call->status;
  bool skip = false;
  const int32_t done = routines->message_done(call, STATUS_NO_CONDITION);
  const int32_t signaled =
      scpi_engine_signal(output, routines->message_end_code);

  if (signaled == 0) {
    if (call->responding != SCPI_CALL_NO_RESPONSE) {
      output->methods->put(output, SCPI_ENGINE_RESPONSE_END);
    }
  } else if (signaled != STATUS_SCPI_QUERY_DEADLOCKED) {
    skip = scpi_engine_report(routines, status, signaled);
  }
  if (done != 0 && scpi_engine_report(routines, status, done)) {
    skip = true;
  }
  return skip;
}

/* scpi_interpret's and its installer copy's body: parses each message
 * unit of the input, calls its handler, writes a query's response and
 * reports what fails, until a handler ends the session or the port
 * fails (0x292d6).  handled is D3 as the caller left it, which holds
 * the last handler's result, and which the image tests for the
 * session's end after a message unit it called no handler for, the
 * first too (0x29622).  The context's field_1e is a local the image
 * never sets, and the result a handler's end of the session returns
 * the call record's, also unset (docs/firmware/quirks.md). */
static uint32_t scpi_engine_interpret(const scpi_engine_routines* routines,
                                      int32_t handled, scpi_port* input,
                                      scpi_port* output, void* buffer,
                                      uint32_t size, scpi_child_list* main_tree,
                                      scpi_child_list* common_list,
                                      status_model* status,
                                      uint32_t last_argument) {
  volatile uint32_t unset_field_1e;
  scpi_arena* arena;
  scpi_path path;
  scpi_context context;
  scpi_call call;
  uint16_t trigger_query;
  int16_t reset_path = 1;
  bool skip = false;

  routines->arena_init(buffer, size, &arena);
  void* const arena_start = routines->arena_current(arena);
  scpi_node* const trigger = routines->trg_defined(common_list, &trigger_query);

  call.input = input;
  call.output = output;
  call.status = status;
  call.arena = arena;
  call.main_tree = main_tree;
  call.common_list = common_list;
  call.field_34 = 0;
  call.last_argument = last_argument;
  routines->context_init(&context, input, 0, &path, &call, arena,
                         unset_field_1e, main_tree, common_list);
  for (;;) {
    if (reset_path) {
      call.responding = SCPI_CALL_NO_RESPONSE;
      routines->path_reset(&path, main_tree);
      routines->arena_set(arena_start, arena);
    }
    if (skip) {
      routines->skip_to_terminator(input);
      skip = false;
    }
    int32_t parsed = routines->parse_message_unit(&context, &reset_path);
    bool calling = parsed == 0;

    for (;;) {
      if (calling) {
        handled = routines->call_handler(&call, (void**)&call.response);
        if (handled == SCPI_HANDLER_CALL_TRIGGER) {
          call.node = trigger;
          call.query = trigger_query;
          continue;
        }
        if (handled == SCPI_HANDLER_RESET_PATH) {
          routines->path_reset(&path, main_tree);
        } else if (handled != 0 && handled != SCPI_HANDLER_SESSION_END) {
          if (scpi_engine_report(routines, status, handled) && !reset_path) {
            skip = true;
          }
          call.query = 0;
        }
        if (call.query != 0 && !scpi_engine_respond(routines, &call)) {
          reset_path = 1;
          break;
        }
        routines->arena_set(arena_start, arena);
      }
      if (parsed == 0 || parsed == SCPI_STATUS_NO_PARAMETERS) {
        if (reset_path && scpi_engine_message_end(routines, &call)) {
          skip = true;
        }
        break;
      }
      handled = 0;
      if (parsed == SCPI_STATUS_SKIPPED_END) {
        /* the unit's handler is the trigger's (0x29592) */
        parsed = 0;
        reset_path = 1;
        call.node = trigger;
        call.query = trigger_query;
        calling = true;
        continue;
      }
      if (parsed == SCPI_STATUS_INPUT_CLOSED) {
        scpi_engine_signal(input, routines->input_end_code);
        routines->input_closed(&call);
      } else if (parsed == SCPI_STATUS_PORT_FAILED) {
        handled = SCPI_HANDLER_SESSION_END;
        call.result = -1;
      } else {
        if (scpi_engine_report(routines, status, parsed)) {
          skip = true;
          routines->message_done(&call, parsed);
          reset_path = 1;
        }
        if (parsed != STATUS_SCPI_QUERY_UNTERMINATED &&
            parsed != STATUS_SCPI_QUERY_INTERRUPTED) {
          break;
        }
        scpi_engine_signal(input, routines->input_end_code);
      }
      reset_path = 1;
      break;
    }
    if (handled == SCPI_HANDLER_SESSION_END) {
      return call.result;
    }
  }
}
