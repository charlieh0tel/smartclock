/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in an image it is written as their
 * difference.  This is the Z3816A 4001's copy of the 58503A 3704's, its
 * base (docs/firmware/z3816a-4001.md): each check's addresses are the
 * Z3816A's, derived from the base's (make check-common's
 * layout_addresses.py; docs/firmware/rewrite.md, "Shared C"), and each
 * block's comment is still the 58503A's as it stood when the Z3816A was
 * seeded, its addresses the 58503A's (or, where that comment says so,
 * the Z3805A's), but where a block's comment names the Z3816A.  Compiled, never
 * run, by "make check-types" with an m68k compiler, so that int, long and
 * pointers are the target's 32 bits. */

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

CHECK_SIZE(holdover_steering_state, 0x102728 - 0x10270c);
CHECK_FIELD(holdover_steering_state, aging_limit, 0x102714 - 0x10270c);
CHECK_FIELD(holdover_steering_state, ti_sum, 0x102718 - 0x10270c);
CHECK_FIELD(holdover_steering_state, ti_count, 0x102724 - 0x10270c);
CHECK_SIZE(loop_steering_state, 0x102744 - 0x102728);
CHECK_FIELD(loop_steering_state, seconds, 0x102734 - 0x102728);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x102738 - 0x102728);
CHECK_FIELD(loop_steering_state, filter_weight, 0x102740 - 0x102728);

CHECK_SIZE(serial_ring, 0x103695 - 0x103593);
CHECK_SIZE(oncore_leap_status_fields, 0x101490 - 0x101480);
CHECK_SIZE(satellite_view_state, 0x102206 - 0x101ee2);
CHECK_FIELD(satellite_view_state, hours_seen, 0x10216a - 0x101ee2);
CHECK_FIELD(satellite_view_state, used_range, 0x10216e - 0x101ee2);
CHECK_FIELD(satellite_view_state, tracked_mean, 0x102187 - 0x101ee2);
CHECK_FIELD(satellite_view_state, hour_mean, 0x1021a0 - 0x101ee2);
CHECK_FIELD(satellite_view_state, hour_mean_count, 0x1021a4 - 0x101ee2);
CHECK_FIELD(satellite_view_state, doppler, 0x1021a6 - 0x101ee2);
/* duarta at 0x103696, up to duartb_receive_errors at 0x1036a0 (duartb_isr,
 * 0x2db52): its size a number, as layout_addresses.py's references map
 * the 58503A 3704's duartb_receive_errors to sci.receive_errors
 * (0x1037f7).  Channel B has no ring and no driver here. */
CHECK_SIZE(duarta_driver, 10);
CHECK_FIELD(duarta_driver, overrun, 0x10369c - 0x103696);
CHECK_FIELD(duarta_driver, least_free, 0x10369f - 0x103696);

/* The Z3816A 4001's SCI, the host port (docs/firmware/z3816a-4001.md,
 * "The host port"): its settings at 0x10261a, which sci_settings_preset
 * copies 6 bytes over (0x406aa); its exchanges at 0x1036b0, which
 * sci_lock and sci_unlock take (0x2ebde, 0x2ebec); and its driver at
 * 0x1037ea, each field where sci_puts, sci_gets, sci_send_async,
 * sci_init and the SCI's interrupt address it (0x2e6c4 to 0x2f376), and
 * the installer's at 0x10afea, as its copies address it
 * (installer::0x103038 to installer::0x103dc4). */
CHECK_SIZE(sci_settings_record, 0x102620 - 0x10261a);
CHECK_FIELD(sci_settings_record, baud, 0x10261c - 0x10261a);
CHECK_FIELD(sci_settings_record, echo, 0x10261f - 0x10261a);
CHECK_SIZE(sci_exchange_state, 0x1036e8 - 0x1036b0);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x1036cc - 0x1036b0);
CHECK_SIZE(serial_ring, 0x1037ea - 0x1036e8);
CHECK_SIZE(sci_driver, 0x103808 - 0x1037ea);
CHECK_FIELD(sci_driver, transmit_count, 0x1037ee - 0x1037ea);
CHECK_FIELD(sci_driver, async_next, 0x1037f0 - 0x1037ea);
CHECK_FIELD(sci_driver, async_count, 0x1037f4 - 0x1037ea);
CHECK_FIELD(sci_driver, flow, 0x1037f6 - 0x1037ea);
CHECK_FIELD(sci_driver, receive_errors, 0x1037f7 - 0x1037ea);
CHECK_FIELD(sci_driver, read_errors, 0x1037f8 - 0x1037ea);
CHECK_FIELD(sci_driver, mark_bit, 0x1037fa - 0x1037ea);
CHECK_FIELD(sci_driver, reconfiguring, 0x1037fc - 0x1037ea);
CHECK_FIELD(sci_driver, field_13, 0x1037fd - 0x1037ea);
CHECK_FIELD(sci_driver, async_allowed, 0x1037fe - 0x1037ea);
CHECK_FIELD(sci_driver, field_15, 0x1037ff - 0x1037ea);
CHECK_FIELD(sci_driver, baud_constant, 0x103800 - 0x1037ea);
CHECK_FIELD(sci_driver, echo, 0x103802 - 0x1037ea);
CHECK_FIELD(sci_driver, last_terminator, 0x103806 - 0x1037ea);
CHECK_SIZE(serial_ring, 0x10afea - 0x10aee8);
CHECK_SIZE(installer_sci_driver, 0x10b00a - 0x10afea);
CHECK_FIELD(installer_sci_driver, transmit_count, 0x10afee - 0x10afea);
CHECK_FIELD(installer_sci_driver, async_next, 0x10aff0 - 0x10afea);
CHECK_FIELD(installer_sci_driver, flow, 0x10aff6 - 0x10afea);
CHECK_FIELD(installer_sci_driver, receive_errors, 0x10aff7 - 0x10afea);
CHECK_FIELD(installer_sci_driver, mark_bit, 0x10affa - 0x10afea);
CHECK_FIELD(installer_sci_driver, transmitting, 0x10b000 - 0x10afea);
CHECK_FIELD(installer_sci_driver, baud_constant, 0x10b002 - 0x10afea);
CHECK_FIELD(installer_sci_driver, echo, 0x10b004 - 0x10afea);
CHECK_FIELD(installer_sci_driver, transmit_busy, 0x10b008 - 0x10afea);
CHECK_SIZE(duart_shadow_registers, 0x103593 - 0x103586);
CHECK_FIELD(duart_shadow_registers, imr, 0x10358a - 0x103586);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x10358d - 0x103586);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x103591 - 0x103586);
/* csrb, which duarta_init clears (0x2dde2 on) */
CHECK_FIELD(duart_shadow_registers, csrb, 0x10358f - 0x103586);

