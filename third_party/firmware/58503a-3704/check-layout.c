/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  This is the 58503A 3704's copy: the
 * 58503A's own layouts (docs/firmware/58503a-3704.md, "The
 * structures") are checked at the 58503A's addresses, each block's comment
 * saying so; the rest are still the Z3805A's figures, for a layout the
 * two share.  Compiled, never run, by "make
 * check-types" with an m68k compiler, so that int, long and pointers
 * are the target's 32 bits. */

#include <stddef.h>

#include "smartclock.h"
#include "../include/mc68331.h"
#include "../include/mc68681.h"

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

/* The 58503A 3704's duartb at 0x103412, each field where the driver's
 * code addresses it (duart_init, drtb_puts, drtb_gets, drtb_send_async,
 * duartb_isr, duartb_transmit_interrupt), echo and last_terminator
 * where drtb_gets loads them (0x1d642); the installer's at 0x10af0c, as
 * its copies address it (0x10361c to 0x103cbc in its RAM). */
CHECK_SIZE(duartb_driver, 0x103468 - 0x103412);
CHECK_FIELD(duartb_driver, transmit_count, 0x103416 - 0x103412);
CHECK_FIELD(duartb_driver, loopback_test, 0x103418 - 0x103412);
CHECK_FIELD(duartb_driver, least_free, 0x103419 - 0x103412);
CHECK_FIELD(duartb_driver, reconfiguring, 0x10341a - 0x103412);
CHECK_FIELD(duartb_driver, flow, 0x10341b - 0x103412);
CHECK_FIELD(duartb_driver, xon_xoff, 0x10341c - 0x103412);
CHECK_FIELD(duartb_driver, in_transmit_interrupt, 0x10341d - 0x103412);
CHECK_FIELD(duartb_driver, read_errors, 0x10341e - 0x103412);
CHECK_FIELD(duartb_driver, parity, 0x10341f - 0x103412);
CHECK_FIELD(duartb_driver, async_count, 0x103420 - 0x103412);
CHECK_FIELD(duartb_driver, async_pending, 0x103421 - 0x103412);
CHECK_FIELD(duartb_driver, async_next, 0x103422 - 0x103412);
CHECK_FIELD(duartb_driver, read_exchange, 0x103426 - 0x103412);
CHECK_FIELD(duartb_driver, write_exchange, 0x10342a - 0x103412);
CHECK_FIELD(duartb_driver, read_token, 0x10342e - 0x103412);
CHECK_FIELD(duartb_driver, write_token, 0x103446 - 0x103412);
CHECK_FIELD(duartb_driver, async_allowed, 0x10345e - 0x103412);
CHECK_FIELD(duartb_driver, field_4d, 0x10345f - 0x103412);
CHECK_FIELD(duartb_driver, echo, 0x103462 - 0x103412);
CHECK_FIELD(duartb_driver, last_terminator, 0x103466 - 0x103412);
CHECK_FIELD(duartb_driver, transmit_count, 0x10af10 - 0x10af0c);
CHECK_FIELD(duartb_driver, reconfiguring, 0x10af14 - 0x10af0c);
CHECK_FIELD(duartb_driver, read_errors, 0x10af18 - 0x10af0c);
CHECK_FIELD(duartb_driver, async_next, 0x10af1c - 0x10af0c);
CHECK_FIELD(duartb_driver, read_token, 0x10af28 - 0x10af0c);
CHECK_FIELD(duartb_driver, write_token, 0x10af40 - 0x10af0c);
CHECK_FIELD(duartb_driver, async_allowed, 0x10af58 - 0x10af0c);
CHECK_FIELD(duartb_driver, echo, 0x10af5c - 0x10af0c);
CHECK_FIELD(duartb_driver, last_terminator, 0x10af60 - 0x10af0c);

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
 * them: 0x62 bytes to 0x10264c and 0x90 to 0x1025bc. */
CHECK_SIZE(psos_configuration, 0x1026ae - 0x10264c);
CHECK_FIELD(psos_configuration, data_end, 0x102650 - 0x10264c);
CHECK_FIELD(psos_configuration, region_2_start, 0x102658 - 0x10264c);
CHECK_FIELD(psos_configuration, task_records, 0x102664 - 0x10264c);
CHECK_FIELD(psos_configuration, exchange_records, 0x102666 - 0x10264c);
CHECK_FIELD(psos_configuration, message_buffers, 0x102668 - 0x10264c);
CHECK_FIELD(psos_configuration, last_device, 0x10266e - 0x10264c);
CHECK_FIELD(psos_configuration, io_table, 0x102670 - 0x10264c);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x102674 - 0x10264c);
CHECK_FIELD(psos_configuration, root_entry, 0x102678 - 0x10264c);
CHECK_FIELD(psos_configuration, root_priority, 0x102680 - 0x10264c);
CHECK_FIELD(psos_configuration, start_hook, 0x102682 - 0x10264c);
CHECK_FIELD(psos_configuration, delete_hook, 0x102686 - 0x10264c);
CHECK_FIELD(psos_configuration, debug_hooks, 0x10268e - 0x10264c);
CHECK_FIELD(psos_configuration, task_hooks, 0x102692 - 0x10264c);
CHECK_SIZE(io_device_jumps[4], 0x10264c - 0x1025bc);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x1026b8 + 6n, its state at 0x1026b6 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x1026b8 - 0x1026b6);
CHECK_SIZE(task_table_entry[17], 0x10271c - 0x1026b6);

