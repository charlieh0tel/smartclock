/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  Compiled, never run, by "make
 * check-types" with an m68k compiler, so that int, long and pointers
 * are the target's 32 bits. */

#include <stddef.h>

#include "smartclock.h"
#include "mc68331.h"
#include "mc68681.h"

#define CHECK_SIZE(type, size) _Static_assert(sizeof(type) == (size), #type)
#define CHECK_FIELD(type, field, offset) \
	_Static_assert(offsetof(type, field) == (offset), #type "." #field)

CHECK_SIZE(scpi_numeric_param, 0x2c);
CHECK_FIELD(scpi_numeric_param, minimum, 0x06);
CHECK_FIELD(scpi_numeric_param, maximum, 0x0a);
CHECK_FIELD(scpi_numeric_param, validate, 0x0e);
CHECK_FIELD(scpi_numeric_param, flags, 0x1e);
CHECK_FIELD(scpi_setting_param, flags, 0x14);
CHECK_FIELD(scpi_numeric_param, value, 0x16);
CHECK_FIELD(scpi_numeric_param, is_integer, 0x26);
CHECK_FIELD(scpi_numeric_param, steps, 0x28);
CHECK_FIELD(scpi_numeric_param, gps_request, 0x20);
CHECK_FIELD(scpi_numeric_param, group_cell, 0x22);

CHECK_SIZE(holdover_steering_state, 0x10205e - 0x102042);
CHECK_FIELD(holdover_steering_state, aging_limit, 0x10204a - 0x102042);
CHECK_FIELD(holdover_steering_state, ti_sum, 0x10204e - 0x102042);
CHECK_FIELD(holdover_steering_state, ti_count, 0x10205a - 0x102042);
CHECK_SIZE(loop_steering_state, 0x10207a - 0x10205e);
CHECK_FIELD(loop_steering_state, seconds, 0x10206a - 0x10205e);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x10206e - 0x10205e);
CHECK_FIELD(loop_steering_state, filter_weight, 0x102076 - 0x10205e);

CHECK_SIZE(serial_ring, 0x102fdf - 0x102edd);
CHECK_SIZE(serial_ring, 0x1030ec - 0x102fea);
CHECK_SIZE(duarta_driver, 0x102fea - 0x102fe0);
CHECK_FIELD(duarta_driver, overrun, 0x102fe6 - 0x102fe0);
CHECK_FIELD(duarta_driver, least_free, 0x102fe9 - 0x102fe0);

CHECK_SIZE(sci_settings_record, 0x101f56 - 0x101f50);
CHECK_FIELD(sci_settings_record, baud, 0x101f52 - 0x101f50);
CHECK_FIELD(sci_settings_record, echo, 0x101f55 - 0x101f50);
CHECK_SIZE(duartb_settings_record, 0x101f61 - 0x101f58);
CHECK_FIELD(duartb_settings_record, baud, 0x101f5a - 0x101f58);
CHECK_FIELD(duartb_settings_record, field_05, 0x101f5d - 0x101f58);
CHECK_FIELD(duartb_settings_record, eight_bits, 0x101f5e - 0x101f58);
CHECK_FIELD(duartb_settings_record, echo, 0x101f60 - 0x101f58);
CHECK_SIZE(duartb_applied_settings, 0x102ed0 - 0x102ecc);
CHECK_SIZE(duart_shadow_registers, 0x102edd - 0x102ed0);
CHECK_FIELD(duart_shadow_registers, imr, 0x102ed4 - 0x102ed0);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x102ed7 - 0x102ed0);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x102edb - 0x102ed0);