CHECK_SIZE(exchange_token, 16);
CHECK_SIZE(qspi_state, 0x103856 - 0x103808);
CHECK_FIELD(qspi_state, waiting, 0x10380c - 0x103808);
CHECK_FIELD(qspi_state, busy, 0x10384c - 0x103808);
CHECK_FIELD(qspi_state, receive_buffer, 0x10384e - 0x103808);
CHECK_FIELD(qspi_state, running, 0x103855 - 0x103808);
CHECK_SIZE(eeprom_lock_token, 0x1023f0 - 0x1023e0);
CHECK_FIELD(eeprom_lock_token, owner, 0x1023e8 - 0x1023e0);
CHECK_FIELD(eeprom_lock_token, depth, 0x1023ec - 0x1023e0);

/* The installer's kernel configuration at 0x10ace6, up to
 * installer_scpi_echo_length, and its I/O table, one device's, before
 * it.  The installer keeps no DUART state: its console is the SCI. */
CHECK_SIZE(psos_configuration, 0x10ad48 - 0x10ace6);
CHECK_SIZE(io_device_jumps, 0x10ace6 - 0x10acc2);

/* The boot code's results, at 0x10ac34, as the Z3815A 4010's: the
 * latch's shadow a word, as the boot code writes it (0x574); the
 * installer's :DIAGnostic:TEST? reads the rest (installer::0x108234). */
CHECK_SIZE(boot_selftest_results, 0x10ac44 - 0x10ac34);
CHECK_FIELD(boot_selftest_results, cpu, 0x10ac36 - 0x10ac34);
CHECK_FIELD(boot_selftest_results, ram, 0x10ac38 - 0x10ac34);
CHECK_FIELD(boot_selftest_results, ram_data_bits, 0x10ac3a - 0x10ac34);
CHECK_FIELD(boot_selftest_results, ram_address_lines, 0x10ac3c - 0x10ac34);
/* a number: layout_addresses.py's references map the 58503A 3704's to
 * an address in the installer's code */
CHECK_FIELD(boot_selftest_results, uart, 0xc);
CHECK_FIELD(boot_selftest_results, latch, 0x10ac42 - 0x10ac34);

/* The kernel's configuration and I/O table, as psos_config_init copies
 * them (0x53972): 0x62 bytes to 0x10294c and 0x6c to 0x1028e0. */
CHECK_SIZE(psos_configuration, 0x102d5e - 0x102cfc);
CHECK_FIELD(psos_configuration, data_end, 0x102d00 - 0x102cfc);
CHECK_FIELD(psos_configuration, region_2_start, 0x102d08 - 0x102cfc);
CHECK_FIELD(psos_configuration, task_records, 0x102d14 - 0x102cfc);
CHECK_FIELD(psos_configuration, exchange_records, 0x102d16 - 0x102cfc);
CHECK_FIELD(psos_configuration, message_buffers, 0x102d18 - 0x102cfc);
CHECK_FIELD(psos_configuration, last_device, 0x102d1e - 0x102cfc);
CHECK_FIELD(psos_configuration, io_table, 0x102d20 - 0x102cfc);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x102d24 - 0x102cfc);
CHECK_FIELD(psos_configuration, root_entry, 0x102d28 - 0x102cfc);
CHECK_FIELD(psos_configuration, root_priority, 0x102d30 - 0x102cfc);
CHECK_FIELD(psos_configuration, start_hook, 0x102d32 - 0x102cfc);
CHECK_FIELD(psos_configuration, delete_hook, 0x102d36 - 0x102cfc);
CHECK_FIELD(psos_configuration, debug_hooks, 0x102d3e - 0x102cfc);
CHECK_FIELD(psos_configuration, task_hooks, 0x102d42 - 0x102cfc);
CHECK_SIZE(io_device_jumps[3], 0x102cfc - 0x102c90);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x1029ba + 6n, its state at 0x1029b8 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x102d6a - 0x102d68);
CHECK_SIZE(task_table_entry[17], 0x102dce - 0x102d68);

CHECK_SIZE(spoo_request, 0x103c7c - 0x103c64);
CHECK_FIELD(spoo_request, function, 0x103c6c - 0x103c64);
CHECK_FIELD(spoo_request, arguments, 0x103c70 - 0x103c64);
CHECK_SIZE(early_status_events, 0x10357e - 0x103576);
CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x102c40 - 0x102c28);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x102c34 - 0x102c28);
CHECK_FIELD(efc_gain_values, step_limit, 0x102c38 - 0x102c28);
CHECK_SIZE(efc_range_state, 0x1023b3 - 0x1023b0);
CHECK_FIELD(efc_range_state, near_end_checks, 0x1023b2 - 0x1023b0);
CHECK_SIZE(gps_command_mailbox, 0x102c56 - 0x102c40);
CHECK_FIELD(gps_command_mailbox, arguments, 0x102c42 - 0x102c40);
CHECK_FIELD(gps_command_mailbox, request, 0x102c52 - 0x102c40);
CHECK_SIZE(adc_monitor, 0x102260 - 0x102230);
CHECK_SIZE(adc_monitor[8], 0x1023b0 - 0x102230);
CHECK_FIELD(adc_monitor, value, 0x102258 - 0x102230);
CHECK_FIELD(adc_monitor, valid, 0x10225c - 0x102230);
CHECK_FIELD(adc_monitor, sample_index, 0x10225d - 0x102230);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x10228e - 0x102260);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x10225f - 0x102230);
CHECK_SIZE(gps_request_code_word, 0x102210 - 0x10220e);
CHECK_SIZE(gps_script_step, 0x102222 - 0x10220e);
CHECK_FIELD(gps_script_step, argument_mode, 0x102210 - 0x10220e);
CHECK_FIELD(gps_script_step, arguments, 0x102212 - 0x10220e);

/* the 58503A 3704's, at 0x100e02, each field where the tests address it:
 * selftest_abort at +0x20 (0x100e22), the GPS's long at +0x14
 * (0x18edc), lamp_test's flag at +0x36 (0x1866e); in the Z3816A 4001 the
 * lamp mask at +0x0c (duart_output_bit_set, 0x2d4f4) */
CHECK_SIZE(selftest_results, 0x48);
CHECK_FIELD(selftest_results, uart, 0x0a);
CHECK_FIELD(selftest_results, lamp_mask, 0x0c);
CHECK_FIELD(selftest_results, summary, 0x0e);
CHECK_FIELD(selftest_results, gps, 0x100e88 - 0x100e74);
CHECK_FIELD(selftest_results, interpolator, 0x18);
CHECK_FIELD(selftest_results, abort, 0x100e94 - 0x100e74);
CHECK_FIELD(selftest_results, running, 0x100e95 - 0x100e74);
CHECK_FIELD(selftest_results, internal_reference, 0x24);
CHECK_FIELD(selftest_results, qspi, 0x26);
CHECK_FIELD(selftest_results, lamp_testing, 0x100eaa - 0x100e74);
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
/* The ring, gps_requests at 0x1016f0, up to command_monitor_result at
 * 0x101794: its size a number, as layout_addresses.py's references map the
 * 58503A 3704's command_monitor_result to status_monitor_result
 * (0x101795), the senders' numbers differing. */
