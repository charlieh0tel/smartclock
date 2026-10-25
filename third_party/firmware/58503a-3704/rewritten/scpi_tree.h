/* What the SCPI tree's lookups share with the installer's copy of
 * them (scpi_tree.c, installer_scpi_tree.c): their bodies, which both
 * copies' routines call.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* Compares a token with a keyword pair, its short form, a NUL and the
 * rest of its long form: 0 when the token is either form, else -1 or 1
 * as it sorts before or after. */
static int16_t scpi_tree_keyword_compare(char* token, int32_t length,
                                         char* keyword) {
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
 *. */
static scpi_node* scpi_tree_node_find(scpi_child_list* children,
                                      scpi_keyword* keyword) {
  int16_t low = 0;
  int16_t high = children->count - 1;

  while (low <= high) {
    int16_t middle = (high + low) >> 1;
    scpi_node* node = children->children[middle];
    int16_t order = scpi_tree_keyword_compare(keyword->text, keyword->length,
                                              node->keywords);

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
static void scpi_tree_path_add(scpi_path* path, uint16_t word) {
  path->words[path->count++] = word;
}

/* Adds a node's word to the path, when it has one. */
static void scpi_tree_path_add_node(scpi_path* path, scpi_node* node) {
  if (node->path_word != SCPI_NONE) {
    scpi_tree_path_add(path, node->path_word);
  }
}

/* A keyword's node in the path's list, or else down the default
 * children, adding their words to the path; 0 for none, the path as it
 * was. */
static scpi_node* scpi_tree_node_find_with_defaults(scpi_path* header,
                                                    scpi_keyword* keyword,
                                                    void* unused) {
  scpi_node* node = scpi_tree_node_find(header->list, keyword);
  int16_t count = header->count;

  for (scpi_child_list* list = header->list; node == 0;) {
    if (list->default_child == SCPI_NONE) {
      header->count = count;
      return 0;
    }
    scpi_node* child = list->children[list->default_child];

    scpi_tree_path_add_node(header, child);
    list = child->children;
    if (list == 0) {
      header->count = count;
      return 0;
    }
    node = scpi_tree_node_find(list, keyword);
  }
  return node;
}

/* A keyword's node with its numeric suffix taken off and added to the
 * path, the keyword ending in '*' as numbered nodes' do; or, for a
 * suffix of 1, the plain keyword's.  The suffix is the digits
 * at its end, read from after the character before them, or from that
 * character itself when it sorts before '0' (0x26efe to 0x26f0a). */
static scpi_node* scpi_tree_node_find_numbered(scpi_path* header,
                                               scpi_keyword* name,
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
  scpi_node* node = scpi_tree_node_find_with_defaults(header, name, unused);

  if (node != 0) {
    scpi_tree_path_add(header, suffix_digits < 1 ? 1 : suffix);
    return node;
  }
  if (suffix_digits < 1 || suffix != 1) {
    return 0;
  }
  name->length = length;
  node = scpi_tree_node_find_with_defaults(header, name, unused);
  if (node == 0) {
    return 0;
  }
  scpi_tree_path_add_node(header, node);
  return node;
}

/* scpi_header_resolve's and its installer copy's body: resolves a
 * command header's last keyword, through the default children and
 * numeric suffixes, to its node, and on down the default children to one
 * with the handler, 0 the setter and 1 the query; takes the arena's room
 * for its parameters, the path's words first, and gives back the
 * handler's parameter list and the offset past the words.  It works on
 * a copy of the path. */
static uint32_t scpi_tree_header_resolve(scpi_keyword* keyword, scpi_call* call,
                                         int16_t* offset,
                                         scpi_parameter** parameters,
                                         const scpi_path* header, int16_t query,
                                         scpi_arena* arena, void* unused) {
  scpi_path path;

  path.list = header->list;
  path.count = header->count;
  for (int16_t index = 0; index < path.count; index++) {
    path.words[index] = header->words[index];
  }
  scpi_node* node = scpi_tree_node_find_with_defaults(&path, keyword, unused);

  if (node != 0) {
    scpi_tree_path_add_node(&path, node);
  } else {
    node = scpi_tree_node_find_numbered(&path, keyword, unused);
    if (node == 0) {
      return STATUS_SCPI_UNDEFINED_HEADER;
    }
  }
  while (node->handlers[query].function == 0) {
    scpi_child_list* list = node->children;

    if (list == 0 || list->default_child == SCPI_NONE) {
      return STATUS_SCPI_HEADER_LEAF;
    }
    node = list->children[list->default_child];
    scpi_tree_path_add_node(&path, node);
  }
  call->parameters =
      scpi_arena_alloc(node->handlers[query].parameters_size, arena);
  if (call->parameters == 0) {
    return STATUS_SCPI_NO_MEMORY;
  }
  call->query = query;
  call->node = node;
  *parameters = node->handlers[query].parameters;
  *offset = 0;
  for (int16_t index = 0; index < path.count; index++) {
    *(uint16_t*)(call->parameters + *offset) = path.words[index];
    *offset += sizeof(uint16_t);
  }
  return 0;
}

/* Moves the path one keyword down a compound header. */
static uint32_t scpi_tree_header_step(scpi_keyword* keyword, scpi_path* header,
                                      void* unused) {
  scpi_node* node = scpi_tree_node_find_with_defaults(header, keyword, unused);

  if (node != 0) {
    scpi_tree_path_add_node(header, node);
  } else {
    node = scpi_tree_node_find_numbered(header, keyword, unused);
    if (node == 0) {
      return STATUS_SCPI_UNDEFINED_HEADER;
    }
  }
  header->list = node->children;
  return header->list == 0 ? STATUS_SCPI_HEADER_LEAF : 0;
}

/* Sets a header's path back to a root list. */
static void scpi_tree_path_reset(scpi_path* path, scpi_child_list* list) {
  path->list = list;
  path->count = 0;
}

/* Resolves a common command's header in its list to its node and
 * handler, 0 the setter and 1 the query, and takes the arena's room for
 * its parameters; -33, as past a leaf, for no such handler. */
static uint32_t scpi_tree_common_header_resolve(
    scpi_keyword* keyword, scpi_call* call, int16_t* offset,
    scpi_parameter** parameters, uint16_t unused, int16_t query,
    scpi_arena* arena, scpi_child_list* list) {
  scpi_node* node = scpi_tree_node_find(list, keyword);

  if (node == 0) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  scpi_handler* handler = &node->handlers[query];

  if (handler->function == 0) {
    return STATUS_SCPI_HEADER_LEAF;
  }
  call->parameters = scpi_arena_alloc(handler->parameters_size, arena);
  if (call->parameters == 0) {
    return STATUS_SCPI_NO_MEMORY;
  }
  call->query = query;
  call->node = node;
  *parameters = handler->parameters;
  *offset = 0;
  return 0;
}

/* Looks _TRG up in a list, and whether its node has no setter
 *. */
static scpi_node* scpi_tree_trg_defined(scpi_child_list* list,
                                        uint16_t* no_setter) {
  scpi_keyword keyword;
  char* from = "_TRG";
  char* to = keyword.text;

  while ((*to++ = *from++) != 0) {
  }
  keyword.length = scpi_engine_length(keyword.text);
  scpi_node* node = scpi_tree_node_find(list, &keyword);

  if (node != 0) {
    *no_setter = node->handlers[0].function == 0;
  }
  return node;
}