CHECK_SIZE(spoo_request, 0x10371a - 0x103702);
CHECK_FIELD(spoo_request, function, 0x10370a - 0x103702);
CHECK_FIELD(spoo_request, arguments, 0x10370e - 0x103702);
CHECK_SIZE(early_status_events, 0x102ec4 - 0x102ebc);
CHECK_SIZE(early_status_events[2], 0x102ecc - 0x102ebc);
CHECK_FIELD(early_status_events, events, 0x02);

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
CHECK_SIZE(gps_request_code_word, 0x101b58 - 0x101b56);
CHECK_SIZE(gps_script_step, 0x101b6a - 0x101b56);
CHECK_FIELD(gps_script_step, argument_mode, 0x101b58 - 0x101b56);
CHECK_FIELD(gps_script_step, arguments, 0x101b5a - 0x101b56);

/* the 58503A 3704's, at 0x100e02, each field where the tests address it:
 * selftest_abort at +0x20 (0x100e22), the GPS's long at +0x14
 * (0x18edc), lamp_test's flag at +0x36 (0x1866e) */
CHECK_SIZE(selftest_results, 0x48);
CHECK_FIELD(selftest_results, uart, 0x0a);
CHECK_FIELD(selftest_results, latch, 0x100e0e - 0x100e02);
CHECK_FIELD(selftest_results, lamp_mask, 0x0d);
CHECK_FIELD(selftest_results, summary, 0x0e);
CHECK_FIELD(selftest_results, gps, 0x100e16 - 0x100e02);
CHECK_FIELD(selftest_results, interpolator, 0x18);
CHECK_FIELD(selftest_results, abort, 0x100e22 - 0x100e02);
CHECK_FIELD(selftest_results, running, 0x100e23 - 0x100e02);
CHECK_FIELD(selftest_results, internal_reference, 0x24);
CHECK_FIELD(selftest_results, qspi, 0x26);
CHECK_FIELD(selftest_results, lamp_testing, 0x100e38 - 0x100e02);
CHECK_FIELD(selftest_results, passed, 0x38);
CHECK_FIELD(selftest_results, result, 0x3a);
CHECK_FIELD(selftest_results, details, 0x3e);
CHECK_FIELD(selftest_results, test, 0x46);

CHECK_SIZE(gps_request_entry, 26);
CHECK_FIELD(gps_request_entry, sender, 0x08);
CHECK_FIELD(gps_request_entry, result, 0x09);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, argument_mode, 0x0c);
CHECK_FIELD(gps_request_entry, arguments, 0x0e);
CHECK_FIELD(gps_request_entry, sent, 0x12);
/* The 58503A 3704's ring, gps_requests at 0x101606: read and write at
 * +0x9c and +0xa0 (request_ring_put, 0x4c3aa). */
CHECK_SIZE(gps_request_ring, 0x1016aa - 0x101606);
CHECK_FIELD(gps_request_ring, read, 0x1016a2 - 0x101606);
CHECK_FIELD(gps_request_ring, write, 0x1016a6 - 0x101606);

CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0a);
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
/* the 58503A 3704's, nv_save's frame (link -160, 0x328bc) and stride */
CHECK_SIZE(nv_record, 0xa0);
CHECK_FIELD(nv_record, sequence, 4);
CHECK_FIELD(nv_record, packed, 0x10);
CHECK_SIZE(reset_record, 0x5c);
CHECK_FIELD(reset_record, text, 0xc);

CHECK_SIZE(sprintf_output, 8);
CHECK_SIZE(scpi_decimal, 20);
CHECK_FIELD(scpi_decimal, exponent, 0x12);
CHECK_SIZE(scpi_multiplier[13], 0x27b62 - 0x27b2e);
CHECK_SIZE(scpi_unit[2], 0x53d7a - 0x53d6e);
CHECK_FIELD(scpi_keyword, text, 2);
CHECK_SIZE(scpi_keyword, 0x14);
CHECK_FIELD(scpi_unit, exponent, 4);
CHECK_SIZE(scpi_error_message[101], 0x28b6e - 0x28910);
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