CHECK_SIZE(gps_request_ring, 0xa4);
CHECK_FIELD(gps_request_ring, read, 0x10178c - 0x1016f0);
CHECK_FIELD(gps_request_ring, write, 0x101790 - 0x1016f0);

CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x00);
CHECK_FIELD(gps_command_log_entry, code, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0a);
CHECK_FIELD(gps_command_log_entry, date, 0x0c);
CHECK_FIELD(gps_command_log_entry, time, 0x10);
/* The Z3816A's: command_log_add's offsets (0x51452, 0x5144c). */
CHECK_SIZE(gps_command_log, 0x404);
CHECK_FIELD(gps_command_log, read, 0x3fc);
CHECK_FIELD(gps_command_log, write, 0x400);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x1023cc - 0x1023c0);
CHECK_FIELD(lifetime_state, date, 0x1023c4 - 0x1023c0);
CHECK_FIELD(lifetime_state, count, 0x1023c8 - 0x1023c0);

CHECK_SIZE(qspi_buffers, 8);
CHECK_SIZE(scpi_step, 8);
CHECK_SIZE(parameter_record, 16);
CHECK_FIELD(parameter_record, sci_baud, 9);
CHECK_SIZE(identification_record, 48);
CHECK_FIELD(identification_record, suffix, 0x26);
CHECK_SIZE(lifetime_record, 32);
CHECK_FIELD(lifetime_record, checksum, 0);
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
CHECK_SIZE(scpi_multiplier[13], 0x37000 - 0x36fcc);
CHECK_SIZE(scpi_unit[2], 0x5ae4e - 0x5ae42);
CHECK_FIELD(scpi_keyword, text, 2);
CHECK_SIZE(scpi_keyword, 0x14);
CHECK_FIELD(scpi_unit, exponent, 4);
CHECK_SIZE(scpi_error_message[101], 0x3804e - 0x37df0);
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
CHECK_SIZE(oncore_time_fields, 0x100ec0 - 0x100ebd);
CHECK_SIZE(oncore_gmt_offset_fields, 0x100ec3 - 0x100ec0);
CHECK_FIELD(oncore_date_fields, day, 0x100ec5 - 0x100ec4);
CHECK_FIELD(oncore_date_fields, year, 0x100ec6 - 0x100ec4);
CHECK_SIZE(oncore_position_fields, 0x100ed8 - 0x100ec8);
CHECK_FIELD(oncore_position_fields, longitude, 0x100ecc - 0x100ec8);
CHECK_FIELD(oncore_position_fields, height_gps, 0x100ed0 - 0x100ec8);
CHECK_FIELD(oncore_position_fields, height_msl, 0x100ed4 - 0x100ec8);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x100f68 - 0x100f64);
CHECK_FIELD(oncore_hold_position_fields, height, 0x100f6c - 0x100f64);

/* @@Ek: in the 58503A 3704, 64 bytes of data in a 71-byte message, which
 * its decoder's table copies whole (0x525ed); @@Bk's (0x525e8) puts its
 * 38 bytes to the URAs first, zeroes the last two URAs and copies 14
 * more. */
CHECK_SIZE(oncore_extension_fields, 71 - 7);
CHECK_FIELD(oncore_extension_fields, ura, 0x20);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x28);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x3e);

CHECK_SIZE(aging_curve_state, 0x1026cc - 0x1026a2);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x1026a3 - 0x1026a2);
CHECK_FIELD(aging_curve_state, oldest_time, 0x1026a4 - 0x1026a2);
CHECK_FIELD(aging_curve_state, newest_time, 0x1026a8 - 0x1026a2);
CHECK_FIELD(aging_curve_state, a, 0x1026b4 - 0x1026a2);
CHECK_FIELD(aging_curve_state, b, 0x1026b8 - 0x1026a2);
CHECK_FIELD(aging_curve_state, c, 0x1026bc - 0x1026a2);
CHECK_FIELD(aging_curve_state, rms, 0x1026c4 - 0x1026a2);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x1026c8 - 0x1026a2);

/* The Z3816A 4001's GPS task state at 0x100ebc, memset(&gps, 0, 0x61e)
 * by gps_init (0x50b1e): each message's record where the message table
 * (oncore_messages, 0x56732, 56 entries of 60 bytes) points its
 * decoders, its own state where its code addresses it
 * (docs/firmware/z3816a-4001.md, "Types"). */
