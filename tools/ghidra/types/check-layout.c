/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  Compiled, never run, by "make
 * check-types" with an m68k compiler, so that int, long and pointers
 * are the target's 32 bits. */

#include <stddef.h>

#include "smartclock.h"

#define CHECK_SIZE(type, size) _Static_assert(sizeof(type) == (size), #type)
#define CHECK_FIELD(type, field, offset) \
	_Static_assert(offsetof(type, field) == (offset), #type "." #field)

CHECK_SIZE(scpi_numeric_param, 0x2c);
CHECK_FIELD(scpi_numeric_param, minimum, 0x06);
CHECK_FIELD(scpi_numeric_param, maximum, 0x0a);
CHECK_FIELD(scpi_numeric_param, convert, 0x0e);
CHECK_FIELD(scpi_numeric_param, value, 0x16);
CHECK_FIELD(scpi_numeric_param, is_integer, 0x26);
CHECK_FIELD(scpi_numeric_param, table_28, 0x28);

CHECK_SIZE(status_register, 16);
CHECK_FIELD(status_register, parent, 0x0a);
CHECK_FIELD(status_register, parent_bit, 0x0e);

CHECK_SIZE(status_condition, 6);

/* status_create allocates 0x16 bytes and four a register; the first
 * four of those are registers[0]. */
CHECK_SIZE(status_model, 0x12 + 7 * 4);
CHECK_FIELD(status_model, output, 0x02);
CHECK_FIELD(status_model, queue, 0x06);
CHECK_FIELD(status_model, conditions, 0x0a);
CHECK_FIELD(status_model, field_0e, 0x0e);
CHECK_FIELD(status_model, registers, 0x12);

/* The Oncore records, from the cells their messages' decoders fill. */
CHECK_SIZE(oncore_time_fields, 0x100c6e - 0x100c6b);
CHECK_SIZE(oncore_gmt_offset_fields, 0x100c71 - 0x100c6e);
CHECK_FIELD(oncore_date_fields, day, 0x100c73 - 0x100c72);
CHECK_FIELD(oncore_date_fields, year, 0x100c74 - 0x100c72);
CHECK_SIZE(oncore_position_fields, 0x100c86 - 0x100c76);
CHECK_FIELD(oncore_position_fields, longitude, 0x100c7a - 0x100c76);
CHECK_FIELD(oncore_position_fields, height_gps, 0x100c7e - 0x100c76);
CHECK_FIELD(oncore_position_fields, height_msl, 0x100c82 - 0x100c76);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x100d14 - 0x100d10);
CHECK_FIELD(oncore_hold_position_fields, height, 0x100d18 - 0x100d10);

/* @@Bk: 62 bytes of data in a 69-byte message, as printed. */
CHECK_SIZE(oncore_extension_fields, 69 - 7);
CHECK_FIELD(oncore_extension_fields, tdop, 0x100fb6 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, magnetic_variation, 0x100fb8 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, velocity_north, 0x100fba - 0x100fae);
CHECK_FIELD(oncore_extension_fields, correction_age, 0x100fc0 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, ecef_x, 0x100fc2 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, ecef_z, 0x100fca - 0x100fae);
CHECK_FIELD(oncore_extension_fields, ura, 0x100fce - 0x100fae);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x100fd4 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, clock_bias, 0x100fe8 - 0x100fae);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x100fea - 0x100fae);

/* The EFC history: memset(&efc_history, 0, 0x366) at 0x1021ac. */
CHECK_SIZE(efc_ring, 0x366);
CHECK_FIELD(efc_ring, stop_ptr, 0x1021ad - 0x1021ac);
CHECK_FIELD(efc_ring, field_002, 0x1021ae - 0x1021ac);
CHECK_FIELD(efc_ring, sample_count, 0x1021b0 - 0x1021ac);
CHECK_FIELD(efc_ring, summed_count, 0x1021b4 - 0x1021ac);
CHECK_FIELD(efc_ring, averages, 0x1021b6 - 0x1021ac);
CHECK_FIELD(efc_ring, weights, 0x1022b6 - 0x1021ac);
CHECK_FIELD(efc_ring, times, 0x1022f6 - 0x1021ac);
CHECK_FIELD(efc_ring, logs, 0x1023f6 - 0x1021ac);
CHECK_FIELD(efc_ring, efc_sum, 0x1024f6 - 0x1021ac);
CHECK_FIELD(efc_ring, drift, 0x1024fe - 0x1021ac);
CHECK_FIELD(efc_ring, field_35a, 0x102506 - 0x1021ac);
CHECK_FIELD(efc_ring, field_35e, 0x10250a - 0x1021ac);
CHECK_FIELD(efc_ring, field_362, 0x10250e - 0x1021ac);