/* @@Ek: in the 58503A 3704, 64 bytes of data in a 71-byte message, which
 * its decoder's table copies whole (0x525ed); @@Bk's (0x525e8) puts its
 * 38 bytes to the URAs first, zeroes the last two URAs and copies 14
 * more. */
CHECK_SIZE(oncore_extension_fields, 71 - 7);
CHECK_FIELD(oncore_extension_fields, ura, 0x20);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x28);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x3e);

CHECK_SIZE(aging_curve_state, 0x102002 - 0x101fd8);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x101fd9 - 0x101fd8);
CHECK_FIELD(aging_curve_state, oldest_time, 0x101fda - 0x101fd8);
CHECK_FIELD(aging_curve_state, newest_time, 0x101fde - 0x101fd8);
CHECK_FIELD(aging_curve_state, a, 0x101fea - 0x101fd8);
CHECK_FIELD(aging_curve_state, b, 0x101fee - 0x101fd8);
CHECK_FIELD(aging_curve_state, c, 0x101ff2 - 0x101fd8);
CHECK_FIELD(aging_curve_state, rms, 0x101ffa - 0x101fd8);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x101ffe - 0x101fd8);

/* The 58503A 3704's GPS task state at 0x100e50, memset(&gps, 0, 0x5a8)
 * by gps_init (0x4c914): each message's record where the message table
 * (0x51178, 56 entries of 60 bytes) points its decoders, its own state
 * where its code addresses it. */
CHECK_SIZE(gps_state, 0x5a8);
CHECK_FIELD(gps_state, time, 0x100e51 - 0x100e50);
CHECK_FIELD(gps_state, gmt_offset, 0x100e54 - 0x100e50);
CHECK_FIELD(gps_state, date, 0x100e58 - 0x100e50);
CHECK_FIELD(gps_state, position, 0x100e5c - 0x100e50);
CHECK_FIELD(gps_state, mask_angle, 0x100e6c - 0x100e50);
CHECK_FIELD(gps_state, satellite_select, 0x100e6d - 0x100e50);
CHECK_FIELD(gps_state, manual_satellites, 0x100e6e - 0x100e50);
CHECK_FIELD(gps_state, xdop_type, 0x100e76 - 0x100e50);
CHECK_FIELD(gps_state, xdop_hysteresis, 0x100e78 - 0x100e50);
CHECK_FIELD(gps_state, xdop_threshold, 0x100e7a - 0x100e50);
CHECK_FIELD(gps_state, hdop_threshold, 0x100e7c - 0x100e50);
CHECK_FIELD(gps_state, ignored_satellites, 0x100e7e - 0x100e50);
CHECK_FIELD(gps_state, almanac_update, 0x100ee0 - 0x100e50);
CHECK_FIELD(gps_state, ionospheric_correction, 0x100ef6 - 0x100e50);
CHECK_FIELD(gps_state, fix_algorithm, 0x100ef7 - 0x100e50);
CHECK_FIELD(gps_state, hold_position, 0x100ef8 - 0x100e50);
CHECK_FIELD(gps_state, hold_height_type, 0x100f04 - 0x100e50);
CHECK_FIELD(gps_state, position_hold, 0x100f06 - 0x100e50);
CHECK_FIELD(gps_state, altitude_hold_height, 0x100f08 - 0x100e50);
CHECK_FIELD(gps_state, altitude_hold_height_type, 0x100f0c - 0x100e50);
CHECK_FIELD(gps_state, altitude_hold, 0x100f0d - 0x100e50);
CHECK_FIELD(gps_state, utc_time, 0x100f0e - 0x100e50);
CHECK_FIELD(gps_state, epoch_offset, 0x100f10 - 0x100e50);
CHECK_FIELD(gps_state, pps_offset, 0x100f12 - 0x100e50);
CHECK_FIELD(gps_state, pps_cable_delay, 0x100f16 - 0x100e50);
/* @@Az's decoder, 0x5003e, stores the engine's value at 0x100f1a */
CHECK_FIELD(gps_state, pps_cable_delay_given, 0x100f1a - 0x100e50);
CHECK_FIELD(gps_state, application_type, 0x100f1f - 0x100e50);
CHECK_FIELD(gps_state, position_fix_threshold, 0x100f20 - 0x100e50);
CHECK_FIELD(gps_state, differential_output_threshold, 0x100f21 - 0x100e50);
CHECK_FIELD(gps_state, status_received, 0xd2);
CHECK_FIELD(gps_state, status_rate, 0xd3);
CHECK_FIELD(gps_state, status, 0x100f24 - 0x100e50);
CHECK_FIELD(gps_state, status.latitude, 0x100f24 + 0x0c - 0x100e50);
CHECK_FIELD(gps_state, status.channels, 0x100f24 + 0x26 - 0x100e50);
/* the decoders' tables put the receiver status at +0x46 (0x525cc,
 * 0x525d6), which oncore_position_message reads (0x50642) */