CHECK_SIZE(gps_state, 0x1014da - 0x100ebc);
CHECK_FIELD(gps_state, time, 0x100ebd - 0x100ebc);
CHECK_FIELD(gps_state, gmt_offset, 0x100ec0 - 0x100ebc);
CHECK_FIELD(gps_state, date, 0x100ec4 - 0x100ebc);
CHECK_FIELD(gps_state, position, 0x100ec8 - 0x100ebc);
CHECK_FIELD(gps_state, mask_angle, 0x100ed8 - 0x100ebc);
CHECK_FIELD(gps_state, satellite_select, 0x100ed9 - 0x100ebc);
CHECK_FIELD(gps_state, manual_satellites, 0x100eda - 0x100ebc);
CHECK_FIELD(gps_state, xdop_type, 0x100ee2 - 0x100ebc);
CHECK_FIELD(gps_state, xdop_hysteresis, 0x100ee4 - 0x100ebc);
CHECK_FIELD(gps_state, xdop_threshold, 0x100ee6 - 0x100ebc);
CHECK_FIELD(gps_state, hdop_threshold, 0x100ee8 - 0x100ebc);
CHECK_FIELD(gps_state, ignored_satellites, 0x100eea - 0x100ebc);
CHECK_FIELD(gps_state, almanac_update, 0x100f4c - 0x100ebc);
CHECK_FIELD(gps_state, field_092, 0x100f4e - 0x100ebc);
CHECK_FIELD(gps_state, ionospheric_correction, 0x100f62 - 0x100ebc);
CHECK_FIELD(gps_state, fix_algorithm, 0x100f63 - 0x100ebc);
CHECK_FIELD(gps_state, hold_position, 0x100f64 - 0x100ebc);
CHECK_FIELD(gps_state, hold_height_type, 0x100f70 - 0x100ebc);
CHECK_FIELD(gps_state, position_hold, 0x100f72 - 0x100ebc);
CHECK_FIELD(gps_state, altitude_hold_height, 0x100f74 - 0x100ebc);
CHECK_FIELD(gps_state, altitude_hold_height_type, 0x100f78 - 0x100ebc);
CHECK_FIELD(gps_state, altitude_hold, 0x100f79 - 0x100ebc);
CHECK_FIELD(gps_state, utc_time, 0x100f7a - 0x100ebc);
CHECK_FIELD(gps_state, epoch_offset, 0x100f7c - 0x100ebc);
CHECK_FIELD(gps_state, pps_offset, 0x100f7e - 0x100ebc);
CHECK_FIELD(gps_state, pps_cable_delay, 0x100f82 - 0x100ebc);
CHECK_FIELD(gps_state, pps_cable_delay_given, 0x100f86 - 0x100ebc);
CHECK_FIELD(gps_state, application_type, 0x100f8b - 0x100ebc);
CHECK_FIELD(gps_state, position_fix_threshold, 0x100f8c - 0x100ebc);
CHECK_FIELD(gps_state, differential_output_threshold, 0x100f8d - 0x100ebc);
CHECK_FIELD(gps_state, status_received, 0x100f8e - 0x100ebc);
CHECK_FIELD(gps_state, status_rate, 0x100f8f - 0x100ebc);
CHECK_FIELD(gps_state, status, 0x100f90 - 0x100ebc);
CHECK_FIELD(gps_state, visibility_received, 0x100fd8 - 0x100ebc);
CHECK_FIELD(gps_state, visibility, 0x100fda - 0x100ebc);
CHECK_FIELD(gps_state, xdop_table, 0x101059 - 0x100ebc);
CHECK_FIELD(gps_state, almanac, 0x1010a6 - 0x100ebc);
CHECK_FIELD(gps_state, corrections, 0x101176 - 0x100ebc);
CHECK_FIELD(gps_state, correction_average, 0x1011b6 - 0x100ebc);
CHECK_FIELD(gps_state, correction_deviation, 0x1011be - 0x100ebc);
CHECK_FIELD(gps_state, extension, 0x1011ca - 0x100ebc);
CHECK_FIELD(gps_state, raim, 0x10120a - 0x100ebc);
CHECK_FIELD(oncore_raim_fields, pps_mode, 0x101210 - 0x10120a);
CHECK_FIELD(oncore_raim_fields, channels, 0x101226 - 0x10120a);
CHECK_FIELD(gps_state, raim_received, 0x101256 - 0x100ebc);
CHECK_FIELD(gps_state, raim_alarm_date, 0x101258 - 0x100ebc);
CHECK_FIELD(gps_state, raim_alarm_sigma, 0x101260 - 0x100ebc);
CHECK_FIELD(gps_state, raim_alarm_channels, 0x101262 - 0x100ebc);
CHECK_FIELD(gps_state, raim_sigma, 0x101292 - 0x100ebc);
CHECK_FIELD(gps_state, self_test_response, 0x101294 - 0x100ebc);
CHECK_FIELD(gps_state, power_on_failure, 0x101296 - 0x100ebc);
CHECK_FIELD(gps_state, self_test_mask, 0x101298 - 0x100ebc);
CHECK_FIELD(gps_state, antenna_sense, 0x10129a - 0x100ebc);
CHECK_FIELD(gps_state, self_test, 0x10129c - 0x100ebc);
CHECK_FIELD(gps_state, fix_mode, 0x10130a - 0x100ebc);
CHECK_FIELD(gps_state, receiver_id_received, 0x10130c - 0x100ebc);
CHECK_FIELD(gps_state, receiver_id, 0x10130d - 0x100ebc);
CHECK_FIELD(gps_state, dcxo_drift_reference, 0x101422 - 0x100ebc);
CHECK_FIELD(gps_state, dcxo_setting, 0x101424 - 0x100ebc);
CHECK_FIELD(gps_state, dcxo_hz_per_step, 0x101426 - 0x100ebc);
CHECK_FIELD(gps_state, field_56c, 0x101428 - 0x100ebc);
CHECK_FIELD(gps_state, average_count, 0x10142a - 0x100ebc);
CHECK_FIELD(gps_state, average_latitude, 0x10142e - 0x100ebc);
CHECK_FIELD(gps_state, average_longitude, 0x101436 - 0x100ebc);
CHECK_FIELD(gps_state, average_height_gps, 0x10143e - 0x100ebc);
CHECK_FIELD(gps_state, average_height_msl, 0x101446 - 0x100ebc);
CHECK_FIELD(gps_state, references, 0x10144e - 0x100ebc);
CHECK_FIELD(gps_state, fix_seen, 0x10147a - 0x100ebc);
CHECK_FIELD(gps_state, surveying, 0x10147b - 0x100ebc);
CHECK_FIELD(gps_state, field_5c0, 0x10147c - 0x100ebc);
CHECK_FIELD(gps_state, leap_second_reported, 0x10147e - 0x100ebc);
CHECK_FIELD(gps_state, utc_offset, 0x10147f - 0x100ebc);
CHECK_FIELD(gps_state, leap_status, 0x101480 - 0x100ebc);
CHECK_FIELD(gps_state, leap_second_pending, 0x101490 - 0x100ebc);
CHECK_FIELD(gps_state, time_status, 0x101491 - 0x100ebc);
CHECK_FIELD(gps_state, utc_offset_received, 0x101492 - 0x100ebc);
CHECK_FIELD(gps_state, raim_assessment, 0x101494 - 0x100ebc);
CHECK_FIELD(gps_state, pps_check_disabled, 0x101495 - 0x100ebc);
CHECK_FIELD(gps_state, receiver_health_failed, 0x101496 - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x101498 - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_state, 0x101499 - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_query, 0x10149a - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_time, 0x10149e - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_error, 0x1014a2 - 0x100ebc);
CHECK_FIELD(gps_state, command_monitor_failed, 0x1014a3 - 0x100ebc);
CHECK_FIELD(gps_state, status_monitor_enabled, 0x1014a4 - 0x100ebc);
CHECK_FIELD(gps_state, status_monitor_state, 0x1014a5 - 0x100ebc);
CHECK_FIELD(gps_state, status_message_time, 0x1014a6 - 0x100ebc);
CHECK_FIELD(gps_state, raim_message_time, 0x1014aa - 0x100ebc);
CHECK_FIELD(gps_state, status_monitor_time, 0x1014ae - 0x100ebc);
CHECK_FIELD(gps_state, status_last_time, 0x1014b2 - 0x100ebc);
CHECK_FIELD(gps_state, status_repeat_count, 0x1014b6 - 0x100ebc);
CHECK_FIELD(gps_state, status_monitor_failed, 0x1014b7 - 0x100ebc);
CHECK_FIELD(gps_state, poll_monitor_enabled, 0x1014b8 - 0x100ebc);
CHECK_FIELD(gps_state, poll_monitor_state, 0x1014b9 - 0x100ebc);
CHECK_FIELD(gps_state, poll_monitor_place, 0x1014ba - 0x100ebc);
CHECK_FIELD(gps_state, poll_monitor_time, 0x1014be - 0x100ebc);
CHECK_FIELD(gps_state, channel_count, 0x1014c2 - 0x100ebc);
CHECK_FIELD(gps_state, engine, 0x1014c4 - 0x100ebc);
CHECK_FIELD(gps_state, tracking_log, 0x1014c6 - 0x100ebc);
CHECK_FIELD(gps_state, tracking_log_active, 0x1014c7 - 0x100ebc);
CHECK_FIELD(gps_state, track_words, 0x1014c8 - 0x100ebc);
CHECK_FIELD(gps_state, channel_word, 0x1014d4 - 0x100ebc);
CHECK_FIELD(gps_state, channel_word_lowest, 0x1014d8 - 0x100ebc);
/* the decoders' tables put the receiver status at +0x46 (0x525cc,
 * 0x525d6), which oncore_position_message reads (0x50642) */
