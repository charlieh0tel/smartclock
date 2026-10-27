/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the image it is written as their
 * difference: the Z3801A 3543's, each derived again from its code and
 * export where it began as the Z3805A 3543B's
 * (tools/firmware/layout_addresses.py).  Compiled, never run, by "make
 * check-types" with an m68k compiler, so that int, long and pointers
 * are the target's 32 bits. */

#include <stddef.h>

#include "smartclock.h"
#include "../../../include/mc68331.h"
#include "../../../include/mc68681.h"

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

CHECK_SIZE(holdover_steering_state, 0x102056 - 0x10203a);
CHECK_FIELD(holdover_steering_state, aging_limit, 0x102042 - 0x10203a);
CHECK_FIELD(holdover_steering_state, ti_sum, 0x102046 - 0x10203a);
CHECK_FIELD(holdover_steering_state, ti_count, 0x102052 - 0x10203a);
CHECK_SIZE(loop_steering_state, 0x102072 - 0x102056);
CHECK_FIELD(loop_steering_state, seconds, 0x102062 - 0x102056);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x102066 - 0x102056);
CHECK_FIELD(loop_steering_state, filter_weight, 0x10206e - 0x102056);

CHECK_SIZE(serial_ring, 0x102fb1 - 0x102eaf);
CHECK_SIZE(serial_ring, 0x1030be - 0x102fbc);
CHECK_SIZE(duarta_driver, 0x102fbc - 0x102fb2);
CHECK_FIELD(duarta_driver, overrun, 0x102fb8 - 0x102fb2);
CHECK_FIELD(duarta_driver, least_free, 0x102fbb - 0x102fb2);

CHECK_SIZE(duartb_settings_record, 0x101f5b - 0x101f52);
CHECK_FIELD(duartb_settings_record, baud, 0x101f54 - 0x101f52);
CHECK_FIELD(duartb_settings_record, field_05, 0x101f57 - 0x101f52);
CHECK_FIELD(duartb_settings_record, eight_bits, 0x101f58 - 0x101f52);
CHECK_FIELD(duartb_settings_record, echo, 0x101f5a - 0x101f52);
CHECK_SIZE(duartb_applied_settings, 0x102ea2 - 0x102e9e);
CHECK_SIZE(duart_shadow_registers, 0x102eaf - 0x102ea2);
CHECK_FIELD(duart_shadow_registers, imr, 0x102ea6 - 0x102ea2);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x102ea9 - 0x102ea2);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x102ead - 0x102ea2);

CHECK_SIZE(exchange_token, 16);
CHECK_SIZE(qspi_state, 0x103170 - 0x103122);
CHECK_FIELD(qspi_state, waiting, 0x103126 - 0x103122);
CHECK_FIELD(qspi_state, busy, 0x103166 - 0x103122);
CHECK_FIELD(qspi_state, receive_buffer, 0x103168 - 0x103122);
CHECK_FIELD(qspi_state, running, 0x10316f - 0x103122);
CHECK_SIZE(eeprom_lock_token, 0x101d2a - 0x101d1a);
CHECK_FIELD(eeprom_lock_token, owner, 0x101d22 - 0x101d1a);
CHECK_FIELD(eeprom_lock_token, depth, 0x101d26 - 0x101d1a);
CHECK_SIZE(duartb_driver, 0x103114 - 0x1030be);
CHECK_FIELD(duartb_driver, flow, 0x1030ca - 0x1030be);
CHECK_FIELD(duartb_driver, reconfiguring, 0x1030d1 - 0x1030be);
CHECK_FIELD(duartb_driver, read_exchange, 0x1030d4 - 0x1030be);
CHECK_FIELD(duartb_driver, read_token, 0x1030dc - 0x1030be);
CHECK_FIELD(duartb_driver, write_token, 0x1030f4 - 0x1030be);
CHECK_FIELD(duartb_driver, async_allowed, 0x10310c - 0x1030be);
CHECK_FIELD(duartb_driver, echo, 0x10310e - 0x1030be);
CHECK_FIELD(duartb_driver, last_terminator, 0x103112 - 0x1030be);

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
CHECK_SIZE(duartb_settings_record, 0x10ab39 - 0x10ab30);
CHECK_SIZE(psos_configuration, 0x10abe6 - 0x10ab84);
CHECK_SIZE(io_device_jumps, 0x10ab84 - 0x10ab60);

/* The boot code's results, at 0x10ab20, up to the installer's settings. */
CHECK_SIZE(boot_selftest_results, 0x10ab30 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, cpu, 0x10ab22 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram, 0x10ab24 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram_data_bits, 0x10ab26 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram_address_lines, 0x10ab28 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, uart, 0x10ab2c - 0x10ab20);
CHECK_FIELD(boot_selftest_results, lamp_mask, 0x10ab2e - 0x10ab20);

/* The kernel's configuration and I/O table, as psos_config_init copies
 * them: 0x62 bytes to 0x10261e and 0x90 to 0x1025b2. */
CHECK_SIZE(psos_configuration, 0x102680 - 0x10261e);
CHECK_FIELD(psos_configuration, data_end, 0x102622 - 0x10261e);
CHECK_FIELD(psos_configuration, region_2_start, 0x10262a - 0x10261e);
CHECK_FIELD(psos_configuration, task_records, 0x102636 - 0x10261e);
CHECK_FIELD(psos_configuration, exchange_records, 0x102638 - 0x10261e);
CHECK_FIELD(psos_configuration, message_buffers, 0x10263a - 0x10261e);
CHECK_FIELD(psos_configuration, last_device, 0x102640 - 0x10261e);
CHECK_FIELD(psos_configuration, io_table, 0x102642 - 0x10261e);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x102646 - 0x10261e);
CHECK_FIELD(psos_configuration, root_entry, 0x10264a - 0x10261e);
CHECK_FIELD(psos_configuration, root_priority, 0x102652 - 0x10261e);
CHECK_FIELD(psos_configuration, start_hook, 0x102654 - 0x10261e);
CHECK_FIELD(psos_configuration, delete_hook, 0x102658 - 0x10261e);
CHECK_FIELD(psos_configuration, debug_hooks, 0x102660 - 0x10261e);
CHECK_FIELD(psos_configuration, task_hooks, 0x102664 - 0x10261e);
CHECK_SIZE(io_device_jumps[3], 0x10261e - 0x1025b2);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x10268a + 6n, its state at 0x102688 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x10268a - 0x102688);
CHECK_SIZE(task_table_entry[17], 0x1026ee - 0x102688);