CHECK_FIELD(gps_state, status.receiver_status, 0x11a);
CHECK_SIZE(oncore_status_fields, 0x47);
/* @@Bb's decoder, 0x50716, sets the flag and clamps the count after it */
CHECK_FIELD(gps_state, visibility_received, 0x11c);
CHECK_FIELD(gps_state, visibility, 0x100f6e - 0x100e50);
CHECK_FIELD(gps_state, visibility.satellites, 0x100f6e + 2 - 0x100e50);
CHECK_SIZE(oncore_visibility_fields, 0x7a);
CHECK_FIELD(gps_state, xdop_table, 0x100fe9 - 0x100e50);
CHECK_SIZE(oncore_xdop_table_fields, 75);
CHECK_FIELD(gps_state, almanac, 0x101036 - 0x100e50);
CHECK_SIZE(oncore_almanac_status_fields, 16);
CHECK_FIELD(gps_state, corrections, 0x101104 - 0x100e50);
CHECK_SIZE(oncore_corrections_fields, 64);
CHECK_FIELD(gps_state, correction_average, 0x2f4);
CHECK_FIELD(gps_state, correction_deviation, 0x2fc);
CHECK_FIELD(gps_state, raim_received, 0x306);
CHECK_FIELD(gps_state, raim, 0x101157 - 0x100e50);
CHECK_SIZE(oncore_raim_fields, 0x4b);
CHECK_FIELD(gps_state, leap_second_pending, 0x1011a2 - 0x100e50);
CHECK_FIELD(gps_state, self_test, 0x1011a4 - 0x100e50);
CHECK_FIELD(gps_state, extension, 0x1011ac - 0x100e50);
CHECK_FIELD(gps_state, fix_mode, 0x101256 - 0x100e50);
CHECK_FIELD(gps_state, receiver_id_received, 0x408);
CHECK_FIELD(gps_state, receiver_id, 0x101259 - 0x100e50);
/* gps_init clears +0x51e (0x4c922) */
CHECK_FIELD(gps_state, field_51e, 0x51e);
CHECK_FIELD(gps_state, average_count, 0x520);
CHECK_FIELD(gps_state, average_latitude, 0x524);
CHECK_FIELD(gps_state, average_height_msl, 0x53c);
/* the two reference records, 0x4b82a's (0x101394, 0x1013aa) */
CHECK_FIELD(gps_state, references, 0x101394 - 0x100e50);
CHECK_FIELD(gps_state, references[1], 0x1013aa - 0x100e50);
CHECK_SIZE(gps_reference_record, 0x1013aa - 0x101394);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* 0x4b858 stores the GPS's at 0x1013a6 and 0x1013a8 */
CHECK_FIELD(gps_reference_record, hysteresis, 0x1013a6 - 0x101394);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x1013a8 - 0x101394);
CHECK_FIELD(gps_state, surveying, 0x570);
CHECK_FIELD(gps_state, field_571, 0x571);
CHECK_FIELD(gps_state, raim_assessment, 0x572);
CHECK_FIELD(gps_state, pps_check_disabled, 0x1013c3 - 0x100e50);
CHECK_FIELD(gps_state, receiver_health_failed, 0x574);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x576);
CHECK_FIELD(gps_state, command_monitor_state, 0x577);
CHECK_FIELD(gps_state, command_monitor_query, 0x578);
CHECK_FIELD(gps_state, command_monitor_time, 0x57c);
CHECK_FIELD(gps_state, command_monitor_error, 0x1013d0 - 0x100e50);
CHECK_FIELD(gps_state, command_monitor_failed, 0x1013d1 - 0x100e50);
CHECK_FIELD(gps_state, status_monitor_enabled, 0x582);
CHECK_FIELD(gps_state, status_monitor_state, 0x583);
CHECK_FIELD(gps_state, status_monitor_time, 0x584);
CHECK_FIELD(gps_state, status_last_time, 0x588);
CHECK_FIELD(gps_state, status_repeat_count, 0x58c);
CHECK_FIELD(gps_state, status_monitor_timed_out, 0x58d);
CHECK_FIELD(gps_state, status_monitor_failed, 0x58e);
CHECK_FIELD(gps_state, channel_count, 0x1013e0 - 0x100e50);
CHECK_FIELD(gps_state, field_592, 0x1013e2 - 0x100e50);
CHECK_FIELD(gps_state, tracking_log, 0x594);
CHECK_FIELD(gps_state, tracking_log_active, 0x595);
CHECK_FIELD(gps_state, field_596, 0x596);
CHECK_FIELD(gps_state, field_5a2, 0x5a2);
CHECK_FIELD(gps_state, field_5a6, 0x5a6);

