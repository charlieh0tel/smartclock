/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  This is the Z3805A 1.01.03's copy of
 * the 58503B 1.01.04's, its base (docs/firmware/z3805a-1.01.03.md):
 * each check's addresses are this image's ("make check-common" derives
 * them), a ROM table the image's export does not name is checked by
 * its size alone, and the checks of what the image has not are gone.
 * A block's comment is its base's, or the Z3817A 3752's before it, and
 * the addresses it cites theirs, but where it says the block is this
 * image's or cites "make record-access".  Compiled, never run, by
 * "make check-types" with an m68k compiler, so that int, long and
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

CHECK_SIZE(loop_steering_state, 0x102b26 - 0x102b0a);
CHECK_FIELD(loop_steering_state, seconds, 0x102b16 - 0x102b0a);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x102b1a - 0x102b0a);
CHECK_FIELD(loop_steering_state, filter_weight, 0x102b22 - 0x102b0a);

CHECK_SIZE(serial_ring, 0x103bb3 - 0x103ab1);
CHECK_SIZE(serial_ring, 0x103cc1 - 0x103bbf);
CHECK_FIELD(serial_ring, data, 0x103ab3 - 0x103ab1);
CHECK_FIELD(serial_ring, read, 0x103ab2 - 0x103ab1);
CHECK_FIELD(serial_ring, write, 0x103ab1 - 0x103ab1);

CHECK_SIZE(duarta_driver, 0x103bbe - 0x103bb4);
CHECK_FIELD(duarta_driver, overrun, 0x103bba - 0x103bb4);
CHECK_FIELD(duarta_driver, least_free, 0x103bbd - 0x103bb4);
CHECK_FIELD(duarta_driver, held, 0x103bbc - 0x103bb4);
CHECK_FIELD(duarta_driver, transmit_count, 0x103bb8 - 0x103bb4);
CHECK_FIELD(duarta_driver, transmit_next, 0x103bb4 - 0x103bb4);

/* The SCI's settings at 0x102640, as sci_reconfigure reads them
 * (0x30730 on), and the installer's at 0x10acb0, as
 * installer_parameter_record_read loads them (installer::0x108398 on). */
CHECK_SIZE(sci_settings_record, 0x102a0a - 0x102a04);
CHECK_FIELD(sci_settings_record, baud, 0x102a06 - 0x102a04);
CHECK_FIELD(sci_settings_record, parity, 0x102a07 - 0x102a04);
CHECK_FIELD(sci_settings_record, xon_xoff, 0x102a08 - 0x102a04);
CHECK_FIELD(sci_settings_record, echo, 0x102a09 - 0x102a04);
/* DUART channel B's settings at 0x102648, 9 bytes, which
 * parameter_record_read loads from +2 to +8 (0x452b4 to 0x452e4) and
 * duartb_settings_preset copies whole (0x45394): the fields the other
 * images assert, each where this image's code reads it (make
 * record-access), the baud in duartb_baud_from_settings (0x2dd00), echo
 * in duart_init (0x2e320), field_05 and eight_bits in their queries
 * (0x4432a, 0x445b8). */
CHECK_SIZE(duartb_settings_record, 0x102a15 - 0x102a0c);
CHECK_FIELD(duartb_settings_record, baud, 0x102a0e - 0x102a0c);
CHECK_FIELD(duartb_settings_record, field_05, 0x102a11 - 0x102a0c);
CHECK_FIELD(duartb_settings_record, eight_bits, 0x102a12 - 0x102a0c);
CHECK_FIELD(duartb_settings_record, echo, 0x102a14 - 0x102a0c);
CHECK_SIZE(duartb_applied_settings, 0x103aa4 - 0x103aa0);
CHECK_SIZE(duartb_applied_settings, 0x10ad40 - 0x10ad3c);
CHECK_FIELD(duartb_applied_settings, baud, 0x103aa0 - 0x103aa0);
CHECK_FIELD(duartb_applied_settings, baud, 0x10ad3c - 0x10ad3c);
CHECK_SIZE(duart_shadow_registers, 0x103ab1 - 0x103aa4);
CHECK_FIELD(duart_shadow_registers, imr, 0x103aa8 - 0x103aa4);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x103aab - 0x103aa4);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x103aaf - 0x103aa4);

CHECK_SIZE(exchange_token, 16);
/* The SCI's exchanges at 0x103cc2, as sci_init and sci_lock address
 * them (0x3047a, 0x303de), its ring at 0x103cfa, as sci_getc does, and
 * its driver at 0x103dfc, each field where the driver's code addresses
 * it (sci_puts, sci_getc, sci_reconfigure, sci_send_async,
 * sci_transmitting_set, sci_transmit_end_schedule, sci_gets). */
CHECK_SIZE(sci_exchange_state, 0x10414c - 0x104114);
CHECK_FIELD(sci_exchange_state, read_token, 0x104118 - 0x104114);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x104130 - 0x104114);
CHECK_FIELD(sci_exchange_state, write_token, 0x104134 - 0x104114);
CHECK_SIZE(serial_ring, 0x10424e - 0x10414c);

/* The SCI's driver, sci at 0x103ee6, before qspi (0x103f04), as the
 * driver's code addresses it (make record-access): flow at 0x103ef3
 * (sci_transmit_allowed, 0x2e958), the echo at 0x103efe (0x2e5e0). */
CHECK_SIZE(sci_driver, 0x10426c - 0x10424e);
CHECK_FIELD(sci_driver, transmit_count, 0x104252 - 0x10424e);
CHECK_FIELD(sci_driver, async_next, 0x104254 - 0x10424e);
CHECK_FIELD(sci_driver, async_count, 0x104258 - 0x10424e);
CHECK_FIELD(sci_driver, async_pending, 0x104259 - 0x10424e);
CHECK_FIELD(sci_driver, flow, 0x10425b - 0x10424e);
CHECK_FIELD(sci_driver, receive_errors, 0x10425c - 0x10424e);
CHECK_FIELD(sci_driver, read_errors, 0x10425d - 0x10424e);
CHECK_FIELD(sci_driver, xon_xoff, 0x10425e - 0x10424e);
CHECK_FIELD(sci_driver, mark_bit, 0x10425f - 0x10424e);
CHECK_FIELD(sci_driver, seven_bits, 0x104260 - 0x10424e);
CHECK_FIELD(sci_driver, reconfiguring, 0x104261 - 0x10424e);
CHECK_FIELD(sci_driver, async_allowed, 0x104262 - 0x10424e);
CHECK_FIELD(sci_driver, released_for_timecode, 0x104263 - 0x10424e);
CHECK_FIELD(sci_driver, baud_constant, 0x104264 - 0x10424e);
CHECK_FIELD(sci_driver, echo, 0x104266 - 0x10424e);
CHECK_FIELD(sci_driver, last_terminator, 0x10426a - 0x10424e);
CHECK_FIELD(sci_driver, field_0c, 0x10425a - 0x10424e);
CHECK_FIELD(sci_driver, field_1d, 0x10426b - 0x10424e);
CHECK_SIZE(qspi_state, 0x1042ba - 0x10426c);
CHECK_FIELD(qspi_state, waiting, 0x104270 - 0x10426c);
CHECK_FIELD(qspi_state, busy, 0x1042b0 - 0x10426c);
CHECK_FIELD(qspi_state, receive_buffer, 0x1042b2 - 0x10426c);
CHECK_FIELD(qspi_state, running, 0x1042b9 - 0x10426c);
CHECK_FIELD(qspi_state, copy_pending, 0x1042b8 - 0x10426c);
CHECK_FIELD(qspi_state, field_00, 0x10426c - 0x10426c);
CHECK_FIELD(qspi_state, read, 0x10426e - 0x10426c);
CHECK_FIELD(qspi_state, receive_count, 0x1042b6 - 0x10426c);
CHECK_FIELD(qspi_state, write, 0x10426d - 0x10426c);

CHECK_SIZE(eeprom_lock_token, 0x1027a2 - 0x102792);
CHECK_FIELD(eeprom_lock_token, owner, 0x10279a - 0x102792);
CHECK_FIELD(eeprom_lock_token, depth, 0x10279e - 0x102792);
CHECK_FIELD(eeprom_lock_token, exchange, 0x102796 - 0x102792);
CHECK_FIELD(eeprom_lock_token, status, 0x102792 - 0x102792);