CHECK_SIZE(oncore_status_fields, 0x47);
/* @@Bb's decoder, 0x50716, sets the flag and clamps the count after it */
CHECK_SIZE(oncore_visibility_fields, 0x7a);
CHECK_SIZE(oncore_xdop_table_fields, 75);
CHECK_SIZE(oncore_almanac_status_fields, 16);
CHECK_SIZE(oncore_corrections_fields, 64);
CHECK_SIZE(oncore_raim_fields, 0x4c);
/* the two reference records, 0x4b82a's (0x101394, 0x1013aa) */
CHECK_SIZE(gps_reference_record, 0x101464 - 0x10144e);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* 0x4b858 stores the GPS's at 0x1013a6 and 0x1013a8 */
CHECK_FIELD(gps_reference_record, hysteresis, 0x101460 - 0x10144e);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x101462 - 0x10144e);

/* The Z3816A 4001's loop state at 0x10282c, memset(&pll, 0, 0x3e0) by
 * pll_init (0x4af24), each field where its code addresses it: the
 * Z3805A 3543B's to the EFC history, the hold causes a byte at +0x11
 * (pll_init's clr.b, 0x4af68), the history the 58503A 3704's at +0x4a
 * (0x498e8), the fields after it where pll_init and the routines that
 * read them address them. */
CHECK_SIZE(pll_state, 0x3e0);
CHECK_FIELD(pll_state, previous_stage, 0x01);
CHECK_FIELD(pll_state, powerup_countdown, 0x02);
CHECK_FIELD(pll_state, powerup_state, 0x03);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x04);
CHECK_FIELD(pll_state, reference_check_state, 0x06);
CHECK_FIELD(pll_state, startup_state, 0x0c);
CHECK_FIELD(pll_state, normal_state, 0x0e);
CHECK_FIELD(pll_state, holdover_state, 0x10283c - 0x10282c);
CHECK_FIELD(pll_state, hold_causes, 0x10283d - 0x10282c);
CHECK_SIZE(pll_hold_cause, 1);
CHECK_FIELD(pll_state, manual, 0x10283e - 0x10282c);
CHECK_FIELD(pll_state, ti_limit_override, 0x10283f - 0x10282c);
CHECK_FIELD(pll_state, recovery_countdown, 0x14);
CHECK_FIELD(pll_state, recovery_state, 0x15);
CHECK_FIELD(pll_state, coarse_state, 0x16);
CHECK_FIELD(pll_state, fine_state, 0x102843 - 0x10282c);
CHECK_FIELD(pll_state, frequency_adjusted, 0x18);
CHECK_FIELD(pll_state, fine_index, 0x102845 - 0x10282c);
CHECK_FIELD(pll_state, fine_retries, 0x102846 - 0x10282c);
CHECK_FIELD(pll_state, fine_d, 0x102848 - 0x10282c);
CHECK_FIELD(pll_state, aging_state, 0x20);
CHECK_FIELD(pll_state, gps_diag_state, 0x24);
CHECK_FIELD(pll_state, gps_init_state, 0x26);
CHECK_FIELD(pll_state, leap_check_timer, 0x28);
CHECK_FIELD(pll_state, leap_check_requested, 0x2a);
CHECK_FIELD(pll_state, leap_check_state, 0x2b);
CHECK_FIELD(pll_state, leap_ack_state, 0x2c);
CHECK_FIELD(pll_state, leap_ack_again, 0x30);
CHECK_FIELD(pll_state, efc, 0x10285e - 0x10282c);
CHECK_FIELD(pll_state, tfom, 0x102862 - 0x10282c);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x3a);
CHECK_FIELD(pll_state, frequency_tolerance, 0x102872 - 0x10282c);
CHECK_FIELD(pll_state, efc_history, 0x102876 - 0x10282c);
/* the history 0x36a bytes, the 58503A 3704's: efc_history_update's
 * long at +0x34a (0x41518), efc_sum at +0x34e and the rest 4 bytes on;
 * this image's drift and fit at pll + 0x3a0 to 0x3b0 */
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
CHECK_FIELD(efc_ring, drift, 0x102bcc - 0x102876);
CHECK_FIELD(efc_ring, fit_a, 0x102bd4 - 0x102876);
CHECK_FIELD(efc_ring, fit_b, 0x102bd8 - 0x102876);
CHECK_FIELD(efc_ring, fit_c, 0x102bdc - 0x102876);
CHECK_FIELD(pll_state, ti_filtered, 0x102be0 - 0x10282c);
CHECK_FIELD(pll_state, efc_base, 0x102be4 - 0x10282c);
CHECK_FIELD(pll_state, loop_time_constant, 0x3bc);
CHECK_FIELD(pll_state, holdover_start_time, 0x3c0);
CHECK_FIELD(pll_state, holdover_start_date, 0x3c4);
CHECK_FIELD(pll_state, led_gps_lock, 0x102bf4 - 0x10282c);
CHECK_FIELD(pll_state, led_holdover, 0x102bf5 - 0x10282c);
CHECK_FIELD(pll_state, holdover_duration, 0x102bf6 - 0x10282c);
CHECK_FIELD(pll_state, reference_uptime, 0x102bfe - 0x10282c);
CHECK_FIELD(pll_state, reference_time, 0x102c02 - 0x10282c);
CHECK_FIELD(pll_state, reference_seconds, 0x102c06 - 0x10282c);
CHECK_FIELD(pll_state, fpga_configuration, 0x3db);
CHECK_FIELD(pll_state, fpga_loaded, 0x102c08 - 0x10282c);
CHECK_FIELD(pll_state, loop_running, 0x102c09 - 0x10282c);
CHECK_FIELD(pll_state, sync_requested, 0x3de);
CHECK_FIELD(pll_state, field_3df, 0x3df);

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
CHECK_SIZE(oncore_receiver_id_line, (0x57edc - 0x57e70) / 9);
CHECK_FIELD(oncore_receiver_id_line, text, 2);
CHECK_FIELD(oncore_receiver_id_line, label, 6);
CHECK_FIELD(oncore_receiver_id_line, label_length, 10);
CHECK_FIELD(oncore_receiver_id, model_number, 0x101353 - 0x1012c5);
CHECK_FIELD(oncore_receiver_id, options, 0x1013bf - 0x1012c5);
/* gps_counts at 0x101524, 0x38 bytes (clear_gps_debug, 0x4d40a): its
 * fields as the 58503A's code reaches them. */