CHECK_SIZE(spoo_request, 0x103596 - 0x10357e);
CHECK_FIELD(spoo_request, function, 0x103586 - 0x10357e);
CHECK_FIELD(spoo_request, arguments, 0x10358a - 0x10357e);
CHECK_SIZE(early_status_events, 0x102e96 - 0x102e8e);
CHECK_SIZE(early_status_events[2], 0x102e9e - 0x102e8e);
CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x102562 - 0x10254a);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x102556 - 0x10254a);
CHECK_FIELD(efc_gain_values, step_limit, 0x10255a - 0x10254a);
CHECK_SIZE(efc_range_state, 0x101cf5 - 0x101cf2);
CHECK_FIELD(efc_range_state, near_end_checks, 0x101cf4 - 0x101cf2);
CHECK_SIZE(gps_command_mailbox, 0x102578 - 0x102562);
CHECK_FIELD(gps_command_mailbox, arguments, 0x102564 - 0x102562);
CHECK_FIELD(gps_command_mailbox, request, 0x102574 - 0x102562);
CHECK_SIZE(adc_monitor, 0x101ba0 - 0x101b70);
CHECK_SIZE(adc_monitor[8], 0x101cf0 - 0x101b70);
CHECK_FIELD(adc_monitor, value, 0x101b98 - 0x101b70);
CHECK_FIELD(adc_monitor, valid, 0x101b9c - 0x101b70);
CHECK_FIELD(adc_monitor, sample_index, 0x101b9d - 0x101b70);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x101bce - 0x101ba0);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x101b9f - 0x101b70);
CHECK_SIZE(gps_request_code_word, 0x101b56 - 0x101b54);
CHECK_SIZE(gps_script_step, 0x101b68 - 0x101b54);
CHECK_FIELD(gps_script_step, argument_mode, 0x101b56 - 0x101b54);
CHECK_FIELD(gps_script_step, arguments, 0x101b58 - 0x101b54);

CHECK_SIZE(selftest_results, 0x100c66 - 0x100c22);
CHECK_FIELD(selftest_results, uart, 0x100c2c - 0x100c22);
CHECK_FIELD(selftest_results, lamp_mask, 0x100c2e - 0x100c22);
CHECK_FIELD(selftest_results, gps, 0x100c36 - 0x100c22);
CHECK_FIELD(selftest_results, interpolator_calibrated, 0x100c3a - 0x100c22);
CHECK_FIELD(selftest_results, running, 0x100c41 - 0x100c22);
CHECK_FIELD(selftest_results, qspi, 0x100c46 - 0x100c22);
CHECK_FIELD(selftest_results, passed, 0x100c56 - 0x100c22);
CHECK_FIELD(selftest_results, result, 0x100c58 - 0x100c22);
CHECK_FIELD(selftest_results, details, 0x100c5c - 0x100c22);
CHECK_FIELD(selftest_results, test, 0x100c64 - 0x100c22);

CHECK_SIZE(gps_request_entry, 26);
CHECK_FIELD(gps_request_entry, sender, 0x08);
CHECK_FIELD(gps_request_entry, result, 0x09);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, argument_mode, 0x0c);
CHECK_FIELD(gps_request_entry, arguments, 0x0e);
CHECK_FIELD(gps_request_entry, sent, 0x12);
CHECK_SIZE(gps_request_ring, 0x101426 - 0x10139c);
CHECK_FIELD(gps_request_ring, read, 0x10141e - 0x10139c);
CHECK_FIELD(gps_request_ring, write, 0x101422 - 0x10139c);

CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0a);
CHECK_FIELD(gps_command_log_entry, date, 0x0c);
CHECK_FIELD(gps_command_log_entry, time, 0x10);
CHECK_SIZE(gps_command_log, 0x1019a0 - 0x1017f4);
CHECK_SIZE(gps_command_log, 0x101b4c - 0x1019a0);
CHECK_FIELD(gps_command_log, read, 0x101998 - 0x1017f4);
CHECK_FIELD(gps_command_log, write, 0x10199c - 0x1017f4);
CHECK_FIELD(gps_command_log, read, 0x101b44 - 0x1019a0);
CHECK_FIELD(gps_command_log, write, 0x101b48 - 0x1019a0);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x101d0a - 0x101cfe);
CHECK_FIELD(lifetime_state, date, 0x101d02 - 0x101cfe);
CHECK_FIELD(lifetime_state, count, 0x101d06 - 0x101cfe);

CHECK_SIZE(qspi_buffers, 8);
CHECK_SIZE(scpi_step, 8);
CHECK_SIZE(parameter_record, 16);
CHECK_FIELD(parameter_record, sci_baud, 9);
CHECK_SIZE(identification_record, 48);
CHECK_FIELD(identification_record, suffix, 0x26);
CHECK_SIZE(lifetime_record, 32);
CHECK_FIELD(lifetime_record, date, 2);
CHECK_FIELD(lifetime_record, count, 6);
CHECK_FIELD(lifetime_record, efc, 0xa);
CHECK_SIZE(calibration_block, 64);
CHECK_FIELD(calibration_block, time_offset, 6);
CHECK_SIZE(nv_record, 0x80);
CHECK_FIELD(nv_record, sequence, 4);
CHECK_FIELD(nv_record, packed, 0x10);
CHECK_SIZE(reset_record, 0x5c);
CHECK_FIELD(reset_record, text, 0xc);