CHECK_SIZE(exchange_token, 16);
CHECK_SIZE(sci_exchange_state, 0x103188 - 0x103150);
CHECK_FIELD(sci_exchange_state, read_token, 0x103154 - 0x103150);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x10316c - 0x103150);
CHECK_FIELD(sci_exchange_state, write_token, 0x103170 - 0x103150);
CHECK_SIZE(serial_ring, 0x10328a - 0x103188);
CHECK_SIZE(sci_driver, 0x1032a6 - 0x10328a);
CHECK_SIZE(qspi_state, 0x1032f4 - 0x1032a6);
CHECK_FIELD(qspi_state, waiting, 0x1032aa - 0x1032a6);
CHECK_FIELD(qspi_state, busy, 0x1032ea - 0x1032a6);
CHECK_FIELD(qspi_state, receive_buffer, 0x1032ec - 0x1032a6);
CHECK_FIELD(qspi_state, running, 0x1032f3 - 0x1032a6);
CHECK_SIZE(eeprom_lock_token, 0x101d2c - 0x101d1c);
CHECK_FIELD(eeprom_lock_token, owner, 0x101d24 - 0x101d1c);
CHECK_FIELD(eeprom_lock_token, depth, 0x101d28 - 0x101d1c);
CHECK_FIELD(sci_driver, flow, 0x103294 - 0x10328a);
CHECK_FIELD(sci_driver, async_allowed, 0x10329c - 0x10328a);
CHECK_FIELD(sci_driver, baud_constant, 0x10329e - 0x10328a);
CHECK_FIELD(sci_driver, echo, 0x1032a0 - 0x10328a);
CHECK_FIELD(sci_driver, last_terminator, 0x1032a4 - 0x10328a);
CHECK_SIZE(duartb_driver, 0x103142 - 0x1030ec);
CHECK_FIELD(duartb_driver, flow, 0x1030f8 - 0x1030ec);
CHECK_FIELD(duartb_driver, reconfiguring, 0x1030ff - 0x1030ec);
CHECK_FIELD(duartb_driver, read_exchange, 0x103102 - 0x1030ec);
CHECK_FIELD(duartb_driver, read_token, 0x10310a - 0x1030ec);
CHECK_FIELD(duartb_driver, write_token, 0x103122 - 0x1030ec);
CHECK_FIELD(duartb_driver, async_allowed, 0x10313a - 0x1030ec);
CHECK_FIELD(duartb_driver, echo, 0x10313c - 0x1030ec);
CHECK_FIELD(duartb_driver, last_terminator, 0x103140 - 0x1030ec);

/* The installer's copies, laid out as the primary's. */
CHECK_SIZE(duartb_applied_settings, 0x10ad40 - 0x10ad3c);
CHECK_SIZE(duart_shadow_registers, 0x10ad4d - 0x10ad40);
CHECK_FIELD(duart_shadow_registers, imr, 0x10ad44 - 0x10ad40);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x10ad47 - 0x10ad40);
CHECK_FIELD(duart_shadow_registers, csrb, 0x10ad49 - 0x10ad40);
CHECK_SIZE(serial_ring, 0x10ae4f - 0x10ad4d);
CHECK_FIELD(duarta_driver, overrun, 0x10ae56 - 0x10ae50);
CHECK_FIELD(duarta_driver, least_free, 0x10ae59 - 0x10ae50);
CHECK_SIZE(duarta_driver, 0x10ae5a - 0x10ae50);
CHECK_SIZE(serial_ring, 0x10af5c - 0x10ae5a);
CHECK_FIELD(duartb_driver, async_next, 0x10af60 - 0x10af5c);
CHECK_FIELD(duartb_driver, transmit_count, 0x10af64 - 0x10af5c);
CHECK_FIELD(duartb_driver, async_pending, 0x10af67 - 0x10af5c);
CHECK_FIELD(duartb_driver, flow, 0x10af68 - 0x10af5c);
CHECK_FIELD(duartb_driver, read_errors, 0x10af6a - 0x10af5c);
CHECK_FIELD(duartb_driver, xon_xoff, 0x10af6c - 0x10af5c);
CHECK_FIELD(duartb_driver, parity, 0x10af6e - 0x10af5c);
CHECK_FIELD(duartb_driver, in_transmit_interrupt, 0x10af70 - 0x10af5c);
CHECK_FIELD(duartb_driver, read_token.task, 0x10af82 - 0x10af5c);
CHECK_FIELD(duartb_driver, write_token.task, 0x10af9a - 0x10af5c);
CHECK_FIELD(duartb_driver, async_allowed, 0x10afaa - 0x10af5c);
CHECK_FIELD(duartb_driver, echo, 0x10afac - 0x10af5c);
CHECK_FIELD(duartb_driver, last_terminator, 0x10afb0 - 0x10af5c);
CHECK_SIZE(duartb_driver, 0x10afb2 - 0x10af5c);