/* The 58503A 3704's duartb at 0x103412, each field where the driver's
 * code addresses it (duart_init, drtb_puts, drtb_gets, drtb_send_async,
 * duartb_isr, duartb_transmit_interrupt), echo and last_terminator
 * where drtb_gets loads them (0x1d642); the installer's at 0x10af0c, as
 * its copies address it (0x10361c to 0x103cbc in its RAM). */

/* The installer's copies, laid out as the primary's. */

CHECK_FIELD(duart_shadow_registers, imr, 0x10ad44 - 0x10ad40);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x10ad47 - 0x10ad40);
CHECK_FIELD(duart_shadow_registers, csrb, 0x10ad49 - 0x10ad40);

CHECK_SIZE(psos_configuration, 0x62);
CHECK_SIZE(psos_configuration, 0x1034b6 - 0x103454);
CHECK_FIELD(psos_configuration, data_end, 0x103458 - 0x103454);
CHECK_FIELD(psos_configuration, region_2_start, 0x103460 - 0x103454);
CHECK_FIELD(psos_configuration, task_records, 0x10346c - 0x103454);
CHECK_FIELD(psos_configuration, exchange_records, 0x10346e - 0x103454);
CHECK_FIELD(psos_configuration, message_buffers, 0x103470 - 0x103454);
CHECK_FIELD(psos_configuration, last_device, 0x103476 - 0x103454);
CHECK_FIELD(psos_configuration, io_table, 0x103478 - 0x103454);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x10347c - 0x103454);
CHECK_FIELD(psos_configuration, root_entry, 0x103480 - 0x103454);
CHECK_FIELD(psos_configuration, root_priority, 0x103488 - 0x103454);
CHECK_FIELD(psos_configuration, start_hook, 0x10348a - 0x103454);
CHECK_FIELD(psos_configuration, delete_hook, 0x10348e - 0x103454);
CHECK_FIELD(psos_configuration, debug_hooks, 0x103496 - 0x103454);
CHECK_FIELD(psos_configuration, task_hooks, 0x10349a - 0x103454);
CHECK_FIELD(psos_configuration, data_area, 0x103454 - 0x103454);

/* The boot code's results, at 0x10ac50, up to the installer's settings;
 * the latch's shadow a word, as boot_trap_11 writes it (0x5c2). */
CHECK_SIZE(boot_selftest_results, 0x10ab30 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, cpu, 0x10ab22 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram, 0x10ab24 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram_data_bits, 0x10ab26 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, ram_address_lines, 0x10ab28 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, uart, 0xc);
CHECK_FIELD(boot_selftest_results, latch, 0x10ab2e - 0x10ab20);
CHECK_FIELD(boot_selftest_results, checksum, 0x10ab20 - 0x10ab20);

/* The kernel's configuration and I/O table, as this image's
 * psos_config_init copies them: 0x62 bytes from 0x55890 to 0x103454
 * (0x5569a) and 0x90 from 0x55d48 to 0x1033c4 (0x556b0), four
 * devices'. */

CHECK_SIZE(io_device_jumps[4], 0x103454 - 0x1033c4);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x102ed2 + 6n, its state at 0x102ed0 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x1034c2 - 0x1034c0);
CHECK_SIZE(task_table_entry[17], 0x103526 - 0x1034c0);
CHECK_FIELD(task_table_entry, state, 0x1034c0 - 0x1034c0);

CHECK_SIZE(spoo_request, 0x10377e - 0x103766);
CHECK_FIELD(spoo_request, function, 0x10376e - 0x103766);
CHECK_FIELD(spoo_request, arguments, 0x103772 - 0x103766);
CHECK_SIZE(early_status_events, 0x103a94 - 0x103a8c);

CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x10333e - 0x10332a);
CHECK_FIELD(efc_gain_values, reciprocal_ti, 0x103332 - 0x10332a);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x103336 - 0x10332a);
CHECK_FIELD(efc_gain_values, step_limit, 0x10333a - 0x10332a);
CHECK_SIZE(efc_range_state, 0x102743 - 0x102740);
CHECK_FIELD(efc_range_state, near_end_checks, 0x102742 - 0x102740);
CHECK_FIELD(efc_range_state, clamped, 0x102740 - 0x102740);
CHECK_FIELD(efc_range_state, near_end, 0x102741 - 0x102740);

CHECK_SIZE(gps_command_mailbox, 0x103354 - 0x10333e);
CHECK_FIELD(gps_command_mailbox, arguments, 0x103340 - 0x10333e);
CHECK_FIELD(gps_command_mailbox, request, 0x103350 - 0x10333e);
CHECK_FIELD(gps_command_mailbox, code, 0x10333e - 0x10333e);

/* adc_monitors at 0x1021e4, eight, efc_range_state after them */
CHECK_SIZE(adc_monitor, 0x1025f0 - 0x1025c0);
CHECK_SIZE(adc_monitor[8], 0x102740 - 0x1025c0);
CHECK_FIELD(adc_monitor, value, 0x1025e8 - 0x1025c0);
CHECK_FIELD(adc_monitor, valid, 0x1025ec - 0x1025c0);
CHECK_FIELD(adc_monitor, sample_index, 0x1025ed - 0x1025c0);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x10261e - 0x1025f0);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x1025ef - 0x1025c0);
CHECK_SIZE(hmon_channel_entry, 0x2361e - 0x235f4);
CHECK_SIZE(hmon_channel_entry[8], 0x23744 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, add, 0x235f8 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, check, 0x235fc - 0x235f4);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x23600 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, report, 0x23604 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, minimum, 0x23608 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, maximum, 0x2360c - 0x235f4);
CHECK_FIELD(hmon_channel_entry, nominal, 0x23610 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, name, 0x23614 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, events, 0x23618 - 0x235f4);
CHECK_FIELD(hmon_channel_entry, bit, 0x2361a - 0x235f4);
CHECK_FIELD(hmon_channel_entry, field_28, 0x2361c - 0x235f4);
/* gps_request_step at 0x10215e */
CHECK_SIZE(gps_request_code_word, 0x1021ba - 0x1021b8);
CHECK_SIZE(gps_script_step, 0x1025a6 - 0x102592);
CHECK_FIELD(gps_script_step, argument_mode, 0x102594 - 0x102592);
CHECK_FIELD(gps_script_step, arguments, 0x102596 - 0x102592);

/* selftest at 0x1012a0, each field where this image's code addresses
 * it (make record-access), the 58503A 3704's layout: its latch a byte
 * at +0x0c, the record 0x48 long, the GPS record after it (0x1012e8). */

CHECK_SIZE(selftest_results, 0x1012e8 - 0x1012a0);
CHECK_FIELD(selftest_results, uart, 0x1012aa - 0x1012a0);
CHECK_FIELD(selftest_results, latch, 0x1012ac - 0x1012a0);
CHECK_FIELD(selftest_results, lamp_mask, 0x1012ad - 0x1012a0);
CHECK_FIELD(selftest_results, summary, 0x1012ae - 0x1012a0);
CHECK_FIELD(selftest_results, eeprom, 0x1012b0 - 0x1012a0);
CHECK_FIELD(selftest_results, fpga, 0x1012b2 - 0x1012a0);
CHECK_FIELD(selftest_results, gps, 0x1012b4 - 0x1012a0);
CHECK_FIELD(selftest_results, interpolator, 0x1012b8 - 0x1012a0);
CHECK_FIELD(selftest_results, interpolator_calibrated, 0x1012ba - 0x1012a0);
CHECK_FIELD(selftest_results, interpolator_tries, 0x1012bb - 0x1012a0);
CHECK_FIELD(selftest_results, interpolator_readings, 0x1012bc - 0x1012a0);
CHECK_FIELD(selftest_results, abort, 0x1012c0 - 0x1012a0);
CHECK_FIELD(selftest_results, running, 0x1012c1 - 0x1012a0);
CHECK_FIELD(selftest_results, power, 0x1012c2 - 0x1012a0);
CHECK_FIELD(selftest_results, internal_reference, 0x1012c4 - 0x1012a0);
CHECK_FIELD(selftest_results, qspi, 0x1012c6 - 0x1012a0);
CHECK_FIELD(selftest_results, lamp_testing, 0x1012d6 - 0x1012a0);
CHECK_FIELD(selftest_results, passed, 0x1012d8 - 0x1012a0);
CHECK_FIELD(selftest_results, result, 0x1012da - 0x1012a0);
CHECK_FIELD(selftest_results, details, 0x1012de - 0x1012a0);
CHECK_FIELD(selftest_results, test, 0x1012e6 - 0x1012a0);
CHECK_FIELD(selftest_results, in_run, 0x1012e7 - 0x1012a0);