CHECK_SIZE(sprintf_output, 8);
CHECK_SIZE(scpi_decimal, 20);
CHECK_FIELD(scpi_decimal, exponent, 0x12);
CHECK_SIZE(scpi_multiplier[13], 0x26a26 - 0x269f2);
CHECK_SIZE(scpi_unit[2], 0x53d14 - 0x53d08);
CHECK_FIELD(scpi_keyword, text, 2);
CHECK_SIZE(scpi_keyword, 0x14);
CHECK_FIELD(scpi_unit, exponent, 4);
CHECK_SIZE(scpi_error_message[101], 0x27a32 - 0x277d4);
CHECK_FIELD(sprintf_output, cursor, 4);

CHECK_SIZE(status_register, 16);
CHECK_FIELD(status_register, parent, 0x0a);
CHECK_FIELD(status_register, parent_bit, 0x0e);

CHECK_SIZE(status_condition, 6);
CHECK_SIZE(status_condition_code, 1);
CHECK_SIZE(fpga_configuration_index, 1);
CHECK_SIZE(fpga_chip, 1);
CHECK_SIZE(mutex, 0x18);
CHECK_SIZE(log_event_code, 1);
CHECK_SIZE(serial_baud, 1);
CHECK_SIZE(serial_parity, 1);
CHECK_SIZE(timecode_format, 1);
CHECK_SIZE(data_format, 1);
CHECK_FIELD(scpi_double_step, function, 0x08);
CHECK_SIZE(klok_event_class, 1);
CHECK_FIELD(mutex, holds, 0x0c);
CHECK_SIZE(fpga_configuration_entry, 8);
CHECK_FIELD(fpga_configuration_table, entries, 0x02);

/* status_create allocates 0x16 bytes and four a register; the first
 * four of those are registers[0]. */
CHECK_SIZE(status_model, 0x12 + 7 * 4);
CHECK_FIELD(status_model, output, 0x02);
CHECK_FIELD(status_model, queue, 0x06);
CHECK_FIELD(status_model, conditions, 0x0a);
CHECK_FIELD(status_model, field_0e, 0x0e);
CHECK_FIELD(status_model, registers, 0x12);

/* The Oncore records, from the cells their messages' decoders fill. */
CHECK_SIZE(oncore_time_fields, 0x100c6c - 0x100c69);
CHECK_SIZE(oncore_gmt_offset_fields, 0x100c6f - 0x100c6c);
CHECK_FIELD(oncore_date_fields, day, 0x100c71 - 0x100c70);
CHECK_FIELD(oncore_date_fields, year, 0x100c72 - 0x100c70);
CHECK_SIZE(oncore_position_fields, 0x100c84 - 0x100c74);
CHECK_FIELD(oncore_position_fields, longitude, 0x100c78 - 0x100c74);
CHECK_FIELD(oncore_position_fields, height_gps, 0x100c7c - 0x100c74);
CHECK_FIELD(oncore_position_fields, height_msl, 0x100c80 - 0x100c74);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x100d12 - 0x100d0e);
CHECK_FIELD(oncore_hold_position_fields, height, 0x100d16 - 0x100d0e);

/* @@Bk: 62 bytes of data in a 69-byte message, as printed. */
CHECK_SIZE(oncore_extension_fields, 69 - 7);
CHECK_FIELD(oncore_extension_fields, tdop, 0x100fb4 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, magnetic_variation, 0x100fb6 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, velocity_north, 0x100fb8 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, correction_age, 0x100fbe - 0x100fac);
CHECK_FIELD(oncore_extension_fields, ecef_x, 0x100fc0 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, ecef_z, 0x100fc8 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, ura, 0x100fcc - 0x100fac);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse,
            0x100fd2 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, clock_bias, 0x100fe6 - 0x100fac);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x100fe8 - 0x100fac);

/* The EFC history: memset(&efc_history, 0, 0x366) at 0x1021a4. */
CHECK_SIZE(efc_ring, 0x366);
CHECK_FIELD(efc_ring, stop_ptr, 0x1021a5 - 0x1021a4);
CHECK_FIELD(efc_ring, loop_settled, 0x1021a6 - 0x1021a4);
CHECK_FIELD(efc_ring, sample_count, 0x1021a8 - 0x1021a4);
CHECK_FIELD(efc_ring, summed_count, 0x1021ac - 0x1021a4);
CHECK_FIELD(efc_ring, averages, 0x1021ae - 0x1021a4);
CHECK_FIELD(efc_ring, weights, 0x1022ae - 0x1021a4);
CHECK_FIELD(efc_ring, times, 0x1022ee - 0x1021a4);
CHECK_FIELD(efc_ring, logs, 0x1023ee - 0x1021a4);
CHECK_FIELD(efc_ring, efc_sum, 0x1024ee - 0x1021a4);
CHECK_FIELD(efc_ring, drift, 0x1024f6 - 0x1021a4);
CHECK_FIELD(efc_ring, fit_a, 0x1024fe - 0x1021a4);
CHECK_FIELD(efc_ring, fit_b, 0x102502 - 0x1021a4);
CHECK_FIELD(efc_ring, fit_c, 0x102506 - 0x1021a4);
CHECK_SIZE(aging_curve_state, 0x101ffa - 0x101fd0);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x101fd1 - 0x101fd0);
CHECK_FIELD(aging_curve_state, oldest_time, 0x101fd2 - 0x101fd0);
CHECK_FIELD(aging_curve_state, newest_time, 0x101fd6 - 0x101fd0);
CHECK_FIELD(aging_curve_state, a, 0x101fe2 - 0x101fd0);
CHECK_FIELD(aging_curve_state, b, 0x101fe6 - 0x101fd0);
CHECK_FIELD(aging_curve_state, c, 0x101fea - 0x101fd0);
CHECK_FIELD(aging_curve_state, rms, 0x101ff2 - 0x101fd0);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x101ff6 - 0x101fd0);