CHECK_SIZE(efc_gain_values, 0x10256c - 0x102554);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x102560 - 0x102554);
CHECK_FIELD(efc_gain_values, step_limit, 0x102564 - 0x102554);
CHECK_SIZE(efc_range_state, 0x101cf7 - 0x101cf4);
CHECK_FIELD(efc_range_state, near_end_checks, 0x101cf6 - 0x101cf4);
CHECK_SIZE(gps_command_mailbox, 0x102582 - 0x10256c);
CHECK_FIELD(gps_command_mailbox, arguments, 0x10256e - 0x10256c);
CHECK_FIELD(gps_command_mailbox, request, 0x10257e - 0x10256c);
CHECK_SIZE(adc_monitor, 0x101ba2 - 0x101b72);
CHECK_SIZE(adc_monitor[8], 0x101cf2 - 0x101b72);
CHECK_FIELD(adc_monitor, value, 0x101b9a - 0x101b72);
CHECK_FIELD(adc_monitor, valid, 0x101b9e - 0x101b72);
CHECK_FIELD(adc_monitor, sample_index, 0x101b9f - 0x101b72);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x101bd0 - 0x101ba2);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x101ba1 - 0x101b72);
CHECK_SIZE(gps_script_step, 0x101b6a - 0x101b56);
CHECK_FIELD(gps_script_step, argument_mode, 0x101b58 - 0x101b56);
CHECK_FIELD(gps_script_step, arguments, 0x101b5a - 0x101b56);

CHECK_SIZE(selftest_results, 0x100c68 - 0x100c24);
CHECK_FIELD(selftest_results, uart, 0x100c2e - 0x100c24);
CHECK_FIELD(selftest_results, lamp_mask, 0x100c30 - 0x100c24);
CHECK_FIELD(selftest_results, gps, 0x100c38 - 0x100c24);
CHECK_FIELD(selftest_results, interpolator_calibrated, 0x100c3c - 0x100c24);
CHECK_FIELD(selftest_results, running, 0x100c43 - 0x100c24);
CHECK_FIELD(selftest_results, qspi, 0x100c48 - 0x100c24);
CHECK_FIELD(selftest_results, passed, 0x100c58 - 0x100c24);
CHECK_FIELD(selftest_results, result, 0x100c5a - 0x100c24);
CHECK_FIELD(selftest_results, details, 0x100c5e - 0x100c24);
CHECK_FIELD(selftest_results, test, 0x100c66 - 0x100c24);

CHECK_SIZE(gps_request_entry, 26);
CHECK_FIELD(gps_request_entry, id, 0x08);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, sent, 0x12);
CHECK_SIZE(gps_request_ring, 0x101428 - 0x10139e);
CHECK_FIELD(gps_request_ring, read, 0x101420 - 0x10139e);
CHECK_FIELD(gps_request_ring, write, 0x101424 - 0x10139e);

CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, clock_invalid, 0x0a);
CHECK_FIELD(gps_command_log_entry, date, 0x0c);
CHECK_FIELD(gps_command_log_entry, time, 0x10);
CHECK_SIZE(gps_command_log, 0x1019a2 - 0x1017f6);
CHECK_SIZE(gps_command_log, 0x101b4e - 0x1019a2);
CHECK_FIELD(gps_command_log, read, 0x10199a - 0x1017f6);
CHECK_FIELD(gps_command_log, write, 0x10199e - 0x1017f6);
CHECK_FIELD(gps_command_log, read, 0x101b46 - 0x1019a2);
CHECK_FIELD(gps_command_log, write, 0x101b4a - 0x1019a2);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x101d0c - 0x101d00);
CHECK_FIELD(lifetime_state, date, 0x101d04 - 0x101d00);
CHECK_FIELD(lifetime_state, count, 0x101d08 - 0x101d00);