/* The 58503A 3704's loop state at 0x102476, memset(&pll, 0, 1000) by
 * pll_init (0x475ac), each field where its code addresses it: the hold
 * causes a word at +0x10 (diag_pr_hold_cause, 0x1b1c2), holdover's state
 * after them (holdover's base +0x10, +2), the EFC history at
 * +0x4c. */
CHECK_SIZE(pll_state, 1000);
CHECK_FIELD(pll_state, previous_stage, 0x01);
CHECK_FIELD(pll_state, powerup_countdown, 0x02);
CHECK_FIELD(pll_state, powerup_state, 0x03);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x04);
CHECK_FIELD(pll_state, reference_check_state, 0x06);
CHECK_FIELD(pll_state, startup_state, 0x0c);
CHECK_FIELD(pll_state, normal_state, 0x0e);
CHECK_FIELD(pll_state, hold_causes, 0x102486 - 0x102476);
CHECK_SIZE(pll_hold_cause, 2);
CHECK_FIELD(pll_state, holdover_state, 0x12);
CHECK_FIELD(pll_state, manual, 0x102489 - 0x102476);
CHECK_FIELD(pll_state, ti_limit_override, 0x10248a - 0x102476);
CHECK_FIELD(pll_state, recovery_countdown, 0x16);
CHECK_FIELD(pll_state, recovery_state, 0x17);
CHECK_FIELD(pll_state, coarse_state, 0x18);
CHECK_FIELD(pll_state, fine_state, 0x19);
CHECK_FIELD(pll_state, frequency_adjusted, 0x1a);
CHECK_FIELD(pll_state, fine_index, 0x1b);
CHECK_FIELD(pll_state, fine_retries, 0x1c);
CHECK_FIELD(pll_state, fine_d, 0x1e);
CHECK_FIELD(pll_state, aging_state, 0x22);
CHECK_FIELD(pll_state, gps_diag_state, 0x26);
CHECK_FIELD(pll_state, gps_init_state, 0x28);
CHECK_FIELD(pll_state, leap_check_timer, 0x2a);
CHECK_FIELD(pll_state, leap_check_requested, 0x2c);
CHECK_FIELD(pll_state, leap_check_state, 0x2d);
CHECK_FIELD(pll_state, leap_ack_state, 0x2e);
CHECK_FIELD(pll_state, leap_ack_again, 0x32);
CHECK_FIELD(pll_state, efc, 0x34);
CHECK_FIELD(pll_state, tfom, 0x38);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x3c);
CHECK_FIELD(pll_state, frequency_tolerance, 0x48);
CHECK_FIELD(pll_state, efc_history, 0x4c);
/* the history 0x36a bytes: efc_history_update's long at +0x34a
 * (0x41518), efc_sum at +0x34e and the rest 4 bytes on */
CHECK_SIZE(efc_ring, 0x36a);
CHECK_FIELD(efc_ring, stop_ptr, 1);
CHECK_FIELD(efc_ring, loop_settled, 2);
CHECK_FIELD(efc_ring, sample_count, 4);
CHECK_FIELD(efc_ring, summed_count, 8);
CHECK_FIELD(efc_ring, averages, 0x0a);
CHECK_FIELD(efc_ring, weights, 0x10a);
CHECK_FIELD(efc_ring, times, 0x14a);
CHECK_FIELD(efc_ring, logs, 0x24a);
CHECK_FIELD(efc_ring, field_34a, 0x396 - 0x4c);
CHECK_FIELD(efc_ring, efc_sum, 0x34e);
CHECK_FIELD(efc_ring, drift, 0x3a2 - 0x4c);
CHECK_FIELD(efc_ring, fit_a, 0x3aa - 0x4c);
CHECK_FIELD(efc_ring, fit_b, 0x3ae - 0x4c);
CHECK_FIELD(efc_ring, fit_c, 0x3b2 - 0x4c);
CHECK_FIELD(pll_state, ti_filtered, 0x3b6);
CHECK_FIELD(pll_state, efc_base, 0x3ba);
CHECK_FIELD(pll_state, loop_time_constant, 0x3be);
CHECK_FIELD(pll_state, holdover_start_time, 0x3c2);
CHECK_FIELD(pll_state, holdover_start_date, 0x3c6);
/* :LED:GPSLock? and :LED:HOLDover? read 0x102840 and 0x102841 */
CHECK_FIELD(pll_state, led_gps_lock, 0x102840 - 0x102476);
CHECK_FIELD(pll_state, led_holdover, 0x102841 - 0x102476);
CHECK_FIELD(pll_state, holdover_duration, 0x102842 - 0x102476);
CHECK_FIELD(pll_state, reference_uptime, 0x3d4);
CHECK_FIELD(pll_state, reference_time, 0x3d8);
CHECK_FIELD(pll_state, efc_control_passes, 0x3dc);
CHECK_FIELD(pll_state, reference_seconds, 0x3e0);
CHECK_FIELD(pll_state, fpga_configuration, 0x102857 - 0x102476);
CHECK_FIELD(pll_state, fpga_loaded, 0x102858 - 0x102476);
CHECK_FIELD(pll_state, loop_running, 0x3e3);
CHECK_FIELD(pll_state, sync_requested, 0x10285a - 0x102476);
CHECK_FIELD(pll_state, field_3e5, 0x3e5);
CHECK_FIELD(pll_state, field_3e6, 0x10285c - 0x102476);

