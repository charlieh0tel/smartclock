/* The console's word pr_1pps, the same in each image that includes this
 * file (docs/firmware/rewrite.md, "Shared C", "A file for some images").
 * An image without the word leaves the file out.  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

void diag_pr_1pps(void) { gps_command.code = GPS_CONSOLE_PRINT_PPS; }