CHECK_FIELD(gps_statistics, responses_read, 0x22);
CHECK_FIELD(gps_statistics, raim_detected, 0x101630 - 0x101606);
CHECK_FIELD(gps_statistics, raim_alarms, 0x101632 - 0x101606);
CHECK_FIELD(gps_statistics, frame_errors, 0x10163e - 0x101606);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x101642 - 0x101606);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x101644 - 0x101606);
CHECK_FIELD(gps_statistics, unknown_response, 0x10163c - 0x101606);
CHECK_SIZE(gps_statistics, 0x40);
CHECK_SIZE(exception_record, 0x100bfe - 0x100bec);
CHECK_FIELD(exception_record, name, 0x100bed - 0x100bec);
CHECK_FIELD(exception_record, sr, 0x100bf2 - 0x100bec);
CHECK_FIELD(exception_record, pc, 0x100bf4 - 0x100bec);
CHECK_FIELD(exception_record, vector, 0x100bf8 - 0x100bec);
CHECK_FIELD(exception_record, address, 0x100bfa - 0x100bec);
/* The pool: five entries from 0x1038fa to 0x10397c. */
CHECK_SIZE(klok_entry, (0x103d0e - 0x103c8c) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read: entry n's fields at 0x1020a4, 0x1020a8, 0x1020ac, ... + n * 0x14.
 */
CHECK_SIZE(log_page, 0x102498 - 0x102458);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x10245c - 0x102458);
CHECK_FIELD(log_entry, format, 0x102464 - 0x10245c);
CHECK_FIELD(log_entry, argument_1, 0x102468 - 0x10245c);
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
CHECK_SIZE(gps_reference_validity, 1);

/* nv_settings, each field where its descriptor's value points (below),
 * and the two descriptors' sizes. */
CHECK_SIZE(nv_settings_fields, 0xcb);
/* in the 58503A each follows the last, with no widths between */
CHECK_SIZE(scpi_setting_param, 0x43168 - 0x4314c);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* the group cells, from the descriptors' pointers up to interval_reading */
CHECK_SIZE(scpi_group_cells_fields, 0x102666 - 0x10265a);
CHECK_FIELD(scpi_group_cells_fields, gps_initial, 0x10265c - 0x10265a);
CHECK_FIELD(scpi_group_cells_fields, pulse, 0x10265e - 0x10265a);
CHECK_FIELD(scpi_group_cells_fields, serial_2, 0x102660 - 0x10265a);
CHECK_FIELD(scpi_group_cells_fields, status, 0x102662 - 0x10265a);
CHECK_FIELD(scpi_group_cells_fields, rest, 0x102664 - 0x10265a);
/* 13 action descriptors, each referred to at its own address, from
 * 0x413ca to efc_history_update at 0x41418 */
CHECK_SIZE(scpi_action_param, (0x44cbe - 0x44c70) / 13);
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
CHECK_SIZE(oncore_message_entry, (0x57452 - 0x56732) / 56);
CHECK_FIELD(oncore_message_entry, filter, 0x38);
CHECK_FIELD(oncore_message_entry, kind, 0x56736 - 0x56732);
CHECK_FIELD(oncore_message_entry, encoders, 0x56738 - 0x56732);
CHECK_FIELD(oncore_message_entry, query, 0x56748 - 0x56732);
CHECK_FIELD(oncore_message_entry, timeout, 0x5674c - 0x56732);
CHECK_FIELD(oncore_message_entry, length, 0x5674e - 0x56732);
CHECK_FIELD(oncore_message_entry, match_kind, 0x56750 - 0x56732);
CHECK_FIELD(oncore_message_entry, decoders, 0x56752 - 0x56732);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x56756 - 0x56732);
/* the 58503A 3704's nv_settings, at 0x10219c: each field where its
 * descriptor in nv_save's tables (0x32d62, 0x32dbe) points */
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x102550 - 0x10254e);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x10255c - 0x10254e);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x10255d - 0x10254e);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x10255e - 0x10254e);
CHECK_FIELD(nv_settings_fields, field_14, 0x102562 - 0x10254e);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x102566 - 0x10254e);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x10256a - 0x10254e);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x102572 - 0x10254e);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x102573 - 0x10254e);
CHECK_FIELD(nv_settings_fields, position, 0x102574 - 0x10254e);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x102580 - 0x10254e);
CHECK_FIELD(nv_settings_fields, time_zone, 0x102584 - 0x10254e);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x10258c - 0x10254e);
CHECK_FIELD(nv_settings_fields, leap_date, 0x102590 - 0x10254e);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x102598 - 0x10254e);
CHECK_FIELD(nv_settings_fields, raim_state, 0x102599 - 0x10254e);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x10259c - 0x10254e);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x1025a4 - 0x10254e);
CHECK_FIELD(nv_settings_fields, status_enable, 0x1025a8 - 0x10254e);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x1025c4 - 0x10254e);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x1025d8 - 0x10254e);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x1025ec - 0x10254e);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x1025ee - 0x10254e);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x1025f0 - 0x10254e);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x102600 - 0x10254e);
CHECK_FIELD(nv_settings_fields, field_be, 0x10260c - 0x10254e);
CHECK_FIELD(nv_settings_fields, output_state, 0x102610 - 0x10254e);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x102613 - 0x10254e);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x102617 - 0x10254e);
CHECK_FIELD(nv_settings_fields, debug_output, 0x102618 - 0x10254e);

/* The time-interval counter's two records: the reading at 0x1022ae, its
 * flag at 0x1022b6 and armed flag at 0x1022ba; the configuration at
 * 0x1022c0, ending where interpolator_calibration_state begins. */
CHECK_SIZE(interval_reading_fields, 0x102673 - 0x102666);
CHECK_FIELD(interval_reading_fields, valid, 0x10266e - 0x102666);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x102672 - 0x102666);
CHECK_SIZE(counter_configuration_fields, 0x102698 - 0x102678);
CHECK_FIELD(counter_configuration_fields, calibration, 4);
CHECK_FIELD(counter_configuration_fields, registers_mode, 0xc);
CHECK_FIELD(counter_configuration_fields, calibrate_state, 0xf);
CHECK_FIELD(counter_configuration_fields, sync_mode, 0x102688 - 0x102678);
CHECK_FIELD(counter_configuration_fields, sync_kind, 0x10268a - 0x102678);
CHECK_FIELD(counter_configuration_fields, sync_count, 0x10268c - 0x102678);
CHECK_FIELD(counter_configuration_fields, interpolator_offset, 0x18);
CHECK_FIELD(counter_configuration_fields, interpolator_scale, 0x1c);

