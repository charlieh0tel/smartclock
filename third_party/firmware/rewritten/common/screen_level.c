/* The status screen's signal level, the same in each image that includes
 * this file (docs/firmware/rewrite.md, "Shared C", "A file for some
 * images").  An image whose screen prints no such level leaves the file
 * out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* A signal level as "%3d", or blanks when negative. */
void format_level(int16_t level, char* text) {
  if (level < 0) {
    libc_copy(text, "   ");
    return;
  }
  sprintf(text, "%3d", level);
}