CHECK_SIZE(sprintf_output, 8);
CHECK_SIZE(scpi_decimal, 20);
CHECK_FIELD(scpi_decimal, exponent, 0x12);
CHECK_FIELD(sprintf_output, cursor, 4);

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
CHECK_FIELD(efc_ring, loop_settled, 0x1021ae - 0x1021ac);
CHECK_FIELD(efc_ring, sample_count, 0x1021b0 - 0x1021ac);
CHECK_FIELD(efc_ring, summed_count, 0x1021b4 - 0x1021ac);
CHECK_FIELD(efc_ring, averages, 0x1021b6 - 0x1021ac);
CHECK_FIELD(efc_ring, weights, 0x1022b6 - 0x1021ac);
CHECK_FIELD(efc_ring, times, 0x1022f6 - 0x1021ac);
CHECK_FIELD(efc_ring, logs, 0x1023f6 - 0x1021ac);
CHECK_FIELD(efc_ring, efc_sum, 0x1024f6 - 0x1021ac);
CHECK_FIELD(efc_ring, drift, 0x1024fe - 0x1021ac);
CHECK_FIELD(efc_ring, fit_a, 0x102506 - 0x1021ac);
CHECK_FIELD(efc_ring, fit_b, 0x10250a - 0x1021ac);
CHECK_FIELD(efc_ring, fit_c, 0x10250e - 0x1021ac);
CHECK_SIZE(aging_curve_state, 0x102002 - 0x101fd8);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x101fd9 - 0x101fd8);
CHECK_FIELD(aging_curve_state, oldest_time, 0x101fda - 0x101fd8);
CHECK_FIELD(aging_curve_state, newest_time, 0x101fde - 0x101fd8);
CHECK_FIELD(aging_curve_state, a, 0x101fea - 0x101fd8);
CHECK_FIELD(aging_curve_state, b, 0x101fee - 0x101fd8);
CHECK_FIELD(aging_curve_state, c, 0x101ff2 - 0x101fd8);
CHECK_FIELD(aging_curve_state, rms, 0x101ffa - 0x101fd8);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x101ffe - 0x101fd8);

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
CHECK_FIELD(pll_state, previous_stage, 0x01);
CHECK_FIELD(pll_state, powerup_state, 0x03);
CHECK_FIELD(pll_state, reference_check_state, 0x06);
CHECK_FIELD(pll_state, startup_state, 0x0c);
CHECK_FIELD(pll_state, normal_state, 0x0e);
CHECK_FIELD(pll_state, holdover_state, 0x10);
CHECK_FIELD(pll_state, hold_causes, 0x11);
CHECK_FIELD(pll_state, coarse_state, 0x16);
CHECK_FIELD(pll_state, frequency_adjusted, 0x18);
CHECK_FIELD(pll_state, fine_retries, 0x1a);
CHECK_FIELD(pll_state, gps_diag_state, 0x24);
CHECK_FIELD(pll_state, gps_init_state, 0x26);
CHECK_FIELD(pll_state, leap_check_state, 0x2b);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x3a);
CHECK_FIELD(pll_state, frequency_tolerance, 0x46);
CHECK_FIELD(pll_state, loop_time_constant, 0x3b8);
CHECK_FIELD(pll_state, led_gps_lock, 0x102526 - 0x102162);
CHECK_FIELD(pll_state, holdover_duration, 0x3c8);
CHECK_FIELD(pll_state, reference_uptime, 0x3cc);
CHECK_FIELD(pll_state, reference_seconds, 0x3d0);
CHECK_FIELD(pll_state, sync_requested, 0x102536 - 0x102162);
CHECK_FIELD(pll_state, powerup_countdown, 0x02);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x04);
CHECK_FIELD(pll_state, ti_limit_override, 0x13);
CHECK_FIELD(pll_state, recovery_countdown, 0x14);
CHECK_FIELD(pll_state, aging_state, 0x20);
CHECK_FIELD(pll_state, leap_check_timer, 0x28);
CHECK_FIELD(pll_state, leap_check_requested, 0x2a);
CHECK_FIELD(pll_state, leap_ack_state, 0x2c);
CHECK_FIELD(pll_state, leap_ack_again, 0x30);
CHECK_FIELD(pll_state, ti_filtered, 0x3b0);
CHECK_FIELD(pll_state, efc_base, 0x3b4);
CHECK_FIELD(pll_state, holdover_start_time, 0x3bc);
CHECK_FIELD(pll_state, holdover_start_date, 0x3c0);
CHECK_FIELD(pll_state, fpga_load_pending, 0x3d1);
CHECK_FIELD(pll_state, loop_running, 0x3d3);
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
CHECK_FIELD(gps_state, hold_height_type, 0xb2);
CHECK_FIELD(gps_state, status_received, 0xcc);
CHECK_FIELD(gps_state, status_rate, 0xcd);
CHECK_FIELD(gps_state, reference_qualified, 0x10e);
CHECK_FIELD(gps_state, status_reported, 0x10f);
CHECK_FIELD(gps_state, raim_received, 0x2fa);
CHECK_FIELD(gps_state, reference_valid, 0x52a);
CHECK_FIELD(gps_state, validation_start, 0x52e);
CHECK_FIELD(gps_state, status_code_reset, 0x534);
CHECK_FIELD(gps_state, surveying, 0x53a);
CHECK_FIELD(gps_state, command_monitor_query, 0x540);
CHECK_FIELD(gps_state, command_monitor_time, 0x544);
CHECK_FIELD(gps_state, status_monitor_time, 0x54c);
CHECK_FIELD(gps_state, status_last_time, 0x550);
CHECK_FIELD(gps_state, status_monitor_failed, 0x556);
CHECK_FIELD(gps_state, raim_option, 0x558);
/* The packed enums are bytes, as the fields they type. */
CHECK_SIZE(pll_stage, 1);
CHECK_SIZE(pll_hold_cause, 1);
CHECK_SIZE(adc_channel, 1);
CHECK_SIZE(oncore_channel_mode, 1);
CHECK_SIZE(oncore_raim_status, 1);