/* gpsx_send's frame, 26 bytes from -0x1a(A6) (0x4bb50), and
 * request_ring_put's copy, six longs and a word (0x524b6 to 0x524c2):
 * the 58503A 3704's. */
CHECK_SIZE(gps_request_entry, 28);
CHECK_FIELD(gps_request_entry, sender, 0x08);
CHECK_FIELD(gps_request_entry, result, 0x09);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, argument_mode, 0x0c);
CHECK_FIELD(gps_request_entry, arguments, 0x0e);
CHECK_FIELD(gps_request_entry, sent, 0x12);
CHECK_FIELD(gps_request_entry, untranslated_code, 0x14);
CHECK_FIELD(gps_request_entry, untranslated_argument_mode, 0x16);
/* gps_requests at 0x101b88, this image's: read and write at +0xa8 and
 * +0xac (request_ring_clear, 0x4d6c4 and 0x4d6c0). */
CHECK_FIELD(gps_request_ring, read, 0xa8);
CHECK_FIELD(gps_request_ring, write, 0xac);
CHECK_SIZE(gps_request_ring, 0xb0);

/* command_log at 0x101e5c, the Z3805A 1.01.03's: entries of 0x16 bytes
 * and its cursors at 0x1022be and 0x1022c2, which command_log_add steps
 * by 0x16 and wraps once past 0x1022bd (0x4e896); an entry's fields
 * where command_log_add's frame holds them, the entry at -22 (0x4e8a8
 * to 0x4e93a). */
CHECK_SIZE(gps_command_log_entry, 0x16);
CHECK_FIELD(gps_command_log_entry, untranslated_code, 22 - 20);
CHECK_FIELD(gps_command_log_entry, code, 22 - 18);
CHECK_FIELD(gps_command_log_entry, message, 22 - 16);
CHECK_FIELD(gps_command_log_entry, argument_text, 22 - 14);
CHECK_FIELD(gps_command_log_entry, query, 22 - 11);
CHECK_FIELD(gps_command_log_entry, from_clock, 22 - 10);
CHECK_FIELD(gps_command_log_entry, date, 22 - 8);
CHECK_FIELD(gps_command_log_entry, time, 22 - 4);
CHECK_FIELD(gps_command_log, entries, 0x101e5c - 0x101e5c);
CHECK_FIELD(gps_command_log, read, 0x1022be - 0x101e5c);
CHECK_FIELD(gps_command_log, write, 0x1022c2 - 0x101e5c);
CHECK_SIZE(gps_command_log, 0x1022c6 - 0x101e5c);

/* sky_statistics at 0x1022c6, to raim_suspect_time's alignment
 * (0x10258a): its fields where sky_statistics_clear clears them (0x4f38a
 * to 0x4f3c8) and sky_statistics_update indexes them (0x4f4b6, 0x4f6a0
 * to 0x4f6b2). */
CHECK_SIZE(sky_statistics_record, 0x10258a - 0x1022c6);
CHECK_SIZE(sky_statistics_hours, 0x19);
CHECK_FIELD(sky_statistics_record, cells, 0x1022c6 - 0x1022c6);
CHECK_FIELD(sky_statistics_record, hours_seen, 0x10254e - 0x1022c6);
CHECK_FIELD(sky_statistics_record, hours, 0x102552 - 0x1022c6);
CHECK_FIELD(sky_statistics_record, hours.current, 0x10256a - 0x1022c6);
CHECK_FIELD(sky_statistics_record, hour_average, 0x10256b - 0x1022c6);
CHECK_FIELD(sky_statistics_record, average, 0x102584 - 0x1022c6);
CHECK_FIELD(sky_statistics_record, samples, 0x102588 - 0x1022c6);

/* gps_periodic at 0x101a14, this image's, to status_screen_print_interval
 * (0x101a1c): its fields where gps_init clears them (0x4dc3e to
 * 0x4dc4a); and gps_periodic_send's sentences, copied 6 bytes, a long
 * and a word (0x4dd56, 0x4dd58). */
CHECK_SIZE(gps_periodic_state, 0x101a1c - 0x101a14);
CHECK_FIELD(gps_periodic_state, query_code, 0x101a16 - 0x101a14);
CHECK_FIELD(gps_periodic_state, queries, 0x101a18 - 0x101a14);
CHECK_FIELD(gps_periodic_state, sends, 0x101a1a - 0x101a14);
CHECK_SIZE(gps_periodic_stage, 1);
CHECK_SIZE(gps_fixed_sentence, 6);
CHECK_FIELD(gps_fixed_sentence, length, 4);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x10276c - 0x102760);
CHECK_FIELD(lifetime_state, date, 0x102764 - 0x102760);
CHECK_FIELD(lifetime_state, count, 0x102768 - 0x102760);
CHECK_FIELD(lifetime_state, efc, 0x102760 - 0x102760);

CHECK_SIZE(qspi_buffers, 8);
CHECK_SIZE(scpi_step, 8);
CHECK_SIZE(parameter_record, 16);
CHECK_FIELD(parameter_record, sci_baud, 9);
CHECK_SIZE(identification_record, 48);
CHECK_FIELD(identification_record, suffix, 0x26);
CHECK_FIELD(identification_record, checksum, 0x400010 - 0x400010);
CHECK_FIELD(identification_record, model, 0x400012 - 0x400010);
CHECK_FIELD(identification_record, serial, 0x400024 - 0x400010);

CHECK_SIZE(lifetime_record, 32);
CHECK_FIELD(lifetime_record, checksum, 0);
CHECK_FIELD(lifetime_record, date, 2);
CHECK_FIELD(lifetime_record, count, 6);
CHECK_FIELD(lifetime_record, efc, 0xa);

CHECK_SIZE(efc_calibration_fields, 0x1033c4 - 0x10338e);
CHECK_FIELD(efc_calibration_fields, point, 0x10338f - 0x10338e);
CHECK_FIELD(efc_calibration_fields, field_02, 0x103390 - 0x10338e);
CHECK_FIELD(efc_calibration_fields, frequency_offset, 0x103392 - 0x10338e);
CHECK_FIELD(efc_calibration_fields, adc_low, 0x1033ba - 0x10338e);
CHECK_FIELD(efc_calibration_fields, adc_high, 0x1033be - 0x10338e);
CHECK_FIELD(efc_calibration_fields, settle, 0x1033c2 - 0x10338e);

CHECK_SIZE(ptest_run, 0x102ac0 - 0x102ab6);
CHECK_FIELD(ptest_run, passes, 0x102ab7 - 0x102ab6);
CHECK_FIELD(ptest_run, start_uptime, 0x102ab8 - 0x102ab6);
CHECK_FIELD(ptest_run, end_uptime, 0x102abc - 0x102ab6);

CHECK_SIZE(calibration_block, 64);
CHECK_FIELD(calibration_block, time_offset, 6);
/* save_calibration_block's frame, the block at -64 (0x326aa to
 * 0x326f2) */
CHECK_FIELD(calibration_block, tc_coefficient, 0x40 - 0x3e);
CHECK_FIELD(calibration_block, field_0a, 0x40 - 0x36);
CHECK_FIELD(calibration_block, efc_dac_per_adc, 0x40 - 0x34);
CHECK_FIELD(calibration_block, efc_a, 0x40 - 0x30);
CHECK_FIELD(calibration_block, efc_b, 0x40 - 0x2c);
CHECK_FIELD(calibration_block, tc_coefficient_second, 0x40 - 0x28);
CHECK_FIELD(calibration_block, oscillator, 0x40 - 0x24);
/* efc_calibration_types' entries, 12 bytes each from 0x329ec, as
 * efc_calibration_defaults, efc_calibration_limits and
 * efc_calibration_measure index them (0x32772 to 0x3277a, 0x328b8,
 * 0x328c4). */
CHECK_SIZE(efc_calibration_type, 12);
CHECK_FIELD(efc_calibration_type, a_minimum, 0x329f0 - 0x329ec);
CHECK_FIELD(efc_calibration_type, a_maximum, 0x329f4 - 0x329ec);
/* nv_save's frame (link -160, 0x43734), the sequence it loads from
 * +0x4 (0x4376a) and the slots' stride (0x43976 to 0x43980) */
