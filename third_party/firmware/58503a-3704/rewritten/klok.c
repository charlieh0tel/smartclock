/* klok.c's functions that are this image's own: the rest are
 * common/klok.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

#include <stddef.h>

/* A clock entry's time and date, laid out as a packed_datetime is, which
 * datetime_add and datetime_subtract take; the image passes the entry's
 * +4 (0x10b86, 0x10bee) as one. */
static packed_datetime* klok_when(uint32_t* time) {
  return (packed_datetime*)time;
}