/* Records the printf calls name; in the 58503A 3704 @@Cj's decoder's
 * table (0x52620) lays the receiver ID out as the Z3805A 3543B's: 34
 * bytes, then 9 lines of 27. */
CHECK_SIZE(oncore_receiver_id, 277);
CHECK_FIELD(oncore_receiver_id, software_part_number, 34);
CHECK_FIELD(oncore_receiver_id, software_version, 34 + 27);
CHECK_FIELD(oncore_receiver_id, model_number, 34 + 4 * 27);
CHECK_FIELD(oncore_receiver_id, options, 34 + 8 * 27);
/* The receiver ID's lines (0x5262c): 12 bytes each, the table ending
 * at 0x52698, its tenth; the model number's and the options' texts at
 * 0x1012e7 and 0x101353, in gps.receiver_id from 0x101259. */
CHECK_SIZE(oncore_receiver_id_line, (0x52698 - 0x5262c) / 9);
CHECK_FIELD(oncore_receiver_id_line, text, 2);
CHECK_FIELD(oncore_receiver_id_line, label, 6);
CHECK_FIELD(oncore_receiver_id_line, label_length, 10);
CHECK_FIELD(oncore_receiver_id, model_number, 0x1012e7 - 0x101259);
CHECK_FIELD(oncore_receiver_id, options, 0x101353 - 0x101259);
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
/* log_read: entry n's fields at 0x101d98, 0x101d9c, 0x101da0, ... + n * 0x14.
 */
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
/* The packed enums are bytes, as the fields they type. */
CHECK_SIZE(pll_stage, 1);
CHECK_SIZE(adc_channel, 1);
CHECK_SIZE(oncore_channel_mode, 1);
CHECK_SIZE(oncore_raim_status, 1);

/* nv_settings, each field where its descriptor's value points (below),
 * and the two descriptors' sizes. */
CHECK_SIZE(nv_settings_fields, 0xcb);
/* each is followed by the next descriptor's widths */
CHECK_SIZE(scpi_setting_param, 0x33ca8 - 0x33c8a - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* the group cells, from the descriptors' pointers up to interval_reading */
CHECK_SIZE(scpi_group_cells_fields, 0x101f9c - 0x101f90);
CHECK_FIELD(scpi_group_cells_fields, sample, 0x101f92 - 0x101f90);
CHECK_FIELD(scpi_group_cells_fields, pulse, 0x101f94 - 0x101f90);
CHECK_FIELD(scpi_group_cells_fields, serial_2, 0x101f96 - 0x101f90);
CHECK_FIELD(scpi_group_cells_fields, status, 0x101f98 - 0x101f90);
CHECK_FIELD(scpi_group_cells_fields, rest, 0x101f9a - 0x101f90);
/* 14 action descriptors, each referred to at its own address, from
 * 0x4132a to efc_history_update at 0x4137e */
CHECK_SIZE(scpi_action_param, (0x4137e - 0x4132a) / 14);
CHECK_FIELD(scpi_action_param, action, 0x02);
/* pllc_request_a's frame, link -24, and pll_scpi_receive's; the kernel's
 * message buffers are 0x18 bytes */
CHECK_SIZE(pllc_request, 0x18);
CHECK_FIELD(pllc_request, param, 0x08);
CHECK_FIELD(pllc_request, index, 0x0c);
CHECK_FIELD(pllc_request, source, 0x0d);
CHECK_FIELD(pllc_request, setting, 0x0f);
CHECK_FIELD(pllc_request, values, 0x10);
/* the Oncore message table: in the 58503A 3704, 56 entries from
 * 0x51178 to the ID strings at 0x51e98 (0x50bb2 multiplies by 60);
 * gps_response_timed_out reads +0x1a, gps_response_lookup +0x1c,
 * gps_response_match +0x1e (0x50e3a), gps_response_decode +0x20 and
 * +0x24, and 0x50bc8 the filter at +0x38 */
CHECK_SIZE(oncore_decoder, 8);
CHECK_SIZE(oncore_message_entry, (0x51e98 - 0x51178) / 56);
CHECK_FIELD(oncore_message_entry, filter, 0x38);
CHECK_FIELD(oncore_message_entry, kind, 0x4fed2 - 0x4fece);
CHECK_FIELD(oncore_message_entry, encoders, 0x4fed4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, query, 0x4fee4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, timeout, 0x4fee8 - 0x4fece);
CHECK_FIELD(oncore_message_entry, length, 0x4feea - 0x4fece);
CHECK_FIELD(oncore_message_entry, match_kind, 0x4feec - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders, 0x4feee - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x4fef2 - 0x4fece);
/* the 58503A 3704's nv_settings, at 0x10219c: each field where its
 * descriptor in nv_save's tables (0x32d62, 0x32dbe) points */
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x10219e - 0x10219c);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x1021aa - 0x10219c);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x1021ab - 0x10219c);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x1021ac - 0x10219c);
CHECK_FIELD(nv_settings_fields, field_14, 0x1021b0 - 0x10219c);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x1021b4 - 0x10219c);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x1021b8 - 0x10219c);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x1021c0 - 0x10219c);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x1021c1 - 0x10219c);
CHECK_FIELD(nv_settings_fields, position, 0x1021c2 - 0x10219c);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x1021ce - 0x10219c);
CHECK_FIELD(nv_settings_fields, time_zone, 0x1021d2 - 0x10219c);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x1021da - 0x10219c);
CHECK_FIELD(nv_settings_fields, leap_date, 0x1021de - 0x10219c);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x1021e6 - 0x10219c);
CHECK_FIELD(nv_settings_fields, raim_state, 0x1021e7 - 0x10219c);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x1021ea - 0x10219c);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x1021f2 - 0x10219c);
CHECK_FIELD(nv_settings_fields, status_enable, 0x1021f6 - 0x10219c);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x102212 - 0x10219c);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x102226 - 0x10219c);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x10223a - 0x10219c);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x10223c - 0x10219c);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x10223e - 0x10219c);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x10224e - 0x10219c);
CHECK_FIELD(nv_settings_fields, field_be, 0x10225a - 0x10219c);
CHECK_FIELD(nv_settings_fields, output_state, 0x10225e - 0x10219c);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x102261 - 0x10219c);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x102265 - 0x10219c);
CHECK_FIELD(nv_settings_fields, debug_output, 0x102266 - 0x10219c);

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