CHECK_SIZE(nv_record, 0xa0);
CHECK_FIELD(nv_record, sequence, 4);
CHECK_FIELD(nv_record, packed, 0x10);
CHECK_SIZE(reset_record, 0x5c);
CHECK_FIELD(reset_record, text, 0xc);

CHECK_SIZE(sprintf_output, 8);
CHECK_SIZE(scpi_decimal, 20);
CHECK_FIELD(scpi_decimal, exponent, 0x12);
CHECK_SIZE(scpi_multiplier[13], 0x17dd2 - 0x17d9e);
CHECK_SIZE(scpi_unit[2], 0x0c);
CHECK_FIELD(scpi_keyword, text, 2);
CHECK_SIZE(scpi_keyword, 0x14);
CHECK_FIELD(scpi_unit, exponent, 4);
CHECK_SIZE(scpi_error_message[101], 0x25e);
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
CHECK_FIELD(fpga_configuration_table, last, 0x2409c - 0x2409c);

/* status_create allocates 0x16 bytes and four a register; the first
 * four of those are registers[0]. */
CHECK_SIZE(status_model, 0x12 + 7 * 4);
CHECK_FIELD(status_model, output, 0x02);
CHECK_FIELD(status_model, queue, 0x06);
CHECK_FIELD(status_model, conditions, 0x0a);
CHECK_FIELD(status_model, field_0e, 0x0e);
CHECK_FIELD(status_model, registers, 0x12);

/* The Oncore records this image's gps holds: tps's date (0x58506) and
 * the hold position the tps commit fills (0x5d1c6). */
CHECK_FIELD(oncore_date_fields, day, 0x1015ed - 0x1015ec);
CHECK_FIELD(oncore_date_fields, year, 0x1015ee - 0x1015ec);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x1010e4 - 0x1010e0);
CHECK_FIELD(oncore_hold_position_fields, height, 0x1010e8 - 0x1010e0);

CHECK_SIZE(aging_curve_state, 0x102ab6 - 0x102a8c);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x102a8d - 0x102a8c);
CHECK_FIELD(aging_curve_state, oldest_time, 0x102a8e - 0x102a8c);
CHECK_FIELD(aging_curve_state, newest_time, 0x102a92 - 0x102a8c);
CHECK_FIELD(aging_curve_state, a, 0x102a9e - 0x102a8c);
CHECK_FIELD(aging_curve_state, b, 0x102aa2 - 0x102a8c);
CHECK_FIELD(aging_curve_state, c, 0x102aa6 - 0x102a8c);
CHECK_FIELD(aging_curve_state, rms, 0x102aae - 0x102a8c);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x102ab2 - 0x102a8c);

/* The GPS task's state at 0x1012e8: memset(&gps, 0, 0x72a) by gps_init
 * (0x4dc26), and pllp_task copies 0x1ca longs and a word of it to
 * preserved_region (0x48590); each field where this image's code
 * reaches it, through gps's address or a base register loaded with it
 * (make record-access), the sentences' records where nmea_sentences'
 * entries point (0x54dee), the 58503B's tail at +0x10c. */
CHECK_SIZE(gps_state, 0x1ca * 4 + 2);
CHECK_FIELD(gps_state, mask_angle, 0x101304 - 0x1012e8);
CHECK_FIELD(gps_state, ignored_satellites, 0x10131a - 0x1012e8);
CHECK_FIELD(gps_state, hold_position, 0x101394 - 0x1012e8);
CHECK_FIELD(gps_state, position_hold, 0x1013a2 - 0x1012e8);
CHECK_FIELD(gps_state, pps_cable_delay_given, 0x1013b6 - 0x1012e8);
CHECK_FIELD(gps_state, cable_delay_correction, 0x1013ba - 0x1012e8);
CHECK_FIELD(gps_state, status_received, 0x1013c0 - 0x1012e8);
CHECK_FIELD(gps_state, status_rate, 0x1013c1 - 0x1012e8);
CHECK_FIELD(gps_state, status, 0x1013c2 - 0x1012e8);
CHECK_FIELD(gps_state, status.receiver_status, 0x101418 - 0x1012e8);
CHECK_FIELD(gps_state, visibility_received, 0x10141a - 0x1012e8);
CHECK_FIELD(gps_state, visibility, 0x10141c - 0x1012e8);
CHECK_FIELD(gps_state, raim_satellites, 0x101496 - 0x1012e8);
CHECK_FIELD(gps_state, xdop_table, 0x10149b - 0x1012e8);
CHECK_FIELD(gps_state, corrections, 0x1015b8 - 0x1012e8);
CHECK_FIELD(gps_state, extension, 0x10160c - 0x1012e8);
CHECK_FIELD(gps_state, raim, 0x101650 - 0x1012e8);
CHECK_FIELD(gps_state, raim.sawtooth, 0x10166a - 0x1012e8);
CHECK_FIELD(gps_state, raim_received, 0x1016b4 - 0x1012e8);
CHECK_FIELD(gps_state, raim_alarm_date, 0x1016b6 - 0x1012e8);
CHECK_FIELD(gps_state, raim_alarm_sigma, 0x1016be - 0x1012e8);
CHECK_FIELD(gps_state, raim_alarm_channels, 0x1016c0 - 0x1012e8);
CHECK_FIELD(gps_state, antenna_sense, 0x10170b - 0x1012e8);
CHECK_FIELD(gps_state, self_test, 0x10170c - 0x1012e8);
CHECK_FIELD(gps_state, fix_mode, 0x10177a - 0x1012e8);
CHECK_FIELD(gps_state, receiver_id_received, 0x10177c - 0x1012e8);
CHECK_FIELD(gps_state, receiver_id, 0x10177d - 0x1012e8);
CHECK_FIELD(gps_state, gga, 0x101892 - 0x1012e8);
CHECK_FIELD(gps_state, gsa, 0x1018a8 - 0x1012e8);
CHECK_FIELD(gps_state, gsv, 0x1018b2 - 0x1012e8);
CHECK_FIELD(gps_state, anc, 0x1018fc - 0x1012e8);
CHECK_FIELD(gps_state, acc, 0x10190a - 0x1012e8);
CHECK_FIELD(gps_state, ssd, 0x10190e - 0x1012e8);
CHECK_FIELD(gps_state, ssd.mask_angle, 0x10190f - 0x1012e8);
CHECK_FIELD(gps_state, tst, 0x10191e - 0x1012e8);
CHECK_FIELD(gps_state, tps, 0x101928 - 0x1012e8);
CHECK_FIELD(gps_state, rrm, 0x101954 - 0x1012e8);
CHECK_FIELD(gps_state, rsd, 0x10195c - 0x1012e8);
CHECK_FIELD(gps_state, field_678, 0x101960 - 0x1012e8);
CHECK_FIELD(gps_state, average_count, 0x101962 - 0x1012e8);
CHECK_FIELD(gps_state, surveying, 0x1019b2 - 0x1012e8);
CHECK_FIELD(gps_state, leap_second_pending, 0x1019c8 - 0x1012e8);
CHECK_FIELD(gps_state, time_mode, 0x1019c9 - 0x1012e8);
CHECK_FIELD(gps_state, leap_second_announced, 0x6ce);
CHECK_FIELD(gps_state, leap_year, 0x6d2);
CHECK_FIELD(gps_state, leap_time, 0x6dc);
CHECK_FIELD(gps_state, poll_monitor_enabled, 0x1019f0 - 0x1012e8);
CHECK_FIELD(gps_state, poll_monitor_state, 0x1019f1 - 0x1012e8);
CHECK_FIELD(gps_state, poll_monitor_query, 0x70a);
CHECK_FIELD(gps_state, poll_monitor_time, 0x1019f6 - 0x1012e8);
CHECK_FIELD(gps_state, pps_check_disabled, 0x6e5);
CHECK_FIELD(gps_state, references[0].state, 0x69f);
CHECK_FIELD(gps_state, status.channels, 0x100);
CHECK_FIELD(gps_state, raim_assessment, 0x1019cc - 0x1012e8);
CHECK_FIELD(gps_state, receiver_health_failed, 0x1019ce - 0x1012e8);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x1019d0 - 0x1012e8);
CHECK_FIELD(gps_state, status_monitor_time, 0x1019de - 0x1012e8);
CHECK_FIELD(gps_state, field_6fa, 0x1019e2 - 0x1012e8);
CHECK_FIELD(gps_state, channel_count, 0x1019fa - 0x1012e8);
CHECK_FIELD(gps_state, field_592, 0x1019fc - 0x1012e8);
CHECK_FIELD(gps_state, field_715, 0x1019fd - 0x1012e8);
CHECK_FIELD(gps_state, track_words, 0x101a00 - 0x1012e8);
CHECK_FIELD(gps_state, channel_word_lowest, 0x101a10 - 0x1012e8);