/* The GPS task's state, memset(&gps, 0, 0x55a) at 0x100c68, with each
 * message's record at the cell its decoder fills. */
CHECK_SIZE(gps_state, 0x55a);
CHECK_FIELD(gps_state, time, 0x100c69 - 0x100c68);
CHECK_FIELD(gps_state, date, 0x100c70 - 0x100c68);
CHECK_FIELD(gps_state, position, 0x100c74 - 0x100c68);
CHECK_FIELD(gps_state, mask_angle, 0x100c84 - 0x100c68);
CHECK_FIELD(gps_state, manual_satellites, 0x100c86 - 0x100c68);
CHECK_FIELD(gps_state, xdop_type, 0x100c8c - 0x100c68);
CHECK_FIELD(gps_state, xdop_hysteresis, 0x100c8e - 0x100c68);
CHECK_FIELD(gps_state, ignored_satellites, 0x100c94 - 0x100c68);
CHECK_FIELD(gps_state, almanac_update, 0x100cf6 - 0x100c68);
CHECK_FIELD(gps_state, ionospheric_correction, 0x100d0c - 0x100c68);
CHECK_FIELD(gps_state, hold_position, 0x100d0e - 0x100c68);
CHECK_FIELD(gps_state, position_hold, 0x100d1c - 0x100c68);
CHECK_FIELD(gps_state, altitude_hold, 0x100d23 - 0x100c68);
CHECK_FIELD(gps_state, epoch_offset, 0x100d26 - 0x100c68);
CHECK_FIELD(gps_state, pps_offset, 0x100d28 - 0x100c68);
CHECK_FIELD(gps_state, pps_cable_delay, 0x100d2c - 0x100c68);
CHECK_FIELD(gps_state, application_type, 0x100d31 - 0x100c68);
CHECK_FIELD(gps_state, position_fix_threshold, 0x100d32 - 0x100c68);
CHECK_FIELD(gps_state, status, 0x100d36 - 0x100c68);
CHECK_FIELD(gps_state, status.latitude, 0x100d42 - 0x100c68);
CHECK_FIELD(gps_state, status.channels, 0x100d5c - 0x100c68);
CHECK_FIELD(gps_state, status.receiver_status, 0x100d74 - 0x100c68);
CHECK_FIELD(gps_state, visibility_received, 0x100d78 - 0x100c68);
CHECK_FIELD(gps_state, visibility, 0x100d7a - 0x100c68);
CHECK_FIELD(gps_state, visibility.satellites[0].satellite, 0x100d7c - 0x100c68);
CHECK_FIELD(gps_state, visibility.satellites[0].doppler, 0x100d7e - 0x100c68);
CHECK_FIELD(gps_state, visibility.satellites[0].elevation, 0x100d80 - 0x100c68);
CHECK_FIELD(gps_state, visibility.satellites[0].azimuth, 0x100d82 - 0x100c68);
CHECK_FIELD(gps_state, xdop_table, 0x100df5 - 0x100c68);
CHECK_FIELD(gps_state, xdop_table.combinations, 0x100df8 - 0x100c68);
CHECK_FIELD(gps_state, almanac, 0x100e42 - 0x100c68);
CHECK_FIELD(gps_state, corrections, 0x100f10 - 0x100c68);
CHECK_FIELD(gps_state, corrections.channels, 0x100f14 - 0x100c68);
CHECK_FIELD(gps_state, corrections.channels[0].range_correction,
            0x100f16 - 0x100c68);
CHECK_FIELD(gps_state, raim, 0x100f63 - 0x100c68);
CHECK_FIELD(gps_state, raim.sawtooth, 0x100f7c - 0x100c68);
CHECK_FIELD(gps_state, leap_second_pending, 0x100fa2 - 0x100c68);
CHECK_FIELD(gps_state, self_test, 0x100fa4 - 0x100c68);
CHECK_FIELD(gps_state, extension, 0x100fac - 0x100c68);
CHECK_FIELD(gps_state, fix_mode, 0x101054 - 0x100c68);
CHECK_FIELD(gps_state, receiver_id_received, 0x101056 - 0x100c68);
CHECK_FIELD(gps_state, receiver_id, 0x101057 - 0x100c68);
CHECK_FIELD(gps_state, average_count, 0x10116e - 0x100c68);
CHECK_FIELD(gps_state, average_latitude, 0x101172 - 0x100c68);
CHECK_FIELD(gps_state, average_height_msl, 0x10118a - 0x100c68);
CHECK_SIZE(oncore_status_fields, 63);
CHECK_SIZE(oncore_visibility_fields, 0x100df4 - 0x100d7a);
CHECK_SIZE(oncore_xdop_table_fields, 75);
CHECK_SIZE(oncore_corrections_fields, 64);
CHECK_SIZE(oncore_raim_fields, 63);
CHECK_SIZE(oncore_almanac_status_fields, 16);

/* The loop's state block, memset(&pll, 0, 0x3d6) at 0x10215a. */
CHECK_SIZE(pll_state, 0x3d6);
CHECK_FIELD(pll_state, manual, 0x12);
CHECK_FIELD(pll_state, recovery_state, 0x15);
CHECK_FIELD(pll_state, fine_state, 0x17);
CHECK_FIELD(pll_state, efc, 0x10218c - 0x10215a);
CHECK_FIELD(pll_state, tfom, 0x102190 - 0x10215a);
CHECK_FIELD(pll_state, efc_history, 0x1021a4 - 0x10215a);