/* nv_settings at 0x101e8a, each field where its descriptor's value
 * points, and the two descriptors' sizes. */
CHECK_SIZE(nv_settings_fields, 0xc5);
/* each is followed by the next descriptor's widths */
CHECK_SIZE(scpi_setting_param, 0x33ca8 - 0x33c8a - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* 14 action descriptors, each referred to at its own address, from
 * 0x4132a to efc_history_update at 0x4137e */
CHECK_SIZE(scpi_action_param, (0x4137e - 0x4132a) / 14);
CHECK_FIELD(scpi_action_param, action, 0x02);
/* the Oncore message table: 49 entries from 0x4fece to the ID strings
 * at 0x50986; gps_response_timed_out reads +0x1a, gps_response_lookup
 * +0x1c, gps_response_match +0x1e, gps_response_decode +0x20 and +0x24 */
CHECK_SIZE(oncore_decoder, 8);
CHECK_SIZE(oncore_message_entry, (0x50986 - 0x4fece) / 49);
CHECK_FIELD(oncore_message_entry, kind, 0x4fed2 - 0x4fece);
CHECK_FIELD(oncore_message_entry, encoders, 0x4fed4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, query, 0x4fee4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, timeout, 0x4fee8 - 0x4fece);
CHECK_FIELD(oncore_message_entry, length, 0x4feea - 0x4fece);
CHECK_FIELD(oncore_message_entry, match_kind, 0x4feec - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders, 0x4feee - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x4fef2 - 0x4fece);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x101e8c - 0x101e8a);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x101e98 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x101e9a - 0x101e8a);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x101ea6 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, position, 0x101eaa - 0x101e8a);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x101eb6 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, time_zone, 0x101eba - 0x101e8a);
CHECK_FIELD(nv_settings_fields, leap_date, 0x101ec6 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x101ece - 0x101e8a);
CHECK_FIELD(nv_settings_fields, raim_state, 0x101ecf - 0x101e8a);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x101ed2 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, status_enable, 0x101ede - 0x101e8a);
CHECK_FIELD(nv_settings_fields, status_positive_transition, 0x101efa - 0x101e8a);
CHECK_FIELD(nv_settings_fields, status_negative_transition, 0x101f0e - 0x101e8a);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x101f22 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x101f26 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x101f36 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, field_bc, 0x101f46 - 0x101e8a);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x101f4d - 0x101e8a);
CHECK_FIELD(nv_settings_fields, debug_output, 0x101f4e - 0x101e8a);