/* the sentences' records, from their fields above */
CHECK_SIZE(nmea_gga_fields, 0x5c0 - 0x5aa);
CHECK_SIZE(nmea_gsa_fields, 0x5ca - 0x5c0);
CHECK_SIZE(nmea_gsv_satellite, 0x602 - 0x5fc);
CHECK_SIZE(nmea_anc_fields, 0x622 - 0x614);
CHECK_FIELD(nmea_anc_fields, marked, 0x61c - 0x614);
CHECK_FIELD(nmea_anc_fields, satellites, 0x61e - 0x614);
CHECK_SIZE(nmea_ssd_fields, 0x636 - 0x626);
CHECK_FIELD(nmea_ssd_fields, ignored_satellites, 0x62a - 0x626);
CHECK_FIELD(nmea_ssd_fields, height, 0x62e - 0x626);
CHECK_FIELD(nmea_ssd_fields, cable_delay, 0x632 - 0x626);
CHECK_SIZE(nmea_tst_fields, 0x640 - 0x636);
CHECK_SIZE(nmea_tps_fields, 0x66c - 0x640);
/* twelve channels: the receiver status after them (0x53920) */
CHECK_SIZE(oncore_status_fields, 0x131 - 0xda);
CHECK_SIZE(oncore_visibility_fields, 0x7a);
CHECK_SIZE(oncore_xdop_table_fields, 75);
CHECK_SIZE(oncore_almanac_status_fields, 16);
CHECK_SIZE(oncore_corrections_fields, 64);
/* to raim_received (0x53bda) */
CHECK_SIZE(oncore_raim_fields, 0x3cc - 0x368);
/* gps_print_extension's URAs, twelve, and the matrix after them */
CHECK_SIZE(oncore_extension_fields, 0x368 - 0x324);
CHECK_FIELD(oncore_extension_fields, ura, 0x344 - 0x324);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x350 - 0x324);

/* the two reference records, gps_reference_reset's (0x4cb44; 0x101986,
 * 0x10199c) */

CHECK_SIZE(gps_reference_record, 0x10199c - 0x101986);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* gps_reference_hysteresis_set (0x4cb72) stores the GPS's at 0x101998
 * and 0x10199a */
CHECK_FIELD(gps_reference_record, hysteresis, 0x101998 - 0x101986);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x10199a - 0x101986);

/* The loop's state at 0x102c14: pllp_task copies 0x1bf longs and a word
 * of it to preserved_region (0x485a4); each field where this image's
 * code reaches it (make record-access); the hold causes a word
 * (diag_pr_hold_cause). */
CHECK_SIZE(pll_state, 0x1bf * 4 + 2);
CHECK_FIELD(pll_state, previous_stage, 0x102c15 - 0x102c14);
CHECK_FIELD(pll_state, powerup_countdown, 0x102c16 - 0x102c14);
CHECK_FIELD(pll_state, powerup_state, 0x102c17 - 0x102c14);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x102c18 - 0x102c14);
CHECK_FIELD(pll_state, field_005, 0x102c19 - 0x102c14);
CHECK_FIELD(pll_state, reference_check_state, 0x102c1a - 0x102c14);
CHECK_FIELD(pll_state, field_007, 0x102c1b - 0x102c14);
CHECK_FIELD(pll_state, startup_state, 0x102c20 - 0x102c14);
CHECK_FIELD(pll_state, field_00d, 0x102c21 - 0x102c14);
CHECK_FIELD(pll_state, normal_state, 0x102c22 - 0x102c14);
CHECK_FIELD(pll_state, field_00f, 0x102c23 - 0x102c14);
CHECK_FIELD(pll_state, hold_causes, 0x102c24 - 0x102c14);
CHECK_FIELD(pll_state, holdover_state, 0x102c26 - 0x102c14);
CHECK_FIELD(pll_state, manual, 0x102c27 - 0x102c14);
CHECK_FIELD(pll_state, ti_limit_override, 0x102c28 - 0x102c14);
CHECK_FIELD(pll_state, field_015, 0x102c29 - 0x102c14);
CHECK_FIELD(pll_state, holdover_efc_offset, 0x102c2a - 0x102c14);
CHECK_FIELD(pll_state, holdover_efc_step, 0x102c2e - 0x102c14);
CHECK_FIELD(pll_state, recovery_countdown, 0x102c32 - 0x102c14);
CHECK_FIELD(pll_state, recovery_state, 0x102c33 - 0x102c14);
CHECK_FIELD(pll_state, coarse_state, 0x102c34 - 0x102c14);
CHECK_FIELD(pll_state, fine_state, 0x102c35 - 0x102c14);
CHECK_FIELD(pll_state, frequency_adjusted, 0x102c36 - 0x102c14);
CHECK_FIELD(pll_state, fine_index, 0x102c37 - 0x102c14);
CHECK_FIELD(pll_state, fine_limit, 0x102c38 - 0x102c14);
CHECK_FIELD(pll_state, field_025, 0x102c39 - 0x102c14);
CHECK_FIELD(pll_state, fine_retries, 0x102c3a - 0x102c14);
CHECK_FIELD(pll_state, fine_d, 0x102c3c - 0x102c14);
CHECK_FIELD(pll_state, aging_state, 0x102c40 - 0x102c14);
CHECK_FIELD(pll_state, field_02d, 0x102c41 - 0x102c14);
CHECK_FIELD(pll_state, gps_diag_state, 0x102c44 - 0x102c14);
CHECK_FIELD(pll_state, gps_diag_substate, 0x102c45 - 0x102c14);
CHECK_FIELD(pll_state, gps_init_state, 0x102c46 - 0x102c14);
CHECK_FIELD(pll_state, field_033, 0x102c47 - 0x102c14);
CHECK_FIELD(pll_state, leap_check_timer, 0x102c48 - 0x102c14);
CHECK_FIELD(pll_state, leap_check_requested, 0x102c4a - 0x102c14);
CHECK_FIELD(pll_state, leap_check_state, 0x102c4b - 0x102c14);
CHECK_FIELD(pll_state, leap_ack_state, 0x102c4c - 0x102c14);
CHECK_FIELD(pll_state, leap_ack_requested, 0x102c4d - 0x102c14);
CHECK_FIELD(pll_state, leap_ack_done, 0x102c4e - 0x102c14);
CHECK_FIELD(pll_state, field_03b, 0x102c4f - 0x102c14);
CHECK_FIELD(pll_state, leap_ack_again, 0x102c50 - 0x102c14);
CHECK_FIELD(pll_state, field_03d, 0x102c51 - 0x102c14);
CHECK_FIELD(pll_state, efc, 0x102c52 - 0x102c14);
CHECK_FIELD(pll_state, tfom, 0x102c56 - 0x102c14);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x102c5a - 0x102c14);
CHECK_FIELD(pll_state, measured_uncertainty, 0x102c5e - 0x102c14);
CHECK_FIELD(pll_state, field_04e, 0x102c62 - 0x102c14);
CHECK_FIELD(pll_state, frequency_tolerance, 0x102c6a - 0x102c14);
CHECK_FIELD(pll_state, efc_history, 0x102c6e - 0x102c14);
CHECK_FIELD(pll_state, aging_term, 0x1032dc - 0x102c14);
CHECK_FIELD(pll_state, loop_efc_step, 0x1032e0 - 0x102c14);
CHECK_FIELD(pll_state, ti_filtered, 0x1032e4 - 0x102c14);
CHECK_FIELD(pll_state, efc_base, 0x1032e8 - 0x102c14);
CHECK_FIELD(pll_state, loop_time_constant, 0x1032ec - 0x102c14);
CHECK_FIELD(pll_state, field_6dc, 0x1032f0 - 0x102c14);
CHECK_FIELD(pll_state, led_gps_lock, 0x1032f1 - 0x102c14);
CHECK_FIELD(pll_state, led_holdover, 0x1032f2 - 0x102c14);
CHECK_FIELD(pll_state, field_6df, 0x1032f3 - 0x102c14);
CHECK_FIELD(pll_state, holdover_duration, 0x1032f4 - 0x102c14);
CHECK_FIELD(pll_state, holdover_start_uptime, 0x1032f8 - 0x102c14);
CHECK_FIELD(pll_state, reference_uptime, 0x1032fc - 0x102c14);
CHECK_FIELD(pll_state, reference_time, 0x103300 - 0x102c14);
CHECK_FIELD(pll_state, efc_control_passes, 0x103304 - 0x102c14);
CHECK_FIELD(pll_state, reference_seconds, 0x103308 - 0x102c14);
CHECK_FIELD(pll_state, fpga_configuration, 0x103309 - 0x102c14);
CHECK_FIELD(pll_state, fpga_loaded, 0x10330a - 0x102c14);
CHECK_FIELD(pll_state, loop_running, 0x10330b - 0x102c14);
CHECK_FIELD(pll_state, sync_requested, 0x10330c - 0x102c14);
CHECK_FIELD(pll_state, field_6f9, 0x10330d - 0x102c14);
CHECK_FIELD(pll_state, field_6fa, 0x10330e - 0x102c14);
CHECK_FIELD(pll_state, field_6fb, 0x10330f - 0x102c14);
CHECK_FIELD(pll_state, block_seconds, 0x103310 - 0x102c14);
CHECK_FIELD(pll_state, measured_uncertainty_valid, 0x103311 - 0x102c14);
CHECK_SIZE(pll_hold_cause, 2);

