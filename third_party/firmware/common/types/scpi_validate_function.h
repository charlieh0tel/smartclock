/* A SCPI parameter's validator, as the images' smartclock.h declare them (each
 * includes this header where they stand): docs/firmware/rewrite.md, "Shared C".
 * Where a comment cites code, it is the Z3805A 3543B's, which the
 * 58503A's copy of the comment came from. */

/* A numeric SCPI parameter's descriptor, in ROM; its small tables sit
 * just before it.  0x2c bytes. */
/* A numeric descriptor's validate: the new value, its index, the
 * bounds and step for it, and the request's source; its result is the
 * value as validated. */
typedef long scpi_validate_function(void* value, int index, void* minimum,
                                    void* maximum, void* step, int source);
