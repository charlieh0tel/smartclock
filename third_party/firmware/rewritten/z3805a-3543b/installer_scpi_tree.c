/* The installer's copies of the SCPI tree's lookups
 * (`applications/scpi/tree`; scpi_tree.c has the primary's), whose
 * bodies, scpi_tree.h's, both share.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* A keyword's node in a child list, by bisection; NULL for none *
 * (installer::0x105858). */
scpi_node* installer_scpi_node_find(scpi_child_list* children,
                                    scpi_keyword* keyword) {
  return scpi_tree_node_find(children, keyword);
}

/* A keyword's node in the path's list, or else down the default children,
 * adding their words to the path; 0 for none, the path as it was
 * (installer::0x1058d0). */
scpi_node* installer_scpi_node_find_with_defaults(scpi_path* header,
                                                  scpi_keyword* keyword,
                                                  void* unused) {
  return scpi_tree_node_find_with_defaults(header, keyword, unused);
}

/* A keyword's node with its numeric suffix taken off and added to the path,
 * the keyword ending in '*' as numbered nodes' do; or, for a suffix of 1, the
 * plain keyword's (installer::0x105942). */
scpi_node* installer_scpi_node_find_numbered(scpi_path* header,
                                             scpi_keyword* name, void* unused) {
  return scpi_tree_node_find_numbered(header, name, unused);
}

/* Moves the path one keyword down a compound header (installer::0x105b4c). */
uint32_t installer_scpi_header_step(scpi_keyword* keyword, scpi_path* header,
                                    void* unused) {
  return scpi_tree_header_step(keyword, header, unused);
}

/* Sets a header's path back to a root list (installer::0x105bac). */
void installer_scpi_path_reset(scpi_path* path, scpi_child_list* list) {
  scpi_tree_path_reset(path, list);
}

/* Resolves a common command's header in its list to its node and handler, 0
 * the setter and 1 the query, and takes the arena's room for its parameters;
 * -33, as past a leaf, for no such handler (installer::0x105bbe). */
uint32_t installer_scpi_common_header_resolve(scpi_keyword* keyword,
                                              scpi_call* call, int16_t* offset,
                                              scpi_parameter** parameters,
                                              uint16_t unused, int16_t query,
                                              scpi_arena* arena,
                                              scpi_child_list* list) {
  return scpi_tree_common_header_resolve(keyword, call, offset, parameters,
                                         unused, query, arena, list);
}

/* Looks _TRG up in a list, and whether its node has no setter *
 * (installer::0x105c2c). */
scpi_node* installer_scpi_trg_defined(scpi_child_list* list,
                                      uint16_t* no_setter) {
  return scpi_tree_trg_defined(list, no_setter);
}

/* Resolves a command header to its node and handler, through defaults
 * and numeric suffixes, and takes room for its parameters
 * (installer::0x105a2e). */
uint32_t installer_scpi_header_resolve(scpi_keyword* keyword, scpi_call* call,
                                       int16_t* offset,
                                       scpi_parameter** parameters,
                                       scpi_path* path, uint16_t unused,
                                       int16_t query, scpi_arena* arena,
                                       void* unused_2) {
  return scpi_tree_header_resolve(keyword, call, offset, parameters, path,
                                  query, arena, unused_2);
}
