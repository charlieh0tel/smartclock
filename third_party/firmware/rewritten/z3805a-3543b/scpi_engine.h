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