/* Records the printf calls name. */
CHECK_SIZE(oncore_receiver_id, 277);
CHECK_FIELD(oncore_receiver_id, software_part_number, 0x101079 - 0x101057);
CHECK_FIELD(oncore_receiver_id, software_version, 0x101094 - 0x101057);
CHECK_FIELD(oncore_receiver_id, model_number, 0x1010e5 - 0x101057);
CHECK_FIELD(oncore_receiver_id, options, 0x101151 - 0x101057);
CHECK_FIELD(gps_state, correction_average, 0x100f50 - 0x100c68);
CHECK_FIELD(gps_state, correction_deviation, 0x100f58 - 0x100c68);
CHECK_FIELD(pll_state, fine_index, 0x102173 - 0x10215a);
CHECK_FIELD(pll_state, fine_d, 0x102176 - 0x10215a);
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
CHECK_FIELD(pll_state, led_gps_lock, 0x10251e - 0x10215a);
CHECK_FIELD(pll_state, holdover_duration, 0x3c8);
CHECK_FIELD(pll_state, reference_uptime, 0x3cc);
CHECK_FIELD(pll_state, reference_seconds, 0x3d0);
CHECK_FIELD(pll_state, sync_requested, 0x10252e - 0x10215a);
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
CHECK_FIELD(pll_state, fpga_configuration, 0x3d1);
CHECK_FIELD(pll_state, loop_running, 0x3d3);
CHECK_SIZE(gps_statistics, 0x36);
CHECK_FIELD(gps_statistics, successes, 0x1017c6 - 0x1017ba);
CHECK_FIELD(gps_statistics, rejects, 0x1017ca - 0x1017ba);
CHECK_FIELD(gps_statistics, t_retries, 0x1017e0 - 0x1017ba);
CHECK_FIELD(gps_statistics, raim_detected, 0x1017e2 - 0x1017ba);
CHECK_FIELD(gps_statistics, raim_alarms, 0x1017e4 - 0x1017ba);
CHECK_FIELD(gps_statistics, unknown_response, 0x1017e6 - 0x1017ba);
CHECK_FIELD(gps_statistics, checksum_errors, 0x1017ea - 0x1017ba);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x1017ec - 0x1017ba);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x1017ee - 0x1017ba);
CHECK_SIZE(exception_record, 0x1009b0 - 0x10099e);
CHECK_FIELD(exception_record, name, 0x10099f - 0x10099e);
CHECK_FIELD(exception_record, sr, 0x1009a4 - 0x10099e);
CHECK_FIELD(exception_record, pc, 0x1009a6 - 0x10099e);
CHECK_FIELD(exception_record, vector, 0x1009aa - 0x10099e);
CHECK_FIELD(exception_record, address, 0x1009ac - 0x10099e);
/* The pool: five entries from 0x1035a6 to 0x103628. */
CHECK_SIZE(klok_entry, (0x103628 - 0x1035a6) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read: entry n's fields at 0x101d96, 0x101d9a, 0x101d9e, ... + n * 0x14.
 */
CHECK_SIZE(log_page, 0x101dd2 - 0x101d92);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x101d96 - 0x101d92);
CHECK_FIELD(log_entry, format, 0x101d9e - 0x101d96);
CHECK_FIELD(log_entry, argument_1, 0x101da2 - 0x101d96);
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

/* nv_settings at 0x101e88, each field where its descriptor's value
 * points, and the two descriptors' sizes. */
CHECK_SIZE(nv_settings_fields, 0xc5);
/* each is followed by the next descriptor's widths */
CHECK_SIZE(scpi_setting_param, 0x32a34 - 0x32a16 - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* the group cells, from the descriptors' pointers up to interval_reading */
CHECK_SIZE(scpi_group_cells_fields, 0x101f94 - 0x101f8a);
CHECK_FIELD(scpi_group_cells_fields, pulse, 0x101f8c - 0x101f8a);
CHECK_FIELD(scpi_group_cells_fields, serial_2, 0x101f8e - 0x101f8a);
CHECK_FIELD(scpi_group_cells_fields, status, 0x101f90 - 0x101f8a);
CHECK_FIELD(scpi_group_cells_fields, rest, 0x101f92 - 0x101f8a);
/* 13 action descriptors, each referred to at its own address, from
 * 0x4135c to efc_history_update at 0x413aa */
CHECK_SIZE(scpi_action_param, (0x413aa - 0x4135c) / 13);
CHECK_FIELD(scpi_action_param, action, 0x02);
/* pllc_request_a's frame, link -24, and pll_scpi_receive's; the kernel's
 * message buffers are 0x18 bytes */
CHECK_SIZE(pllc_request, 0x18);
CHECK_FIELD(pllc_request, param, 0x08);
CHECK_FIELD(pllc_request, index, 0x0c);
CHECK_FIELD(pllc_request, source, 0x0d);
CHECK_FIELD(pllc_request, setting, 0x0f);
CHECK_FIELD(pllc_request, values, 0x10);
/* the Oncore message table: 49 entries from 0x4fe8c to the ID strings
 * at 0x50944; gps_response_timed_out reads +0x1a, gps_response_lookup
 * +0x1c, gps_response_match +0x1e, gps_response_decode +0x20 and +0x24 */
CHECK_SIZE(oncore_decoder, 8);
CHECK_SIZE(oncore_message_entry, (0x50944 - 0x4fe8c) / 49);
CHECK_FIELD(oncore_message_entry, kind, 0x4fe90 - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, encoders, 0x4fe92 - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, query, 0x4fea2 - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, timeout, 0x4fea6 - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, length, 0x4fea8 - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, match_kind, 0x4feaa - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, decoders, 0x4feac - 0x4fe8c);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x4feb0 - 0x4fe8c);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x101e8a - 0x101e88);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x101e96 - 0x101e88);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x101e98 - 0x101e88);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x101ea4 - 0x101e88);
CHECK_FIELD(nv_settings_fields, position, 0x101ea8 - 0x101e88);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x101eb4 - 0x101e88);
CHECK_FIELD(nv_settings_fields, time_zone, 0x101eb8 - 0x101e88);
CHECK_FIELD(nv_settings_fields, leap_date, 0x101ec4 - 0x101e88);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x101ecc - 0x101e88);
CHECK_FIELD(nv_settings_fields, raim_state, 0x101ecd - 0x101e88);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x101ed0 - 0x101e88);
CHECK_FIELD(nv_settings_fields, status_enable, 0x101edc - 0x101e88);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x101ef8 - 0x101e88);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x101f0c - 0x101e88);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x101f20 - 0x101e88);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x101f24 - 0x101e88);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x101f34 - 0x101e88);
CHECK_FIELD(nv_settings_fields, field_bc, 0x101f44 - 0x101e88);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x101f4b - 0x101e88);
CHECK_FIELD(nv_settings_fields, debug_output, 0x101f4c - 0x101e88);

