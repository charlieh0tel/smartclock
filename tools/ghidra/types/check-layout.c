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

/* The GPS task's state, memset(&gps, 0, 0x55a) at 0x100c6a, with each
 * message's record at the cell its decoder fills. */
CHECK_SIZE(gps_state, 0x55a);
CHECK_FIELD(gps_state, time, 0x100c6b - 0x100c6a);
CHECK_FIELD(gps_state, date, 0x100c72 - 0x100c6a);
CHECK_FIELD(gps_state, position, 0x100c76 - 0x100c6a);
CHECK_FIELD(gps_state, mask_angle, 0x100c86 - 0x100c6a);
CHECK_FIELD(gps_state, manual_satellites, 0x100c88 - 0x100c6a);
CHECK_FIELD(gps_state, xdop_type, 0x100c8e - 0x100c6a);
CHECK_FIELD(gps_state, xdop_hysteresis, 0x100c90 - 0x100c6a);
CHECK_FIELD(gps_state, ignored_satellites, 0x100c96 - 0x100c6a);
CHECK_FIELD(gps_state, almanac_update, 0x100cf8 - 0x100c6a);
CHECK_FIELD(gps_state, ionospheric_correction, 0x100d0e - 0x100c6a);
CHECK_FIELD(gps_state, hold_position, 0x100d10 - 0x100c6a);
CHECK_FIELD(gps_state, position_hold, 0x100d1e - 0x100c6a);
CHECK_FIELD(gps_state, altitude_hold, 0x100d25 - 0x100c6a);
CHECK_FIELD(gps_state, epoch_offset, 0x100d28 - 0x100c6a);
CHECK_FIELD(gps_state, pps_offset, 0x100d2a - 0x100c6a);
CHECK_FIELD(gps_state, pps_cable_delay, 0x100d2e - 0x100c6a);
CHECK_FIELD(gps_state, application_type, 0x100d33 - 0x100c6a);
CHECK_FIELD(gps_state, position_fix_threshold, 0x100d34 - 0x100c6a);
CHECK_FIELD(gps_state, status, 0x100d38 - 0x100c6a);
CHECK_FIELD(gps_state, status.latitude, 0x100d44 - 0x100c6a);
CHECK_FIELD(gps_state, status.channels, 0x100d5e - 0x100c6a);
CHECK_FIELD(gps_state, status.receiver_status, 0x100d76 - 0x100c6a);
CHECK_FIELD(gps_state, visibility_received, 0x100d7a - 0x100c6a);
CHECK_FIELD(gps_state, visibility, 0x100d7c - 0x100c6a);
CHECK_FIELD(gps_state, visibility.satellites[0].satellite, 0x100d7e - 0x100c6a);
CHECK_FIELD(gps_state, visibility.satellites[0].doppler, 0x100d80 - 0x100c6a);
CHECK_FIELD(gps_state, visibility.satellites[0].elevation, 0x100d82 - 0x100c6a);
CHECK_FIELD(gps_state, visibility.satellites[0].azimuth, 0x100d84 - 0x100c6a);
CHECK_FIELD(gps_state, xdop_table, 0x100df7 - 0x100c6a);
CHECK_FIELD(gps_state, xdop_table.combinations, 0x100dfa - 0x100c6a);
CHECK_FIELD(gps_state, almanac, 0x100e44 - 0x100c6a);
CHECK_FIELD(gps_state, corrections, 0x100f12 - 0x100c6a);
CHECK_FIELD(gps_state, corrections.channels, 0x100f16 - 0x100c6a);
CHECK_FIELD(gps_state, corrections.channels[0].range_correction, 0x100f18 - 0x100c6a);
CHECK_FIELD(gps_state, raim, 0x100f65 - 0x100c6a);
CHECK_FIELD(gps_state, raim.sawtooth, 0x100f7e - 0x100c6a);
CHECK_FIELD(gps_state, leap_second_pending, 0x100fa4 - 0x100c6a);
CHECK_FIELD(gps_state, self_test, 0x100fa6 - 0x100c6a);
CHECK_FIELD(gps_state, extension, 0x100fae - 0x100c6a);
CHECK_FIELD(gps_state, fix_mode, 0x101056 - 0x100c6a);
CHECK_FIELD(gps_state, receiver_id_received, 0x101058 - 0x100c6a);
CHECK_FIELD(gps_state, receiver_id, 0x101059 - 0x100c6a);
CHECK_FIELD(gps_state, average_count, 0x101170 - 0x100c6a);
CHECK_FIELD(gps_state, average_latitude, 0x101174 - 0x100c6a);
CHECK_FIELD(gps_state, average_height_msl, 0x10118c - 0x100c6a);
CHECK_SIZE(oncore_status_fields, 63);
CHECK_SIZE(oncore_visibility_fields, 0x100df6 - 0x100d7c);
CHECK_SIZE(oncore_xdop_table_fields, 75);
CHECK_SIZE(oncore_corrections_fields, 64);
CHECK_SIZE(oncore_raim_fields, 63);
CHECK_SIZE(oncore_almanac_status_fields, 16);