/* The 58503A 3704's preserved region at 0x100000, the 0xb2e bytes
 * record_checksum sums (system_init, 0x12cfa): gps's copy at 0x100004,
 * pll's at 0x1005ac, the block from 0x101e78 at 0x100994 and
 * retained_block's 0x19 bytes at 0x100b14. */
CHECK_SIZE(preserved_region_fields, 0x100b2e - 0x100000);
CHECK_FIELD(preserved_region_fields, restore, 0x100002 - 0x100000);
CHECK_FIELD(preserved_region_fields, gps, 0x100004 - 0x100000);
CHECK_FIELD(preserved_region_fields, pll, 0x1005ac - 0x100000);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0x100994 - 0x100000);
/* the 0x180 bytes pllp_task and system_init copy from 0x101e78: the
 * ADC monitors, which monitor_average_add indexes by 0x30 (0x21d40) */
CHECK_SIZE(((preserved_region_fields*)0)->adc_monitors, 0x180);
CHECK_FIELD(preserved_region_fields, values, 0x100b14 - 0x100000);
CHECK_SIZE(((preserved_region_fields*)0)->values, 0x19);

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

/* the 58503A 3704's three status snapshots, 0x1016ae (status_screen_print,
 * 0x4b6ec), 0x10181e and 0x101988, with the two cells at 0x101818
 * between the first two; each field where status_snapshot (0x4ac56)
 * stores it */