/* The time-interval counter's two records: the reading at 0x101f94, its
 * flag at 0x101f9c and armed flag at 0x101fa0; the configuration at
 * 0x101fa6, ending where interpolator_calibration_state begins. */
CHECK_SIZE(interval_reading_fields, 0x101fa1 - 0x101f94);
CHECK_FIELD(interval_reading_fields, valid, 0x101f9c - 0x101f94);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x101fa0 - 0x101f94);
CHECK_SIZE(counter_configuration_fields, 0x101fc6 - 0x101fa6);
CHECK_FIELD(counter_configuration_fields, calibration, 4);
CHECK_FIELD(counter_configuration_fields, registers_mode, 0xc);
CHECK_FIELD(counter_configuration_fields, calibrate_state, 0xf);
CHECK_FIELD(counter_configuration_fields, sync_mode, 0x101fb6 - 0x101fa6);
CHECK_FIELD(counter_configuration_fields, sync_kind, 0x101fb8 - 0x101fa6);
CHECK_FIELD(counter_configuration_fields, sync_count, 0x101fba - 0x101fa6);
CHECK_FIELD(counter_configuration_fields, interpolator_offset, 0x18);
CHECK_FIELD(counter_configuration_fields, interpolator_scale, 0x1c);

/* The preserved region at 0x100000, the 0x94e bytes record_checksum
 * sums: gps's copy at 0x100004, pll's at 0x10055e, the block from
 * 0x101e6e at 0x100934; crash_message after it. */
CHECK_SIZE(preserved_region_fields, 0x10094e - 0x100000);
CHECK_FIELD(preserved_region_fields, restore, 0x100002 - 0x100000);
CHECK_FIELD(preserved_region_fields, gps, 0x100004 - 0x100000);
CHECK_FIELD(preserved_region_fields, pll, 0x10055e - 0x100000);
CHECK_FIELD(preserved_region_fields, values, 0x100934 - 0x100000);
CHECK_SIZE(((preserved_region_fields*)0)->values, 0x101e87 - 0x101e6e);

/* The clock record at 0x1009b8: the routines' offsets, and the
 * addresses get_date_time, get_gps_time and clock_init use. */
CHECK_SIZE(clock_record, 0x1009eb - 0x1009b8);
CHECK_FIELD(clock_record, uptime, 0x1009ba - 0x1009b8);
CHECK_FIELD(clock_record, time, 0x1009be - 0x1009b8);
CHECK_FIELD(clock_record, date, 0x1009c2 - 0x1009b8);
CHECK_FIELD(clock_record, month_days, 0x1009c6 - 0x1009b8);
CHECK_FIELD(clock_record, gps_time, 0x1009d2 - 0x1009b8);
CHECK_FIELD(clock_record, gps_date, 0x1009d6 - 0x1009b8);
CHECK_FIELD(clock_record, gps_month_days, 0x1009da - 0x1009b8);
CHECK_FIELD(clock_record, minute_length, 0x1009e6 - 0x1009b8);
CHECK_FIELD(clock_record, set, 0x1009e7 - 0x1009b8);
CHECK_FIELD(clock_record, gps_utc_offset, 0x1009e8 - 0x1009b8);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x1009ea - 0x1009b8);

/* pps_message at 0x100bac, pps_exchange after the cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x100bc4 - 0x100bac);
CHECK_FIELD(pps_exchange_message, gps_time, 0x100bb4 - 0x100bac);
CHECK_FIELD(pps_exchange_message, gps_date, 0x100bb8 - 0x100bac);

CHECK_SIZE(gps_process_state, 1);

/* the three status snapshots, 0x10142a, 0x10155e and 0x10168c, with the
 * two cells at 0x101558 between the first two */
CHECK_SIZE(status_snapshot_record, 0x10168c - 0x10155e);
CHECK_SIZE(status_snapshot_record[2], 0x1017ba - 0x10155e);
CHECK_FIELD(status_snapshot_record, interval, 0x0a);
CHECK_FIELD(status_snapshot_record, holdover_duration, 0x22);
CHECK_FIELD(status_snapshot_record, satellites, 0x32);
/* status_screen_mode_lines' five entries, the last a terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x4d00e - 0x4cff0);
CHECK_FIELD(status_screen_mode_line, text, 0x02);
CHECK_FIELD(status_snapshot_record, not_tracked, 0x5c);
CHECK_FIELD(status_snapshot_record, tracked, 0xbc);
CHECK_FIELD(status_snapshot_record, status_received, 0xec);
CHECK_FIELD(status_snapshot_record, time, 0xf8);
CHECK_FIELD(status_snapshot_record, time_zone, 0x100);
CHECK_FIELD(status_snapshot_record, from_clock, 0x108);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 0x10c);
CHECK_FIELD(status_snapshot_record, position_source, 0x114);
CHECK_FIELD(status_snapshot_record, average_count, 0x124);
CHECK_FIELD(status_snapshot_record, hardware_condition, 0x12c);

/* the event log's write page at 0x101d4c, its number after it */
CHECK_SIZE(log_page, 0x101d8c - 0x101d4c);

/* the MC68331's modules: Tables D-2, D-3 and D-13 */
CHECK_SIZE(gpt_registers, 0x2e);
CHECK_FIELD(gpt_registers, tcnt, 0xfff90a - 0xfff900);
CHECK_FIELD(gpt_registers, toc, 0xfff914 - 0xfff900);
CHECK_FIELD(gpt_registers, tmsk, 0xfff920 - 0xfff900);
CHECK_FIELD(gpt_registers, tflg, 0xfff922 - 0xfff900);
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