/* The loop's state block, memset(&pll, 0, 0x3d6) at 0x102162. */
CHECK_SIZE(pll_state, 0x3d6);
CHECK_FIELD(pll_state, manual, 0x12);
CHECK_FIELD(pll_state, recovery_state, 0x15);
CHECK_FIELD(pll_state, fine_state, 0x17);
CHECK_FIELD(pll_state, efc, 0x102194 - 0x102162);
CHECK_FIELD(pll_state, tfom, 0x102198 - 0x102162);
CHECK_FIELD(pll_state, efc_history, 0x1021ac - 0x102162);

/* Records the printf calls name. */
CHECK_SIZE(oncore_receiver_id, 277);
CHECK_FIELD(oncore_receiver_id, software_part_number, 0x10107b - 0x101059);
CHECK_FIELD(oncore_receiver_id, software_version, 0x101096 - 0x101059);
CHECK_FIELD(oncore_receiver_id, model_number, 0x1010e7 - 0x101059);
CHECK_FIELD(oncore_receiver_id, options, 0x101153 - 0x101059);
CHECK_FIELD(gps_state, correction_average, 0x100f52 - 0x100c6a);
CHECK_FIELD(gps_state, correction_deviation, 0x100f5a - 0x100c6a);
CHECK_FIELD(pll_state, fine_index, 0x10217b - 0x102162);
CHECK_FIELD(pll_state, fine_d, 0x10217e - 0x102162);
CHECK_SIZE(gps_statistics, 0x36);
CHECK_FIELD(gps_statistics, successes, 0x1017c8 - 0x1017bc);
CHECK_FIELD(gps_statistics, rejects, 0x1017cc - 0x1017bc);
CHECK_FIELD(gps_statistics, t_retries, 0x1017e2 - 0x1017bc);
CHECK_FIELD(gps_statistics, raim_detected, 0x1017e4 - 0x1017bc);
CHECK_FIELD(gps_statistics, raim_alarms, 0x1017e6 - 0x1017bc);
CHECK_FIELD(gps_statistics, unknown_response, 0x1017e8 - 0x1017bc);
CHECK_FIELD(gps_statistics, checksum_errors, 0x1017ec - 0x1017bc);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x1017ee - 0x1017bc);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x1017f0 - 0x1017bc);
CHECK_SIZE(exception_record, 0x1009b0 - 0x10099e);
CHECK_FIELD(exception_record, name, 0x10099f - 0x10099e);
CHECK_FIELD(exception_record, sr, 0x1009a4 - 0x10099e);
CHECK_FIELD(exception_record, pc, 0x1009a6 - 0x10099e);
CHECK_FIELD(exception_record, vector, 0x1009aa - 0x10099e);
CHECK_FIELD(exception_record, address, 0x1009ac - 0x10099e);
/* The pool: five entries from 0x10372a to 0x1037ac. */
CHECK_SIZE(klok_entry, (0x1037ac - 0x10372a) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read: entry n's fields at 0x101d98, 0x101d9c, 0x101da0, ... + n * 0x14. */
CHECK_SIZE(log_page, 0x101dd4 - 0x101d94);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x101d98 - 0x101d94);
CHECK_FIELD(log_entry, format, 0x101da0 - 0x101d98);
CHECK_FIELD(log_entry, argument_1, 0x101da4 - 0x101d98);
CHECK_SIZE(fpga_bit, 4);
/* interpolator_calibrate reads and writes param[0] to param[8]. */
CHECK_SIZE(interpolator_calibration, 9);
CHECK_FIELD(interpolator_calibration, result_valid, 4);
CHECK_FIELD(interpolator_calibration, reading_high, 8);
