/* The functions of nv.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/nv.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Erases both NV register slots; the clear_nv word's. */
void clear_nv(void) {
  nv_slot_erase(0);
  nv_slot_erase(1);
}
