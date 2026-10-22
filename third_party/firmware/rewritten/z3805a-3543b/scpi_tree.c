/* SCPI keyword lookup and header paths, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/tree`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* Compares a token with a keyword pair, its short form, a NUL and the
 * rest of its long form: 0 when the token is either form, else -1 or 1
 * as it sorts before or after (0x27b62). */
int16_t scpi_keyword_compare(char* token, int32_t length, char* keyword) {
  bool short_form = true;

  for (int16_t index = 0; index < (int16_t)length;
       index++, token++, keyword++) {
    if (*keyword < *token) {
      if (*keyword != 0 || !short_form) {
        return 1;
      }
      keyword++;
      if (*keyword < *token) {
        return 1;
      }
      short_form = false;
    }
    if (*token < *keyword) {
      return -1;
    }
  }
  return *keyword == 0 ? 0 : -1;
}

/* A keyword's node in a child list, by bisection; NULL for none
 * (0x27baa). */
scpi_node* scpi_node_find(scpi_child_list* children, scpi_keyword* keyword) {
  int16_t low = 0;
  int16_t high = children->count - 1;

  while (low <= high) {
    int16_t middle = (high + low) >> 1;
    scpi_node* node = children->children[middle];
    int16_t order =
        scpi_keyword_compare(keyword->text, keyword->length, node->keywords);

    if (order == 0) {
      return node;
    }
    if (order > 0) {
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  return 0;
}

/* Adds a word to a header's path. */
static void scpi_engine_path_add(scpi_path* path, uint16_t word) {
  path->words[path->count++] = word;
}

/* Adds a node's word to the path, when it has one. */
static void scpi_engine_path_add_node(scpi_path* path, scpi_node* node) {
  if (node->path_word != SCPI_NONE) {
    scpi_engine_path_add(path, node->path_word);
  }
}

/* A keyword's node in the path's list, or else down the default
 * children, adding their words to the path; 0 for none, the path as it
 * was (0x27c22). */
scpi_node* scpi_node_find_with_defaults(scpi_path* header,
                                        scpi_keyword* keyword, void* unused) {
  scpi_node* node = scpi_node_find(header->list, keyword);
  int16_t count = header->count;

  for (scpi_child_list* list = header->list; node == 0;) {
    if (list->default_child == SCPI_NONE) {
      header->count = count;
      return 0;
    }
    scpi_node* child = list->children[list->default_child];

    scpi_engine_path_add_node(header, child);
    list = child->children;
    if (list == 0) {
      header->count = count;
      return 0;
    }
    node = scpi_node_find(list, keyword);
  }
  return node;
}

/* A keyword's node with its numeric suffix taken off and added to the
 * path, the keyword ending in '*' as numbered nodes' do; or, for a
 * suffix of 1, the plain keyword's (0x27c94).  The suffix is the digits
 * at its end, read from after the character before them, or from that
 * character itself when it sorts before '0' (0x27caa to 0x27cb6). */
scpi_node* scpi_node_find_numbered(scpi_path* header, scpi_keyword* name,
                                   void* unused) {
  static const char NUMBERED = '*';
  static const int32_t SUFFIX_MOST = 0xffff;
  int16_t length = name->length;
  char* last = &name->text[length - 1];
  char* before;

  for (;;) {
    before = last;
    if (*last > '9') {
      break;
    }
    before = last - 1;
    if (*last < '0') {
      break;
    }
    length--;
    last = before;
  }
  int16_t suffix_digits = name->length - length;
  int32_t suffix = 0;

  for (int16_t left = suffix_digits; left > 0; left--) {
    suffix = *++before + suffix * SCPI_NUMBER_BASE - '0';
    if (suffix > SUFFIX_MOST) {
      return 0;
    }
  }
  name->text[length] = NUMBERED;
  name->length = length + 1;
  scpi_node* node = scpi_node_find_with_defaults(header, name, unused);

  if (node != 0) {
    scpi_engine_path_add(header, suffix_digits < 1 ? 1 : suffix);
    return node;
  }
  if (suffix_digits < 1 || suffix != 1) {
    return 0;
  }
  name->length = length;
  node = scpi_node_find_with_defaults(header, name, unused);
  if (node == 0) {
    return 0;
  }
  scpi_engine_path_add_node(header, node);
  return node;
}

/* Moves the path one keyword down a compound header (0x27e9e). */
uint32_t scpi_header_step(scpi_keyword* keyword, scpi_path* header,
                          void* unused) {
  scpi_node* node = scpi_node_find_with_defaults(header, keyword, unused);

  if (node != 0) {
    scpi_engine_path_add_node(header, node);
  } else {
    node = scpi_node_find_numbered(header, keyword, unused);
    if (node == 0) {
      return STATUS_SCPI_UNDEFINED_HEADER;
    }
  }
  header->list = node->children;
  return header->list == 0 ? STATUS_SCPI_HEADER_LEAF : 0;
}

/* Sets a header's path back to a root list (0x27efe). */
void scpi_path_reset(scpi_path* path, scpi_child_list* list) {
  path->list = list;
  path->count = 0;
}

/* Resolves a common command's header in its list to its node and
 * handler, 0 the setter and 1 the query, and takes the arena's room for
 * its parameters; -33, as past a leaf, for no such handler (0x27f10).
 * The call is a scpi_call, the parameters' slot a scpi_parameter's. */
uint32_t scpi_common_header_resolve(scpi_keyword* keyword, int32_t* call,
                                    uint16_t* offset, int32_t* parameters,
                                    uint16_t unused, int16_t query,
                                    scpi_arena* arena, scpi_child_list* list) {
  scpi_call* record = (scpi_call*)call;
  scpi_node* node = scpi_node_find(list, keyword);

  if (node == 0) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  scpi_handler* handler = &node->handlers[query];

  if (handler->function == 0) {
    return STATUS_SCPI_HEADER_LEAF;
  }
  record->parameters = arena_alloc(handler->parameters_size, arena);
  if (record->parameters == 0) {
    return STATUS_SCPI_NO_MEMORY;
  }
  record->query = query;
  record->node = node;
  *(scpi_parameter**)parameters = handler->parameters;
  *offset = 0;
  return 0;
}

/* Looks _TRG up in a list, and whether its node has no setter
 * (0x27f7e). */
scpi_node* scpi_trg_defined(scpi_child_list* list, uint16_t* no_setter) {
  scpi_keyword keyword;
  char* from = "_TRG";
  char* to = keyword.text;

  while ((*to++ = *from++) != 0) {
  }
  keyword.length = scpi_engine_length(keyword.text);
  scpi_node* node = scpi_node_find(list, &keyword);

  if (node != 0) {
    *no_setter = node->handlers[0].function == 0;
  }
  return node;
}