/* the history 0x66e bytes, at 0x102c6e: efc_history_update's (0x41a20)
 * and the fits' indexed arrays of 64, measured_uncertainty_update's fit
 * arrays */
CHECK_SIZE(efc_ring, 0x1032dc - 0x102c6e);
CHECK_FIELD(efc_ring, stop_ptr, 0x102c6f - 0x102c6e);
CHECK_FIELD(efc_ring, loop_settled, 0x102c70 - 0x102c6e);
CHECK_FIELD(efc_ring, field_003, 0x102c71 - 0x102c6e);
CHECK_FIELD(efc_ring, field_004, 0x102c72 - 0x102c6e);
CHECK_FIELD(efc_ring, summed_count, 0x102c74 - 0x102c6e);
CHECK_FIELD(efc_ring, averages, 0x102c76 - 0x102c6e);
CHECK_FIELD(efc_ring, replaced_average, 0x102d76 - 0x102c6e);
CHECK_FIELD(efc_ring, weights, 0x102d7a - 0x102c6e);
CHECK_FIELD(efc_ring, replaced_weight, 0x102dba - 0x102c6e);
CHECK_FIELD(efc_ring, field_14d, 0x102dbb - 0x102c6e);
CHECK_FIELD(efc_ring, times, 0x102dbc - 0x102c6e);
CHECK_FIELD(efc_ring, replaced_time, 0x102ebc - 0x102c6e);
CHECK_FIELD(efc_ring, logs, 0x102ec0 - 0x102c6e);
CHECK_FIELD(efc_ring, fit_a_history, 0x102fc0 - 0x102c6e);
CHECK_FIELD(efc_ring, fit_b_history, 0x1030c0 - 0x102c6e);
CHECK_FIELD(efc_ring, fit_c_history, 0x1031c0 - 0x102c6e);
CHECK_FIELD(efc_ring, next_entry_uptime, 0x1032c0 - 0x102c6e);
CHECK_FIELD(efc_ring, efc_sum, 0x1032c4 - 0x102c6e);
CHECK_FIELD(efc_ring, efc_first, 0x1032c8 - 0x102c6e);
CHECK_FIELD(efc_ring, field_65e, 0x1032cc - 0x102c6e);
CHECK_FIELD(efc_ring, fit_a, 0x1032d0 - 0x102c6e);
CHECK_FIELD(efc_ring, fit_b, 0x1032d4 - 0x102c6e);
CHECK_FIELD(efc_ring, fit_c, 0x1032d8 - 0x102c6e);

/* Records the printf calls name; in the 58503A 3704 @@Cj's decoder's
 * table (0x52620) lays the receiver ID out as the Z3805A 3543B's: 34
 * bytes, then 9 lines of 27. */

/* The receiver ID's lines (0x5262c): 12 bytes each, the table ending
 * at 0x52698, its tenth; the model number's and the options' texts at
 * 0x1012e7 and 0x101353, in gps.receiver_id from 0x101259. */

/* gps_counts (0x101a8a), 0x44 bytes (clear_gps_debug, 0x4eaf8): the
 * RAIM counts gps_print_time_raim prints (0x49a6c, 0x49a76), the
 * framer's (0x541a0, 0x541da) and command_monitor's (0x4d08e,
 * 0x4d2be). */
CHECK_SIZE(gps_statistics, 0x44);
CHECK_FIELD(gps_statistics, field_0e, 0xe);
CHECK_FIELD(gps_statistics, responses_read, 0x22);
CHECK_FIELD(gps_statistics, e_retries, 0x26);
CHECK_FIELD(gps_statistics, t_retries, 0x28);
CHECK_FIELD(gps_statistics, raim_detected, 0x101ab4 - 0x101a8a);
CHECK_FIELD(gps_statistics, raim_alarms, 0x101ab6 - 0x101a8a);
CHECK_FIELD(gps_statistics, unknown_response, 0x36);
CHECK_FIELD(gps_statistics, frame_errors, 0x101ac2 - 0x101a8a);
CHECK_FIELD(gps_statistics, checksum_errors, 0x101ac4 - 0x101a8a);
CHECK_FIELD(gps_statistics, address_errors, 0x3c);
CHECK_FIELD(gps_statistics, data_errors, 0x3e);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x101aca - 0x101a8a);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x101acc - 0x101a8a);

/* The NMEA framer's record at gps_response_buffer (0x101a1e): the
 * cursor gps_response_cursor (0x101a70), the framer's counts at +0x56 and
 * +0x57 and its field offsets from +0x58 (0x5d6dc, 0x5d71a). */
CHECK_FIELD(nmea_frame, cursor, 0x101a70 - 0x101a1e);
CHECK_FIELD(nmea_frame, fields, 0x56);
CHECK_FIELD(nmea_frame, checksum_digits, 0x57);
CHECK_FIELD(nmea_frame, field_starts, 0x58);
CHECK_SIZE(nmea_frame, 0x101a8a - 0x101a1e);
CHECK_SIZE(exception_record, 0x10102c - 0x10101a);
CHECK_FIELD(exception_record, name, 0x10101b - 0x10101a);
CHECK_FIELD(exception_record, sr, 0x101020 - 0x10101a);
CHECK_FIELD(exception_record, pc, 0x101022 - 0x10101a);
CHECK_FIELD(exception_record, vector, 0x101026 - 0x10101a);
CHECK_FIELD(exception_record, address, 0x101028 - 0x10101a);
CHECK_FIELD(exception_record, pending, 0x10101a - 0x10101a);

/* The pool: five entries from 0x10372a to 0x1037ac. */
CHECK_SIZE(klok_entry, (0x103810 - 0x10378e) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read's page, log_page_buffer at 0x102452: entry n's fields at
 * 0x102456, 0x10245a, 0x10245e, ... + n * 0x14. */
CHECK_SIZE(log_page, 0x10284a - 0x10280a);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x10280e - 0x10280a);
CHECK_FIELD(log_entry, format, 0x102816 - 0x10280e);
CHECK_FIELD(log_entry, argument_1, 0x10281a - 0x10280e);
CHECK_SIZE(fpga_bit, 4);
/* interpolator_calibrate reads and writes param[0] to param[8]. */
CHECK_SIZE(interpolator_calibration, 9);
CHECK_FIELD(interpolator_calibration, result_valid, 4);
CHECK_FIELD(interpolator_calibration, reading_high, 8);
CHECK_FIELD(interpolator_calibration, calibrated, 0x102a83 - 0x102a82);
CHECK_FIELD(interpolator_calibration, reading_low, 0x102a89 - 0x102a82);