CHECK_SIZE(status_snapshot_record, 0x101818 - 0x1016ae);
CHECK_SIZE(status_snapshot_record, 0x101988 - 0x10181e);
CHECK_FIELD(status_snapshot_record, interval, 0x0a);
CHECK_FIELD(status_snapshot_record, holdover_duration, 0x22);
CHECK_FIELD(status_snapshot_record, channel_count, 50);
CHECK_FIELD(status_snapshot_record, satellites, 52);
/* status_screen_mode_lines' five entries, the last a terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x4d050 - 0x4d032);
CHECK_FIELD(status_screen_mode_line, text, 0x02);
CHECK_FIELD(status_snapshot_record, not_tracked, 94);
CHECK_FIELD(status_snapshot_record, tracked, 222);
CHECK_FIELD(status_snapshot_record, status_received, 286);
CHECK_FIELD(status_snapshot_record, not_tracked_count, 287);
CHECK_FIELD(status_snapshot_record, tracked_count, 288);
CHECK_FIELD(status_snapshot_record, pll_field_3d5, 289);
CHECK_FIELD(status_snapshot_record, reference_valid, 290);
CHECK_FIELD(status_snapshot_record, pps_dead, 291);
CHECK_FIELD(status_snapshot_record, status_code, 292);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 294);
CHECK_FIELD(status_snapshot_record, external_valid, 298);
CHECK_FIELD(status_snapshot_record, external_status_code, 300);
CHECK_FIELD(status_snapshot_record, external_elapsed, 302);
CHECK_FIELD(status_snapshot_record, fix_mode, 306);
CHECK_FIELD(status_snapshot_record, receiver_status, 307);
CHECK_FIELD(status_snapshot_record, time, 308);
CHECK_FIELD(status_snapshot_record, date, 312);
CHECK_FIELD(status_snapshot_record, time_zone, 316);
CHECK_FIELD(status_snapshot_record, from_clock, 324);
CHECK_FIELD(status_snapshot_record, utc_time, 325);
CHECK_FIELD(status_snapshot_record, leap_second_pending, 326);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 328);
CHECK_FIELD(status_snapshot_record, mask_angle, 332);
CHECK_FIELD(status_snapshot_record, position_hold, 333);
CHECK_FIELD(status_snapshot_record, douglas_duration_infinite, 334);
CHECK_FIELD(status_snapshot_record, position_source, 336);
CHECK_FIELD(status_snapshot_record, position, 340);
CHECK_FIELD(status_snapshot_record, average_count, 352);
CHECK_FIELD(status_snapshot_record, douglas_duration, 356);
CHECK_FIELD(status_snapshot_record, hardware_condition, 360);

/* the event log's write page at 0x101d4e, its number after it */
CHECK_SIZE(log_page, 0x101d8e - 0x101d4e);

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

/* The ROM's packed script steps, each to the next (0x50a46 to 0x50cc2). */
CHECK_SIZE(gps_script_query_step, 0x50b36 - 0x50b32);
CHECK_SIZE(gps_script_step_1_argument, 0x50a4e - 0x50a46);
CHECK_SIZE(gps_script_step_2_arguments, 0x50a62 - 0x50a56);
CHECK_SIZE(gps_script_step_3_arguments, 0x50a8e - 0x50a7e);
CHECK_SIZE(gps_script_step, 0x50a76 - 0x50a62);
CHECK_SIZE(io_device_stubs, 0x517ce - 0x517aa);
CHECK_SIZE(io_device_stubs, sizeof(io_device_jumps));
CHECK_SIZE(pforth_word_header, 0x1ad1a - 0x1ad0e);
CHECK_FIELD(pforth_word_header, flags, 0x1ad18 - 0x1ad0e);
CHECK_SIZE(diag_word_entry, 0x1c8d2 - 0x1c8ac);
CHECK_SIZE(diag_word_entry[86], 0x1d570 - 0x1c8ac);
CHECK_FIELD(diag_word_entry, argument_count, 0x1e);
CHECK_FIELD(diag_word_entry, result_count, 0x22);

/* The 58503A 3704's health monitor's channel table, eight entries from
 * 0x2238a to the channel names at 0x224da (0x21ca2 multiplies by 0x2a);
 * 0x2220a reads add, check and report at +4, +8 and +0x10, adc_average
 * nominal at +0x1c (0x222e4). */
CHECK_SIZE(hmon_channel_entry, (0x224da - 0x2238a) / 8);
CHECK_FIELD(hmon_channel_entry, check, 0x08);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x22396 - 0x2238a);
CHECK_FIELD(hmon_channel_entry, report, 0x10);
CHECK_FIELD(hmon_channel_entry, minimum, 0x2239e - 0x2238a);
CHECK_FIELD(hmon_channel_entry, maximum, 0x223a2 - 0x2238a);
CHECK_FIELD(hmon_channel_entry, nominal, 0x223a6 - 0x2238a);
CHECK_FIELD(hmon_channel_entry, name, 0x20);

/* 0x29740's frame: link -6, the cursor's text at -6 and severity at -2 */
CHECK_SIZE(alarm_text_cursor, 6);
CHECK_FIELD(alarm_text_cursor, severity, -2 - -6);
/* the tree's root at 0x29b42, its entries' table at 0x29ac2 to the
 * names at 0x29b1c, nine 10-byte entries, the last empty */
CHECK_SIZE(alarm_text_entry, (0x29b1c - 0x29ac2) / 9);
CHECK_FIELD(alarm_text_entry, severity, 8);
CHECK_FIELD(alarm_text_node, entries, 0x29b44 - 0x29b42);

/* port_error_reports at 0x102e48, as scpi_session_init_58503a fills it:
 * the masks at +0, +4, +8 and +0xc, the conditions at +2 to +0xe
 * (0x28dfc to 0x28e24) */
CHECK_SIZE(port_error_report, 4);
CHECK_FIELD(port_error_report, condition, 2);