CHECK_SIZE(time_of_day, 4);
CHECK_FIELD(time_of_day, second, 3);
CHECK_SIZE(calendar_date, 4);
CHECK_FIELD(calendar_date, day, 3);
CHECK_FIELD(oncore_status_fields, time, 4);
CHECK_FIELD(oncore_status_fields, fraction, 8);
CHECK_SIZE(scpi_number, 0x26);
CHECK_FIELD(scpi_number, exponent, 0x12);
CHECK_FIELD(scpi_number, suffix, 0x16);
CHECK_FIELD(scpi_number, scale, 0x22);
CHECK_FIELD(scpi_number, unit, 0x24);
CHECK_SIZE(scpi_token, 0x26);
CHECK_SIZE(scpi_port_methods, 0x44);
CHECK_FIELD(scpi_port_methods, signal, 0x28);
CHECK_FIELD(scpi_port_methods, status_set_bits, 0x30);
CHECK_SIZE(satellite_sets, 0x2a);
CHECK_FIELD(satellite_sets, acquiring, 0x16);
CHECK_FIELD(satellite_sets, visible_count, 0x26);
CHECK_SIZE(gps_request_entry, 0x1a);
CHECK_SIZE(gps_running_command, 0x72);
CHECK_FIELD(gps_running_command, script, 0x1a);
CHECK_FIELD(gps_running_command, message, 0x1e);
CHECK_FIELD(gps_running_command, sent, 0x22);
CHECK_SIZE(reset_record, 0x5c);

CHECK_SIZE(scpi_bcd, 0x0c);
CHECK_FIELD(scpi_bcd, digits, 0x02);
CHECK_FIELD(scpi_bcd, exponent, 0x0a);
CHECK_SIZE(scpi_arena, 0x08);
CHECK_FIELD(scpi_arena, last, 0x04);
CHECK_SIZE(scpi_choices, 0x08);
CHECK_FIELD(scpi_choices, values, 0x04);
CHECK_SIZE(scpi_parameter_kinds, 0x24);
CHECK_FIELD(scpi_parameter_kinds, optional, 0x02);
CHECK_FIELD(scpi_parameter_kinds, units, 0x04);
CHECK_FIELD(scpi_parameter_kinds, choices, 0x08);
CHECK_FIELD(scpi_parameter_kinds, converters, 0x0c);
CHECK_SIZE(scpi_parameter, 0x08);
CHECK_FIELD(scpi_parameter, kinds, 0x04);
CHECK_SIZE(scpi_handler, 10);
CHECK_FIELD(scpi_handler, parameters, 0x04);
CHECK_FIELD(scpi_handler, parameters_size, 0x08);
CHECK_SIZE(scpi_response_item, 10);
CHECK_FIELD(scpi_response_item, size, 0x04);
CHECK_FIELD(scpi_response_item, choices, 0x06);
CHECK_SIZE(scpi_node, 0x32);
CHECK_FIELD(scpi_node, children, 0x04);
CHECK_FIELD(scpi_node, handlers, 0x08);
CHECK_FIELD(scpi_node, response_items, 0x1c);
CHECK_FIELD(scpi_node, response_size, 0x20);
CHECK_FIELD(scpi_node, path_word, 0x22);
CHECK_FIELD(scpi_node, unused, 0x24);
CHECK_SIZE(scpi_child_list, 0x06);
CHECK_FIELD(scpi_child_list, count, 0x02);
CHECK_FIELD(scpi_child_list, default_child, 0x04);
CHECK_FIELD(scpi_child_list, children, 0x06);
CHECK_SIZE(scpi_path, 20);
CHECK_FIELD(scpi_path, count, 0x04);
CHECK_FIELD(scpi_path, words, 0x06);
CHECK_SIZE(scpi_call, 0x3a);
CHECK_FIELD(scpi_call, query, 0x04);
CHECK_FIELD(scpi_call, arena, 0x06);
CHECK_FIELD(scpi_call, parameters, 0x0a);
CHECK_FIELD(scpi_call, response, 0x0e);
CHECK_FIELD(scpi_call, input, 0x12);
CHECK_FIELD(scpi_call, output, 0x16);
CHECK_FIELD(scpi_call, status, 0x1a);
CHECK_FIELD(scpi_call, result, 0x1e);
CHECK_FIELD(scpi_call, main_tree, 0x22);
CHECK_FIELD(scpi_call, common_list, 0x26);
CHECK_FIELD(scpi_call, responding, 0x2a);
CHECK_FIELD(scpi_call, field_34, 0x34);
CHECK_FIELD(scpi_call, last_argument, 0x36);
CHECK_SIZE(scpi_context, 0x22);
CHECK_FIELD(scpi_context, word, 0x04);
CHECK_FIELD(scpi_context, path, 0x06);
CHECK_FIELD(scpi_context, call, 0x0a);
CHECK_FIELD(scpi_context, arena, 0x0e);
CHECK_FIELD(scpi_context, main_tree, 0x12);
CHECK_FIELD(scpi_context, common_list, 0x16);
CHECK_FIELD(scpi_context, field_1e, 0x1e);
CHECK_SIZE(scpi_array, 0x08);
CHECK_FIELD(scpi_array, values, 0x04);
CHECK_SIZE(scpi_resolution_array, 0x0c);
CHECK_FIELD(scpi_resolution_array, values, 0x04);
CHECK_FIELD(scpi_resolution_array, resolutions, 0x08);
CHECK_SIZE(scpi_block, 0x08);
CHECK_FIELD(scpi_block, data, 0x04);
CHECK_SIZE(scpi_array_record, 0x06);
CHECK_FIELD(scpi_array_record, array, 0x02);
CHECK_SIZE(scpi_precise_double, 0x0c);
CHECK_FIELD(scpi_precise_double, to_digits, 0x08);
CHECK_FIELD(scpi_precise_double, precision, 0x0a);
CHECK_SIZE(scpi_float_resolution, 0x06);
CHECK_FIELD(scpi_float_resolution, resolution, 0x04);
CHECK_SIZE(status_error_entry, 0x06);
CHECK_FIELD(status_error_entry, value, 0x02);
CHECK_SIZE(status_error_queue, 0x0a);
CHECK_FIELD(status_error_queue, tail, 0x02);
CHECK_FIELD(status_error_queue, status, 0x04);
CHECK_FIELD(status_error_queue, size, 0x08);
CHECK_FIELD(status_error_queue, entries, 0x0a);
CHECK_FIELD(psos_task, slice_ticks, 0x1a);
CHECK_FIELD(psos_task, list_next, 0x30);
CHECK_FIELD(psos_task, timer_ticks, 0x4a);
CHECK_FIELD(psos_task, exception_hook, 0x82);
CHECK_FIELD(psos_task, ticks, 0xae);
CHECK_SIZE(psos_task, 0xb2);
CHECK_SIZE(psos_exchange, 0x22);
CHECK_FIELD(psos_kernel, region_waiting, 0x64);
CHECK_SIZE(status_register_layout, 22);
CHECK_SIZE(satellite_cell, 4);
CHECK_SIZE(satellite_list_entry, 8);