/* The time-interval counter's two records: the reading at 0x101f9c, its
 * flag at 0x101fa4 and armed flag at 0x101fa8; the configuration at
 * 0x101fae, ending where interpolator_calibration_state begins. */
CHECK_SIZE(interval_reading_fields, 0x101fa9 - 0x101f9c);
CHECK_FIELD(interval_reading_fields, valid, 0x101fa4 - 0x101f9c);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x101fa8 - 0x101f9c);
CHECK_SIZE(counter_configuration_fields, 0x101fce - 0x101fae);
CHECK_FIELD(counter_configuration_fields, calibration, 4);
CHECK_FIELD(counter_configuration_fields, registers_mode, 0xc);
CHECK_FIELD(counter_configuration_fields, calibrate_state, 0xf);
CHECK_FIELD(counter_configuration_fields, sync_mode, 0x101fbe - 0x101fae);
CHECK_FIELD(counter_configuration_fields, sync_kind, 0x101fc0 - 0x101fae);
CHECK_FIELD(counter_configuration_fields, sync_count, 0x101fc2 - 0x101fae);
CHECK_FIELD(counter_configuration_fields, interpolator_offset, 0x18);
CHECK_FIELD(counter_configuration_fields, interpolator_scale, 0x1c);

/* the MC68331's modules: Tables D-2, D-3 and D-13 */
CHECK_SIZE(gpt_registers, 0x2e);
CHECK_FIELD(gpt_registers, tcnt, 0xfff90a - 0xfff900);
CHECK_FIELD(gpt_registers, toc, 0xfff914 - 0xfff900);
CHECK_FIELD(gpt_registers, tmsk1, 0xfff920 - 0xfff900);
CHECK_FIELD(gpt_registers, pwmc, 0xfff925 - 0xfff900);
CHECK_FIELD(gpt_registers, prescl, 0xfff92c - 0xfff900);
CHECK_SIZE(sim_registers, 0xfffa78 - 0xfffa00);
CHECK_FIELD(sim_registers, sypcr, 0xfffa21 - 0xfffa00);
CHECK_FIELD(sim_registers, picr, 0xfffa22 - 0xfffa00);
CHECK_FIELD(sim_registers, swsr, 0xfffa27 - 0xfffa00);
CHECK_FIELD(sim_registers, portc, 0xfffa41 - 0xfffa00);
CHECK_FIELD(sim_registers, csboot, 0xfffa48 - 0xfffa00);
CHECK_FIELD(sim_registers, cs, 0xfffa4c - 0xfffa00);
CHECK_SIZE(qsm_registers, 0xfffd50 - 0xfffc00);
CHECK_FIELD(qsm_registers, sccr1, 0xfffc0a - 0xfffc00);
CHECK_FIELD(qsm_registers, portqs, 0xfffc15 - 0xfffc00);
CHECK_FIELD(qsm_registers, spsr, 0xfffc1f - 0xfffc00);
CHECK_FIELD(qsm_registers, rr, 0xfffd00 - 0xfffc00);
CHECK_FIELD(qsm_registers, tr, 0xfffd20 - 0xfffc00);
CHECK_FIELD(qsm_registers, cr, 0xfffd40 - 0xfffc00);

/* the 68681: Table 4-1, register n at 2n + 1 */
CHECK_SIZE(duart_channel, 8);
CHECK_FIELD(duart_channel, sr_csr, 0x200003 - 0x200000);
CHECK_FIELD(duart_channel, rb_tb, 0x200007 - 0x200000);
CHECK_SIZE(duart_registers, 0x20);
CHECK_FIELD(duart_registers, isr_imr, 0x20000b - 0x200000);
CHECK_FIELD(duart_registers, b, 0x200010 - 0x200000);
CHECK_FIELD(duart_registers, ivr, 0x200019 - 0x200000);
CHECK_FIELD(duart_registers, start_sopbc, 0x20001d - 0x200000);
CHECK_FIELD(duart_registers, stop_ropbc, 0x20001f - 0x200000);