/* The packed enums are bytes, as the fields they type. */
CHECK_SIZE(pll_stage, 1);
CHECK_SIZE(adc_channel, 1);
CHECK_SIZE(oncore_channel_mode, 1);
CHECK_SIZE(oncore_raim_status, 1);

/* nv_settings at 0x1023d2, 0xcb bytes, nv_restore's copy of its
 * defaults, its length 0x10249d less 0x1023d2 (0x4366e): each field
 * where its descriptor's value points, in nv_save's tables
 * (nv_numeric_params, 0x43bda; nv_setting_params, 0x43c36;
 * docs/firmware/structures.md, "The Z3817A 3752's record"). */
CHECK_SIZE(nv_settings_fields, 0x102a03 - 0x102938);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x10293a - 0x102938);
CHECK_FIELD(nv_settings_fields, antenna_delay, 0x10293e - 0x102938);
CHECK_FIELD(nv_settings_fields, douglas_duration, 0x102942 - 0x102938);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x102946 - 0x102938);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x102947 - 0x102938);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x102948 - 0x102938);
CHECK_FIELD(nv_settings_fields, external_questionable_hysteresis,
            0x10294c - 0x102938);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x102950 - 0x102938);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x102954 - 0x102938);
CHECK_FIELD(nv_settings_fields, holdover_limit_threshold, 0x102958 - 0x102938);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x10295c - 0x102938);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x10295d - 0x102938);
CHECK_FIELD(nv_settings_fields, position, 0x10295e - 0x102938);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x10296a - 0x102938);
CHECK_FIELD(nv_settings_fields, time_zone, 0x10296e - 0x102938);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x102976 - 0x102938);
CHECK_FIELD(nv_settings_fields, leap_date, 0x10297a - 0x102938);
CHECK_FIELD(nv_settings_fields, leap_minute_length, 0x10297e - 0x102938);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x102982 - 0x102938);
CHECK_FIELD(nv_settings_fields, raim_state, 0x102983 - 0x102938);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x102986 - 0x102938);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x10298e - 0x102938);
CHECK_FIELD(nv_settings_fields, status_enable, 0x102992 - 0x102938);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x1029ae - 0x102938);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x1029c2 - 0x102938);
CHECK_FIELD(nv_settings_fields, log_wrap, 0x1029d6 - 0x102938);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x1029d7 - 0x102938);
CHECK_FIELD(nv_settings_fields, serial_restricted, 0x1029d8 - 0x102938);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x1029d9 - 0x102938);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x1029da - 0x102938);
CHECK_FIELD(nv_settings_fields, pulse_start_time, 0x1029de - 0x102938);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x1029ea - 0x102938);
CHECK_FIELD(nv_settings_fields, field_be, 0x1029f6 - 0x102938);
CHECK_FIELD(nv_settings_fields, output_state, 0x1029fa - 0x102938);
CHECK_FIELD(nv_settings_fields, format_data, 0x1029fb - 0x102938);
CHECK_FIELD(nv_settings_fields, memory_save, 0x1029fc - 0x102938);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x1029fd - 0x102938);
CHECK_FIELD(nv_settings_fields, douglas_state_powerup, 0x1029fe - 0x102938);
CHECK_FIELD(nv_settings_fields, tcode_format, 0x1029ff - 0x102938);
CHECK_FIELD(nv_settings_fields, gps_utc, 0x102a00 - 0x102938);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x102a01 - 0x102938);
CHECK_FIELD(nv_settings_fields, debug_output, 0x102a02 - 0x102938);

/* each is followed by the next descriptor's widths */
CHECK_SIZE(scpi_setting_param, 0x352f6 - 0x352d8 - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* 14 action descriptors, each referred to at its own address, from
 * 0x4132a to efc_history_update at 0x4137e */
CHECK_SIZE(scpi_action_param, (0x32f6e - 0x32f1a) / 14);
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
CHECK_SIZE(oncore_message_entry, 60);
CHECK_FIELD(oncore_message_entry, filter, 0x38);
CHECK_FIELD(oncore_message_entry, kind, 4);
CHECK_FIELD(oncore_message_entry, encoders, 6);
CHECK_FIELD(oncore_message_entry, query, 0x16);
CHECK_FIELD(oncore_message_entry, timeout, 0x1a);
CHECK_FIELD(oncore_message_entry, length, 0x1c);
CHECK_FIELD(oncore_message_entry, match_kind, 0x1e);
CHECK_FIELD(oncore_message_entry, decoders, 0x20);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x24);

/* The time-interval counter's reading, at the Z3815A 4010's 0x10268c,
 * its flag at 0x102694 and armed flag at 0x102698. */
CHECK_SIZE(interval_reading_fields, 0x102a5d - 0x102a50);
CHECK_FIELD(interval_reading_fields, valid, 0x102a58 - 0x102a50);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x102a5c - 0x102a50);
CHECK_FIELD(interval_reading_fields, value, 0x102a50 - 0x102a50);

/* counter_configuration (0x102a62), ending where
 * interpolator_calibration_state begins (0x102a82). */
CHECK_SIZE(counter_configuration_fields, 0x102a82 - 0x102a62);
CHECK_FIELD(counter_configuration_fields, fpga_registers, 0);
CHECK_FIELD(counter_configuration_fields, calibration, 4);
CHECK_FIELD(counter_configuration_fields, registers_mode, 0xc);
CHECK_FIELD(counter_configuration_fields, registers_state, 0xd);
CHECK_FIELD(counter_configuration_fields, calibrate_mode, 0xe);
CHECK_FIELD(counter_configuration_fields, calibrate_state, 0xf);
CHECK_FIELD(counter_configuration_fields, sync_mode, 0x10);
CHECK_FIELD(counter_configuration_fields, sync_state, 0x11);
CHECK_FIELD(counter_configuration_fields, sync_kind, 0x12);
CHECK_FIELD(counter_configuration_fields, sync_count, 0x14);
CHECK_FIELD(counter_configuration_fields, interpolator_offset, 0x18);
CHECK_FIELD(counter_configuration_fields, interpolator_scale, 0x1c);

/* The status snapshot, the Z3805A 1.01.03's one at 0x101c3c
 * (status_screen_print, 0x4c9aa), to the word status_snapshot stores at
 * +0x214 (0x4bf9a). */
CHECK_SIZE(status_snapshot_record, 0x101e52 - 0x101c3c);
/* its fields, by status_snapshot's stores (0x4be7a to 0x4c1ac) */
CHECK_FIELD(status_snapshot_record, satellites, 0x32);
CHECK_FIELD(status_snapshot_record, not_tracked, 0x5c);
CHECK_FIELD(status_snapshot_record, tracked, 0x14c);
CHECK_FIELD(status_snapshot_record, status_received, 0x1c4);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 0x1cc);
CHECK_FIELD(status_snapshot_record, gps_flags, 0x1d8);
CHECK_FIELD(status_snapshot_record, time, 0x1dc);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 0x1f0);
CHECK_FIELD(status_snapshot_record, position, 0x1fe);
CHECK_FIELD(status_snapshot_record, average_count, 0x20c);
CHECK_FIELD(status_snapshot_record, hardware_condition, 0x214);
/* status_screen_card_lines, six entries of 6 bytes from 0x59716 */
CHECK_SIZE(status_screen_card_line[6], 0x552f8 - 0x552d4);
CHECK_FIELD(status_screen_card_line, text, 0x552dc - 0x552da);
/* the lists' stride (0x4c7e0) */
CHECK_SIZE(satellite_list_entry, 10);
CHECK_SIZE(satellite_sets, 0x60 - 0x36);

/* The preserved region at 0x100000, the 0xfca bytes record_checksum
 * sums: gps's copy at 0x100004, pll's at 0x10072e and adc_monitors' at
 * 0x100e2c. */
/* preserved_region_fields: record_checksum's length (0x485cc) and
 * pllp_task's copies (0x48586 to 0x485c0). */
CHECK_SIZE(preserved_region_fields, 0xfca);
CHECK_FIELD(preserved_region_fields, gps, 4);
CHECK_FIELD(preserved_region_fields, pll, 0x72e);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0xe2c);

/* the 0x180 bytes pllp_task and system_init copy from 0x101e78: the
 * ADC monitors, which monitor_average_add indexes by 0x30 (0x21d40) */

/* The clock record at 0x100c6e: the routines' offsets, at the address
 * the derivation from the 58503A's code gives (layout_addresses.py). */