/* The ROM's packed script steps, each to the next (0x50a04 to 0x50c80). */
CHECK_SIZE(gps_script_query_step, 0x50af4 - 0x50af0);
CHECK_SIZE(gps_script_step_1_argument, 0x50a0c - 0x50a04);
CHECK_SIZE(gps_script_step_2_arguments, 0x50a20 - 0x50a14);
CHECK_SIZE(gps_script_step_3_arguments, 0x50a4c - 0x50a3c);
CHECK_SIZE(gps_script_step, 0x50a34 - 0x50a20);
CHECK_SIZE(io_device_stubs, 0x51768 - 0x51744);
CHECK_SIZE(io_device_stubs, sizeof(io_device_jumps));
CHECK_SIZE(pforth_word_header, 0x1aa22 - 0x1aa16);
CHECK_FIELD(pforth_word_header, flags, 0x1aa20 - 0x1aa16);
CHECK_SIZE(diag_word_entry, 0x1c5da - 0x1c5b4);
CHECK_SIZE(diag_word_entry[86], 0x1d278 - 0x1c5b4);
CHECK_FIELD(diag_word_entry, argument_count, 0x1e);
CHECK_FIELD(diag_word_entry, result_count, 0x22);

/* The fields common/'s C reaches through a global, whose offsets an
 * absolute address in the image's code holds, where rule 1's masking of
 * addresses cannot see them: each where the image's code names it, or,
 * where it does not, at its offset in the global (make check-common;
 * docs/firmware/rewrite.md, "Shared C"). */
CHECK_FIELD(boot_selftest_results, checksum, 0x10ab20 - 0x10ab20);
CHECK_FIELD(counter_configuration_fields, sync_state, 0x101fb7 - 0x101fa6);
CHECK_FIELD(duarta_driver, held, 0x102fba - 0x102fb2);
CHECK_FIELD(duarta_driver, transmit_count, 0x102fb6 - 0x102fb2);
CHECK_FIELD(duarta_driver, transmit_next, 0x102fb2 - 0x102fb2);
CHECK_FIELD(duartb_applied_settings, baud, 0x102e9e - 0x102e9e);
CHECK_FIELD(duartb_applied_settings, baud, 0x10ad3c - 0x10ad3c);
CHECK_FIELD(eeprom_lock_token, exchange, 0x101d1e - 0x101d1a);
CHECK_FIELD(eeprom_lock_token, status, 0x101d1a - 0x101d1a);
CHECK_FIELD(efc_range_state, clamped, 0x101cf2 - 0x101cf2);
CHECK_FIELD(efc_range_state, near_end, 0x101cf3 - 0x101cf2);
CHECK_FIELD(exception_record, pending, 0x10099e - 0x10099e);
CHECK_FIELD(fpga_configuration_table, last, 0x20cf0 - 0x20cf0);
CHECK_FIELD(gps_command_log, entries, 0x1017f4 - 0x1017f4);
CHECK_FIELD(gps_command_mailbox, code, 0x102562 - 0x102562);
CHECK_FIELD(holdover_steering_state, ti_largest, 0x10204e - 0x10203a);
CHECK_FIELD(holdover_steering_state, ti_weighted_sum, 0x10204a - 0x10203a);
CHECK_FIELD(identification_record, checksum, 0x400010 - 0x400010);
CHECK_FIELD(identification_record, model, 0x400012 - 0x400010);
CHECK_FIELD(identification_record, serial, 0x400024 - 0x400010);
CHECK_FIELD(interpolator_calibration, calibrated, 0x101fc7 - 0x101fc6);
CHECK_FIELD(interpolator_calibration, reading_low, 0x101fcd - 0x101fc6);
CHECK_FIELD(interval_reading_fields, value, 0x101f94 - 0x101f94);
CHECK_FIELD(lifetime_state, efc, 0x101cfe - 0x101cfe);
CHECK_FIELD(psos_configuration, data_area, 0x10261e - 0x10261e);
CHECK_FIELD(qspi_state, copy_pending, 0x10316e - 0x103122);
CHECK_FIELD(qspi_state, field_00, 0x103122 - 0x103122);
CHECK_FIELD(qspi_state, read, 0x103124 - 0x103122);
CHECK_FIELD(qspi_state, receive_count, 0x10316c - 0x103122);
CHECK_FIELD(qspi_state, write, 0x103123 - 0x103122);
CHECK_FIELD(serial_ring, data, 0x102eb1 - 0x102eaf);
CHECK_FIELD(serial_ring, read, 0x102eb0 - 0x102eaf);
CHECK_FIELD(serial_ring, write, 0x102eaf - 0x102eaf);
CHECK_FIELD(task_table_entry, state, 0x102688 - 0x102688);