/* The Z3816A 4001's preserved region at 0x100000, the 0xb9c bytes
 * record_checksum sums (system_init, 0x22d32): gps's copy at 0x100004,
 * pll's at 0x100622, adc_monitors' at 0x100a02 and retained_block's 0x19
 * bytes at 0x100b82 (system_init's copies back, 0x22d68 to 0x22dac). */
CHECK_SIZE(preserved_region_fields, 0x100b9c - 0x100000);
CHECK_FIELD(preserved_region_fields, restore, 0x100002 - 0x100000);
CHECK_FIELD(preserved_region_fields, gps, 0x100004 - 0x100000);
CHECK_FIELD(preserved_region_fields, pll, 0x100622 - 0x100000);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0x100a02 - 0x100000);
/* the 0x180 bytes pllp_task and system_init copy: the ADC monitors,
 * 0x60 longs (0x22da8) */
CHECK_SIZE(((preserved_region_fields*)0)->adc_monitors, 0x180);
CHECK_FIELD(preserved_region_fields, values, 0x100b82 - 0x100000);
CHECK_SIZE(((preserved_region_fields*)0)->values, 0x19);

/* The clock record at 0x100b98: the routines' offsets, and the
 * addresses get_date_time, get_gps_time and clock_init use. */
CHECK_SIZE(clock_record, 0x100c39 - 0x100c06);
CHECK_FIELD(clock_record, uptime, 0x100c08 - 0x100c06);
CHECK_FIELD(clock_record, time, 0x100c0c - 0x100c06);
CHECK_FIELD(clock_record, date, 0x100c10 - 0x100c06);
CHECK_FIELD(clock_record, month_days, 0x100c14 - 0x100c06);
CHECK_FIELD(clock_record, gps_time, 0x100c20 - 0x100c06);
CHECK_FIELD(clock_record, gps_date, 0x100c24 - 0x100c06);
CHECK_FIELD(clock_record, gps_month_days, 0x100c28 - 0x100c06);
CHECK_FIELD(clock_record, minute_length, 0x100c34 - 0x100c06);
CHECK_FIELD(clock_record, set, 0x100c35 - 0x100c06);
CHECK_FIELD(clock_record, gps_utc_offset, 0x100c36 - 0x100c06);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x100c38 - 0x100c06);

/* pps_message at 0x100d8c, pps_exchange after the cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x100e14 - 0x100dfc);
CHECK_FIELD(pps_exchange_message, gps_time, 0x100e04 - 0x100dfc);
CHECK_FIELD(pps_exchange_message, gps_date, 0x100e08 - 0x100dfc);

CHECK_SIZE(gps_process_state, 1);

/* status_screen_mode_lines' five entries from 0x5240e, the last a
 * terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x5242c - 0x5240e);
CHECK_FIELD(status_screen_mode_line, text, 0x02);

/* the Z3816A 4001's two status snapshots, 0x101798 and 0x101936, which
 * status_screen_print chooses between (0x4f718, 0x4f720); each field
 * where status_snapshot (0x4ebbc) stores it */
CHECK_SIZE(status_snapshot_record, 0x101936 - 0x101798);
CHECK_FIELD(status_snapshot_record, tfom, 4);
CHECK_FIELD(status_snapshot_record, ffom, 6);
CHECK_FIELD(status_snapshot_record, ffom_valid, 7);
CHECK_FIELD(status_snapshot_record, interval, 8);
CHECK_FIELD(status_snapshot_record, interval_mean, 20);
CHECK_FIELD(status_snapshot_record, holdover_waiting_reason, 26);
CHECK_FIELD(status_snapshot_record, holdover_limit_state, 27);
CHECK_FIELD(status_snapshot_record, holdover_limit_threshold, 28);
CHECK_FIELD(status_snapshot_record, holdover_duration, 32);
CHECK_FIELD(status_snapshot_record, kenneth_present, 36);
CHECK_FIELD(status_snapshot_record, kenneth_present_valid, 40);
CHECK_FIELD(status_snapshot_record, kenneth_present_resolution, 41);
CHECK_FIELD(status_snapshot_record, kenneth_predicted, 42);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_valid, 46);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_resolution, 47);
CHECK_FIELD(status_snapshot_record, channel_count, 48);
CHECK_FIELD(status_snapshot_record, satellites, 50);
CHECK_FIELD(status_snapshot_record, not_tracked, 92);
CHECK_FIELD(status_snapshot_record, tracked, 252);
CHECK_FIELD(status_snapshot_record, status_received, 332);
CHECK_FIELD(status_snapshot_record, not_tracked_count, 333);
CHECK_FIELD(status_snapshot_record, tracked_count, 334);
CHECK_FIELD(status_snapshot_record, pll_field_3df, 335);
CHECK_FIELD(status_snapshot_record, reference_valid, 336);
CHECK_FIELD(status_snapshot_record, pps_dead, 337);
CHECK_FIELD(status_snapshot_record, status_code, 338);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 340);
CHECK_FIELD(status_snapshot_record, external_valid, 344);
CHECK_FIELD(status_snapshot_record, external_pps_dead, 345);
CHECK_FIELD(status_snapshot_record, external_status_code, 346);
CHECK_FIELD(status_snapshot_record, external_elapsed, 348);
CHECK_FIELD(status_snapshot_record, gps_engine, 352);
CHECK_FIELD(status_snapshot_record, fix_mode, 354);
CHECK_FIELD(status_snapshot_record, receiver_status, 355);
CHECK_FIELD(status_snapshot_record, time, 356);
CHECK_FIELD(status_snapshot_record, date, 360);
CHECK_FIELD(status_snapshot_record, time_zone, 364);
CHECK_FIELD(status_snapshot_record, from_clock, 372);
CHECK_FIELD(status_snapshot_record, utc_time, 373);
CHECK_FIELD(status_snapshot_record, time_status, 374);
CHECK_FIELD(status_snapshot_record, leap_second_pending, 375);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 376);
CHECK_FIELD(status_snapshot_record, gps_antenna_sense, 380);
CHECK_FIELD(status_snapshot_record, mask_angle, 381);
CHECK_FIELD(status_snapshot_record, position_hold, 382);
CHECK_FIELD(status_snapshot_record, douglas_duration_infinite, 383);
CHECK_FIELD(status_snapshot_record, gps_field_5bf, 384);
CHECK_FIELD(status_snapshot_record, position_source, 386);
CHECK_FIELD(status_snapshot_record, position, 390);
CHECK_FIELD(status_snapshot_record, height_type, 402);
CHECK_FIELD(status_snapshot_record, average_count, 404);
CHECK_FIELD(status_snapshot_record, douglas_duration, 408);
CHECK_FIELD(status_snapshot_record, hardware_condition, 412);