CHECK_SIZE(clock_record, 0x101067 - 0x101034);
CHECK_FIELD(clock_record, uptime, 0x101036 - 0x101034);
CHECK_FIELD(clock_record, time, 0x10103a - 0x101034);
CHECK_FIELD(clock_record, date, 0x10103e - 0x101034);
CHECK_FIELD(clock_record, month_days, 0x101042 - 0x101034);
CHECK_FIELD(clock_record, gps_time, 0x10104e - 0x101034);
CHECK_FIELD(clock_record, gps_date, 0x101052 - 0x101034);
CHECK_FIELD(clock_record, gps_month_days, 0x101056 - 0x101034);
CHECK_FIELD(clock_record, minute_length, 0x101062 - 0x101034);
CHECK_FIELD(clock_record, set, 0x101063 - 0x101034);
CHECK_FIELD(clock_record, gps_utc_offset, 0x101064 - 0x101034);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x101066 - 0x101034);

/* pps_message at 0x100e64, pps_exchange after the cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x101240 - 0x101228);
CHECK_FIELD(pps_exchange_message, gps_time, 0x101230 - 0x101228);
CHECK_FIELD(pps_exchange_message, gps_date, 0x101234 - 0x101228);

CHECK_SIZE(gps_process_state, 1);

/* the 58503A 3704's three status snapshots, 0x1016ae (status_screen_print,
 * 0x4b6ec), 0x10181e and 0x101988, with the two cells at 0x101818
 * between the first two; each field where status_snapshot (0x4ac56)
 * stores it */

/* status_screen_mode_lines' five entries, the last a terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x1e);
CHECK_FIELD(status_screen_mode_line, text, 0x02);

/* the event log's write page at 0x10240c, its number after it */
CHECK_SIZE(log_page, 0x102804 - 0x1027c4);

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
CHECK_FIELD(oncore_status_fields, dop_type, 0x23);
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
CHECK_SIZE(gps_running_command, 0x74);
CHECK_FIELD(gps_running_command, script, 0x1c);
CHECK_FIELD(gps_running_command, message, 0x20);
CHECK_FIELD(gps_running_command, query, 0x22);
CHECK_FIELD(gps_running_command, length, 0x23);
CHECK_FIELD(gps_running_command, sent, 0x24);
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

/* The ROM's packed script steps, each to the next (0x50a46 to 0x50cc2). */
CHECK_SIZE(gps_script_query_step, 4);
CHECK_SIZE(gps_script_step_1_argument, 8);
CHECK_SIZE(gps_script_step_2_arguments, 0x0c);
CHECK_SIZE(gps_script_step_3_arguments, 0x10);
CHECK_SIZE(gps_script_step, 0x14);
CHECK_SIZE(io_device_stubs, 0x24);
CHECK_SIZE(io_device_stubs, sizeof(io_device_jumps));
/* console_words' entries, as diag_words_define reads them (0x1c400 to
 * 0x1c418) */
CHECK_SIZE(console_word, 38);
CHECK_FIELD(console_word, name, 4);
CHECK_FIELD(console_word, argument_count, 30);
CHECK_FIELD(console_word, result_count, 34);
CHECK_SIZE(pforth_word_header, 0x0c);
CHECK_FIELD(pforth_word_header, flags, 0x0a);
CHECK_SIZE(diag_word_entry, 0x26);
CHECK_SIZE(diag_word_entry[86], 0xcc4);
CHECK_FIELD(diag_word_entry, argument_count, 0x1e);
CHECK_FIELD(diag_word_entry, result_count, 0x22);

/* The 58503A 3704's health monitor's channel table, eight entries from
 * 0x2238a to the channel names at 0x224da (0x21ca2 multiplies by 0x2a);
 * 0x2220a reads add, check and report at +4, +8 and +0x10, adc_average
 * nominal at +0x1c (0x222e4). */

/* 0x29740's frame: link -6, the cursor's text at -6 and severity at -2 */
CHECK_SIZE(alarm_text_cursor, 6);
CHECK_FIELD(alarm_text_cursor, severity, -2 - -6);
/* the tree's root at 0x29b42, its entries' table at 0x29ac2 to the
 * names at 0x29b1c, nine 10-byte entries, the last empty */

CHECK_FIELD(alarm_text_node, entries, 2);

/* port_error_reports at 0x102e48, as scpi_session_init fills it:
 * the masks at +0, +4, +8 and +0xc, the conditions at +2 to +0xe
 * (0x28dfc to 0x28e24) */
CHECK_SIZE(port_error_report, 4);
CHECK_FIELD(port_error_report, condition, 2);

/* The sentence descriptors, codes 0x40 to 0x55 from 0x54e9e to
 * 0x5515e, gps_request_descriptor's 0x20 a code (0x52890); set,M's
 * address, its test, its first encoder and its kind where
 * gps_request_build reads them (0x5438e to 0x543ce). */
CHECK_SIZE(gps_sentence_descriptor[22], 0x5515e - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, kind, 0x54ea2 - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, encoders, 0x54ea4 - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, query, 0x54eb4 - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, timeout, 0x54eb8 - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, match_kind, 0x54eb9 - 0x54e9e);
CHECK_FIELD(gps_sentence_descriptor, test, 0x54eba - 0x54e9e);

/* duartb_driver: duartb at 0x1040ac, loopback_receive_next after it
 * (0x104102); drtb_puts' count (0x1d784) and its test of reconfiguring
 * (0x1d794), the transmit interrupt's pending flag (0x1ec40),
 * duartb_isr's least free space (0x1eb30), drtb_getc's flow and flow
 * control (0x1d84a, 0x1d86c), the echo and last terminator drtb_gets
 * reads (make record-access). */
CHECK_SIZE(duartb_driver, 0x104102 - 0x1040ac);
CHECK_FIELD(duartb_driver, transmit_count, 0x1040b0 - 0x1040ac);
CHECK_FIELD(duartb_driver, puts_pending, 0x1040b3 - 0x1040ac);
CHECK_FIELD(duartb_driver, least_free, 0x1040b4 - 0x1040ac);
CHECK_FIELD(duartb_driver, reconfiguring, 0x1040b5 - 0x1040ac);
CHECK_FIELD(duartb_driver, flow, 0x1040b6 - 0x1040ac);
CHECK_FIELD(duartb_driver, xon_xoff, 0x1040b7 - 0x1040ac);
CHECK_FIELD(duartb_driver, read_errors, 0x1040b8 - 0x1040ac);
CHECK_FIELD(duartb_driver, async_allowed, 0x1040f8 - 0x1040ac);
/* drtb_timecode_released's byte (0x1e06c) */
CHECK_FIELD(duartb_driver, released_for_timecode, 0x1040f9 - 0x1040ac);
CHECK_FIELD(duartb_driver, echo, 0x1040fc - 0x1040ac);
CHECK_FIELD(duartb_driver, last_terminator, 0x104100 - 0x1040ac);

/* The installer's records, the Z3805A 3543B's, whose installer this
 * image's is byte for byte, at its addresses: installer_duartb is
 * installer_duartb_driver, not this image's primary's duartb_driver. */
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
CHECK_FIELD(installer_duartb_driver, async_next, 0x10af60 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, transmit_count, 0x10af64 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, async_pending, 0x10af67 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, flow, 0x10af68 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, read_errors, 0x10af6a - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, xon_xoff, 0x10af6c - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, parity, 0x10af6e - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, in_transmit_interrupt,
            0x10af70 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, read_token.task, 0x10af82 - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, write_token.task, 0x10af9a - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, async_allowed, 0x10afaa - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, echo, 0x10afac - 0x10af5c);
CHECK_FIELD(installer_duartb_driver, last_terminator, 0x10afb0 - 0x10af5c);
CHECK_SIZE(installer_duartb_driver, 0x10afb2 - 0x10af5c);
CHECK_SIZE(duartb_settings_record, 0x10ab39 - 0x10ab30);
CHECK_SIZE(psos_configuration, 0x10abe6 - 0x10ab84);
CHECK_SIZE(io_device_jumps, (0x103454 - 0x1033c4) / 4);

/* The boot code's results, at 0x10ab20, up to the installer's settings. */
CHECK_SIZE(boot_selftest_results, 0x10ab30 - 0x10ab20);
CHECK_FIELD(boot_selftest_results, cpu, 0x10ab22 - 0x10ab20);