/* the event log's write page at 0x10205a, its number after it */
CHECK_SIZE(log_page, 0x102452 - 0x102412);

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
/* the Z3816A 4001's list entries, 10 bytes, the cell's Doppler at +8
 * (satellite_status_list, 0x4dc92; satellite_elevation_azimuth,
 * 0x4d922) */
CHECK_SIZE(satellite_screen_cell, 6);
CHECK_FIELD(satellite_screen_cell, doppler, 4);
CHECK_SIZE(satellite_list_entry, 10);
CHECK_FIELD(satellite_list_entry, level, 2);
CHECK_FIELD(satellite_list_entry, cell, 4);

/* The ROM's packed script steps, each to the next, where image_diff finds
 * the Z3805A's: gps_step_Ab_set to gps_step_Ah_set_2.  The names file
 * names no query step, whose size is a number, nor a step of one or two
 * arguments. */
CHECK_SIZE(gps_script_query_step, 4);
CHECK_SIZE(gps_script_step_3_arguments, 0x57732 - 0x57722);
CHECK_SIZE(io_device_stubs, sizeof(io_device_jumps));
CHECK_SIZE(pforth_word_header, 0x2aef6 - 0x2aeea);
CHECK_FIELD(pforth_word_header, flags, 0x2aef4 - 0x2aeea);
/* the diagnostic words' table, diag_words, 80 entries from 0x2c86c */
CHECK_SIZE(diag_word_entry, 0x2c892 - 0x2c86c);
CHECK_SIZE(diag_word_entry[80], 0x2d44c - 0x2c86c);
CHECK_FIELD(diag_word_entry, argument_count, 0x1e);
CHECK_FIELD(diag_word_entry, result_count, 0x22);

/* The 58503A 3704's health monitor's channel table, eight entries from
 * 0x2238a to the channel names at 0x224da (0x21ca2 multiplies by 0x2a);
 * 0x2220a reads add, check and report at +4, +8 and +0x10, adc_average
 * nominal at +0x1c (0x222e4). */
CHECK_SIZE(hmon_channel_entry, (0x326e6 - 0x32596) / 8);
CHECK_FIELD(hmon_channel_entry, check, 0x08);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x325a2 - 0x32596);
CHECK_FIELD(hmon_channel_entry, report, 0x10);
CHECK_FIELD(hmon_channel_entry, minimum, 0x325aa - 0x32596);
CHECK_FIELD(hmon_channel_entry, maximum, 0x325ae - 0x32596);
CHECK_FIELD(hmon_channel_entry, nominal, 0x325b2 - 0x32596);
CHECK_FIELD(hmon_channel_entry, name, 0x20);

/* In the 58503A 3704, alarm_text's frame (0x29740): link -6, the
 * cursor's text at -6 and severity at -2; and its tree's nine 10-byte
 * entries.  This image carries neither alarm_text nor its tree. */
CHECK_SIZE(alarm_text_cursor, 6);
CHECK_FIELD(alarm_text_cursor, severity, -2 - -6);
CHECK_SIZE(alarm_text_entry, 10);
CHECK_FIELD(alarm_text_entry, severity, 8);

/* port_error_reports at 0x102e48, as scpi_session_init fills it:
 * the masks at +0, +4, +8 and +0xc, the conditions at +2 to +0xe
 * (0x28dfc to 0x28e24) */
CHECK_SIZE(port_error_report, 4);
CHECK_FIELD(port_error_report, condition, 2);

/* The fields common/'s C reaches through a global, whose offsets an
 * absolute address in the image's code holds, where rule 1's masking of
 * addresses cannot see them: each where the image's code names it, or,
 * where it does not, at its offset in the global (make check-common;
 * docs/firmware/rewrite.md, "Shared C"). */
CHECK_FIELD(boot_selftest_results, checksum, 0x10ac34 - 0x10ac34);
CHECK_FIELD(counter_configuration_fields, sync_state, 0x102689 - 0x102678);
CHECK_FIELD(duarta_driver, held, 0x10369e - 0x103696);
CHECK_FIELD(duarta_driver, transmit_count, 0x10369a - 0x103696);
CHECK_FIELD(duarta_driver, transmit_next, 0x103696 - 0x103696);
CHECK_FIELD(eeprom_lock_token, exchange, 0x1023e4 - 0x1023e0);
CHECK_FIELD(eeprom_lock_token, status, 0x1023e0 - 0x1023e0);
CHECK_FIELD(efc_range_state, clamped, 0x1023b0 - 0x1023b0);
CHECK_FIELD(efc_range_state, near_end, 0x1023b1 - 0x1023b0);
CHECK_FIELD(exception_record, pending, 0x100bec - 0x100bec);
CHECK_FIELD(fpga_configuration_table, last, 0x30ecc - 0x30ecc);
CHECK_FIELD(gps_command_log, entries, 0x101ade - 0x101ade);
CHECK_FIELD(gps_command_mailbox, code, 0x102c40 - 0x102c40);
CHECK_FIELD(holdover_steering_state, ti_largest, 0x102720 - 0x10270c);
CHECK_FIELD(holdover_steering_state, ti_weighted_sum, 0x10271c - 0x10270c);
CHECK_FIELD(identification_record, checksum, 0x400010 - 0x400010);
CHECK_FIELD(identification_record, model, 0x400012 - 0x400010);
CHECK_FIELD(identification_record, serial, 0x400024 - 0x400010);
CHECK_FIELD(interpolator_calibration, calibrated, 0x102699 - 0x102698);
CHECK_FIELD(interpolator_calibration, reading_low, 0x10269f - 0x102698);
CHECK_FIELD(interval_reading_fields, value, 0x102666 - 0x102666);
CHECK_FIELD(lifetime_state, efc, 0x1023c0 - 0x1023c0);
CHECK_FIELD(psos_configuration, data_area, 0x102cfc - 0x102cfc);
CHECK_FIELD(qspi_state, copy_pending, 0x103854 - 0x103808);
CHECK_FIELD(qspi_state, field_00, 0x103808 - 0x103808);
CHECK_FIELD(qspi_state, read, 0x10380a - 0x103808);
CHECK_FIELD(qspi_state, receive_count, 0x103852 - 0x103808);
CHECK_FIELD(qspi_state, write, 0x103809 - 0x103808);
CHECK_FIELD(serial_ring, data, 0x103595 - 0x103593);
CHECK_FIELD(serial_ring, read, 0x103594 - 0x103593);
CHECK_FIELD(serial_ring, write, 0x103593 - 0x103593);
CHECK_FIELD(task_table_entry, state, 0x102d68 - 0x102d68);
