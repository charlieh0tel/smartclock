/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  This is the Z3815A 4010's copy of the
 * 58503A 3704's, its base (docs/firmware/z3815a-4010.md): the 58503A's
 * types wave's layouts, checked at the 58503A's addresses, each block's
 * comment saying so, and the Z3805A's figures for the rest.  None is yet
 * read from the Z3815A's code.  Compiled, never run, by "make
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

CHECK_SIZE(holdover_steering_state, 0x1c);
CHECK_FIELD(holdover_steering_state, aging_limit, 0x102754 - 0x10274c);
CHECK_FIELD(holdover_steering_state, ti_sum, 0x102758 - 0x10274c);
CHECK_FIELD(holdover_steering_state, ti_count, 0x102764 - 0x10274c);
CHECK_FIELD(holdover_steering_state, ti_largest, 0x102760 - 0x10274c);
CHECK_FIELD(holdover_steering_state, ti_weighted_sum, 0x10275c - 0x10274c);

CHECK_SIZE(loop_steering_state, 0x102784 - 0x102768);
CHECK_FIELD(loop_steering_state, seconds, 0x102774 - 0x102768);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x102778 - 0x102768);
CHECK_FIELD(loop_steering_state, filter_weight, 0x102780 - 0x102768);

CHECK_SIZE(serial_ring, 0x103b4f - 0x103a4d);
CHECK_SIZE(serial_ring, 0x103c5d - 0x103b5b);
CHECK_SIZE(serial_ring, 0x10b006 - 0x10af04);
CHECK_FIELD(serial_ring, data, 0x103a4f - 0x103a4d);
CHECK_FIELD(serial_ring, read, 0x103a4e - 0x103a4d);
CHECK_FIELD(serial_ring, write, 0x103a4d - 0x103a4d);

CHECK_SIZE(duarta_driver, 0x103b5a - 0x103b50);
CHECK_FIELD(duarta_driver, overrun, 0x103b56 - 0x103b50);
CHECK_FIELD(duarta_driver, least_free, 0x103b59 - 0x103b50);
CHECK_FIELD(duarta_driver, held, 0x103b58 - 0x103b50);
CHECK_FIELD(duarta_driver, transmit_count, 0x103b54 - 0x103b50);
CHECK_FIELD(duarta_driver, transmit_next, 0x103b50 - 0x103b50);

/* The SCI's settings at 0x102640, as sci_reconfigure reads them
 * (0x30730 on), and the installer's at 0x10acb0, as
 * installer_parameter_record_read loads them (installer::0x108398 on). */
CHECK_SIZE(sci_settings_record, 0x102646 - 0x102640);
CHECK_FIELD(sci_settings_record, baud, 0x102642 - 0x102640);
CHECK_FIELD(sci_settings_record, parity, 0x102643 - 0x102640);
CHECK_FIELD(sci_settings_record, xon_xoff, 0x102644 - 0x102640);
CHECK_FIELD(sci_settings_record, echo, 0x102645 - 0x102640);
CHECK_FIELD(sci_settings_record, baud, 0x10acb2 - 0x10acb0);
CHECK_FIELD(sci_settings_record, echo, 0x10acb5 - 0x10acb0);
/* DUART channel B's settings at 0x102648, 9 bytes, which
 * parameter_record_read loads from +2 to +8 (0x452b4 to 0x452e4) and
 * duartb_settings_preset copies whole (0x45394): the fields the other
 * images assert, each where this image's code reads it (make
 * record-access), the baud in duartb_baud_from_settings (0x2dd00), echo
 * in duart_init (0x2e320), field_05 and eight_bits in their queries
 * (0x4432a, 0x445b8). */
CHECK_SIZE(duartb_settings_record, 0x102651 - 0x102648);
CHECK_FIELD(duartb_settings_record, baud, 0x10264a - 0x102648);
CHECK_FIELD(duartb_settings_record, field_05, 0x10264d - 0x102648);
CHECK_FIELD(duartb_settings_record, eight_bits, 0x10264e - 0x102648);
CHECK_FIELD(duartb_settings_record, echo, 0x102650 - 0x102648);
CHECK_SIZE(duartb_applied_settings, 0x103a40 - 0x103a3c);
CHECK_SIZE(duartb_applied_settings, 0x10aed0 - 0x10aecc);
CHECK_FIELD(duartb_applied_settings, baud, 0x103a3c - 0x103a3c);
CHECK_FIELD(duartb_applied_settings, baud, 0x10aecc - 0x10aecc);
CHECK_SIZE(duart_shadow_registers, 0x103a4d - 0x103a40);
CHECK_FIELD(duart_shadow_registers, imr, 0x103a44 - 0x103a40);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x103a47 - 0x103a40);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x103a4b - 0x103a40);

CHECK_SIZE(exchange_token, 16);
/* The SCI's exchanges at 0x103cc2, as sci_init and sci_lock address
 * them (0x3047a, 0x303de), its ring at 0x103cfa, as sci_getc does, and
 * its driver at 0x103dfc, each field where the driver's code addresses
 * it (sci_puts, sci_getc, sci_reconfigure, sci_send_async,
 * sci_transmitting_set, sci_transmit_end_schedule, sci_gets). */
CHECK_SIZE(sci_exchange_state, 0x103cfa - 0x103cc2);
CHECK_FIELD(sci_exchange_state, read_token, 0x103cc6 - 0x103cc2);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x103cde - 0x103cc2);
CHECK_FIELD(sci_exchange_state, write_token, 0x103ce2 - 0x103cc2);
CHECK_SIZE(serial_ring, 0x103dfc - 0x103cfa);

CHECK_SIZE(sci_driver, 0x103e1c - 0x103dfc);
CHECK_FIELD(sci_driver, transmit_count, 0x103e00 - 0x103dfc);
CHECK_FIELD(sci_driver, async_next, 0x103e02 - 0x103dfc);
CHECK_FIELD(sci_driver, async_count, 0x103e06 - 0x103dfc);
CHECK_FIELD(sci_driver, async_pending, 0x103e07 - 0x103dfc);
CHECK_FIELD(sci_driver, receive_errors, 0x103e09 - 0x103dfc);
CHECK_FIELD(sci_driver, read_errors, 0x103e0a - 0x103dfc);
CHECK_FIELD(sci_driver, xon_xoff, 0x103e0b - 0x103dfc);
CHECK_FIELD(sci_driver, mark_bit, 0x103e0c - 0x103dfc);
CHECK_FIELD(sci_driver, seven_bits, 0x103e0d - 0x103dfc);
CHECK_FIELD(sci_driver, reconfiguring, 0x103e0e - 0x103dfc);
CHECK_FIELD(sci_driver, field_13, 0x103e0f - 0x103dfc);
CHECK_FIELD(sci_driver, field_15, 0x103e11 - 0x103dfc);
CHECK_FIELD(sci_driver, transmitting, 0x103e12 - 0x103dfc);
CHECK_FIELD(sci_driver, transmit_busy, 0x103e1a - 0x103dfc);
CHECK_SIZE(qspi_state, 0x103e6a - 0x103e1c);
CHECK_FIELD(qspi_state, waiting, 0x103e20 - 0x103e1c);
CHECK_FIELD(qspi_state, busy, 0x103e60 - 0x103e1c);
CHECK_FIELD(qspi_state, receive_buffer, 0x103e62 - 0x103e1c);
CHECK_FIELD(qspi_state, running, 0x103e69 - 0x103e1c);
CHECK_FIELD(qspi_state, copy_pending, 0x103e68 - 0x103e1c);
CHECK_FIELD(qspi_state, field_00, 0x103e1c - 0x103e1c);
CHECK_FIELD(qspi_state, read, 0x103e1e - 0x103e1c);
CHECK_FIELD(qspi_state, receive_count, 0x103e66 - 0x103e1c);
CHECK_FIELD(qspi_state, write, 0x103e1d - 0x103e1c);

CHECK_SIZE(eeprom_lock_token, 0x1023ea - 0x1023da);
CHECK_FIELD(eeprom_lock_token, owner, 0x1023e2 - 0x1023da);
CHECK_FIELD(eeprom_lock_token, depth, 0x1023e6 - 0x1023da);
CHECK_FIELD(eeprom_lock_token, exchange, 0x1023de - 0x1023da);
CHECK_FIELD(eeprom_lock_token, status, 0x1023da - 0x1023da);

CHECK_FIELD(sci_driver, flow, 0x103e08 - 0x103dfc);
CHECK_FIELD(sci_driver, async_allowed, 0x103e10 - 0x103dfc);
CHECK_FIELD(sci_driver, baud_constant, 0x103e14 - 0x103dfc);
CHECK_FIELD(sci_driver, echo, 0x103e16 - 0x103dfc);
CHECK_FIELD(sci_driver, last_terminator, 0x103e1b - 0x103dfc);

/* The installer's SCI driver at 0x10b006, as its code addresses it:
 * installer_sci_puts (installer::0x1031ea, installer::0x1031f2),
 * installer_sci_send_async (installer::0x103c82 to installer::0x103caa),
 * its interrupt (installer::0x103434, installer::0x103588,
 * installer::0x103634), installer_sci_getc (installer::0x10329e,
 * installer::0x1032c0), installer_sci_gets (installer::0x1032e6,
 * installer::0x1032ec, installer::0x1032f2, installer::0x1033d4), the
 * reconfiguration (installer::0x103882, installer::0x103892,
 * installer::0x103aaa) and the initialization (installer::0x103a52,
 * installer::0x103a58, installer::0x103a66), the transmitting flag and
 * its end (installer::0x10304a, installer::0x1030b6). */
CHECK_SIZE(installer_sci_driver, 0x10b026 - 0x10b006);
CHECK_FIELD(installer_sci_driver, transmit_count, 0x10b00a - 0x10b006);
CHECK_FIELD(installer_sci_driver, async_next, 0x10b00c - 0x10b006);
CHECK_FIELD(installer_sci_driver, async_count, 0x10b010 - 0x10b006);
CHECK_FIELD(installer_sci_driver, async_pending, 0x10b011 - 0x10b006);
CHECK_FIELD(installer_sci_driver, puts_pending, 0x10b012 - 0x10b006);
CHECK_FIELD(installer_sci_driver, flow, 0x10b013 - 0x10b006);
CHECK_FIELD(installer_sci_driver, receive_errors, 0x10b014 - 0x10b006);
CHECK_FIELD(installer_sci_driver, read_errors, 0x10b015 - 0x10b006);
CHECK_FIELD(installer_sci_driver, xon_xoff, 0x10b016 - 0x10b006);
CHECK_FIELD(installer_sci_driver, mark_bit, 0x10b017 - 0x10b006);
CHECK_FIELD(installer_sci_driver, seven_bits, 0x10b018 - 0x10b006);
CHECK_FIELD(installer_sci_driver, reconfiguring, 0x10b019 - 0x10b006);
CHECK_FIELD(installer_sci_driver, in_transmit_interrupt, 0x10b01a - 0x10b006);
CHECK_FIELD(installer_sci_driver, async_allowed, 0x10b01b - 0x10b006);
CHECK_FIELD(installer_sci_driver, field_16, 0x10b01c - 0x10b006);
CHECK_FIELD(installer_sci_driver, transmitting, 0x10b01d - 0x10b006);
CHECK_FIELD(installer_sci_driver, baud_constant, 0x10b01e - 0x10b006);
CHECK_FIELD(installer_sci_driver, echo, 0x10b020 - 0x10b006);
CHECK_FIELD(installer_sci_driver, transmit_busy, 0x10b024 - 0x10b006);
CHECK_FIELD(installer_sci_driver, last_terminator, 0x10b025 - 0x10b006);

/* The 58503A 3704's duartb at 0x103412, each field where the driver's
 * code addresses it (duart_init, drtb_puts, drtb_gets, drtb_send_async,
 * duartb_isr, duartb_transmit_interrupt), echo and last_terminator
 * where drtb_gets loads them (0x1d642); the installer's at 0x10af0c, as
 * its copies address it (0x10361c to 0x103cbc in its RAM). */

/* The installer's copies, laid out as the primary's. */

CHECK_FIELD(duart_shadow_registers, imr, 0x10aed4 - 0x10aed0);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x10aed7 - 0x10aed0);
CHECK_FIELD(duart_shadow_registers, csrb, 0x10aed9 - 0x10aed0);

CHECK_SIZE(psos_configuration, 0x62);
CHECK_SIZE(psos_configuration, 0x102ec6 - 0x102e64);
CHECK_FIELD(psos_configuration, data_end, 0x102e68 - 0x102e64);
CHECK_FIELD(psos_configuration, region_2_start, 0x102e70 - 0x102e64);
CHECK_FIELD(psos_configuration, task_records, 0x102e7c - 0x102e64);
CHECK_FIELD(psos_configuration, exchange_records, 0x102e7e - 0x102e64);
CHECK_FIELD(psos_configuration, message_buffers, 0x102e80 - 0x102e64);
CHECK_FIELD(psos_configuration, last_device, 0x102e86 - 0x102e64);
CHECK_FIELD(psos_configuration, io_table, 0x102e88 - 0x102e64);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x102e8c - 0x102e64);
CHECK_FIELD(psos_configuration, root_entry, 0x102e90 - 0x102e64);
CHECK_FIELD(psos_configuration, root_priority, 0x102e98 - 0x102e64);
CHECK_FIELD(psos_configuration, start_hook, 0x102e9a - 0x102e64);
CHECK_FIELD(psos_configuration, delete_hook, 0x102e9e - 0x102e64);
CHECK_FIELD(psos_configuration, debug_hooks, 0x102ea6 - 0x102e64);
CHECK_FIELD(psos_configuration, task_hooks, 0x102eaa - 0x102e64);
CHECK_FIELD(psos_configuration, data_area, 0x102e64 - 0x102e64);
CHECK_SIZE(io_device_jumps, 0x10ab84 - 0x10ab60);

/* The boot code's results, at 0x10ac50, up to the installer's settings;
 * the latch's shadow a word, as boot_trap_11 writes it (0x5c2). */
CHECK_SIZE(boot_selftest_results, 0x10ac60 - 0x10ac50);
CHECK_FIELD(boot_selftest_results, cpu, 0x10ac52 - 0x10ac50);
CHECK_FIELD(boot_selftest_results, ram, 0x10ac54 - 0x10ac50);
CHECK_FIELD(boot_selftest_results, ram_data_bits, 0x10ac56 - 0x10ac50);
CHECK_FIELD(boot_selftest_results, ram_address_lines, 0x10ac58 - 0x10ac50);
CHECK_FIELD(boot_selftest_results, uart, 0xc);
CHECK_FIELD(boot_selftest_results, latch, 0x10ac5e - 0x10ac50);
CHECK_FIELD(boot_selftest_results, checksum, 0x10ac50 - 0x10ac50);

/* The kernel's configuration and I/O table, as psos_config_init copies
 * them: 0x62 bytes from 0x5ed0a to 0x102e64 and 0x90 from 0x5f1be to
 * 0x102dd4, four devices' (harness.py's PRIMARY_Z3815A_4010). */

CHECK_SIZE(io_device_jumps[4], 0x102e64 - 0x102dd4);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x102ed2 + 6n, its state at 0x102ed0 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x102ed2 - 0x102ed0);
CHECK_SIZE(task_table_entry[17], 0x102f36 - 0x102ed0);
CHECK_FIELD(task_table_entry, state, 0x102ed0 - 0x102ed0);

CHECK_SIZE(spoo_request, 0x10371a - 0x103702);
CHECK_FIELD(spoo_request, function, 0x10370a - 0x103702);
CHECK_FIELD(spoo_request, arguments, 0x10370e - 0x103702);
CHECK_SIZE(early_status_events, 0x103a30 - 0x103a28);

CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x10256c - 0x102554);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x102560 - 0x102554);
CHECK_FIELD(efc_gain_values, step_limit, 0x102564 - 0x102554);
CHECK_SIZE(efc_range_state, 0x102367 - 0x102364);
CHECK_FIELD(efc_range_state, near_end_checks, 0x102366 - 0x102364);
CHECK_FIELD(efc_range_state, clamped, 0x102364 - 0x102364);
CHECK_FIELD(efc_range_state, near_end, 0x102365 - 0x102364);

CHECK_SIZE(gps_command_mailbox, 0x102ca6 - 0x102c90);
CHECK_FIELD(gps_command_mailbox, arguments, 0x102c92 - 0x102c90);
CHECK_FIELD(gps_command_mailbox, request, 0x102ca2 - 0x102c90);
CHECK_FIELD(gps_command_mailbox, code, 0x102c90 - 0x102c90);

/* adc_monitors at 0x1021e4, eight, efc_range_state after them */
CHECK_SIZE(adc_monitor, 0x102214 - 0x1021e4);
CHECK_SIZE(adc_monitor[8], 0x102364 - 0x1021e4);
CHECK_FIELD(adc_monitor, value, 0x10220c - 0x1021e4);
CHECK_FIELD(adc_monitor, valid, 0x102210 - 0x1021e4);
CHECK_FIELD(adc_monitor, sample_index, 0x102211 - 0x1021e4);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x102242 - 0x102214);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x102213 - 0x1021e4);
CHECK_SIZE(hmon_channel_entry, 0x33cc6 - 0x33c9c);
CHECK_SIZE(hmon_channel_entry[8], 0x33dec - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, add, 0x33ca0 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, check, 0x33ca4 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x33ca8 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, report, 0x33cac - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, minimum, 0x33cb0 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, maximum, 0x33cb4 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, nominal, 0x33cb8 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, name, 0x33cbc - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, events, 0x33cc0 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, bit, 0x33cc2 - 0x33c9c);
CHECK_FIELD(hmon_channel_entry, field_28, 0x33cc4 - 0x33c9c);
/* gps_request_step at 0x10215e */
CHECK_SIZE(gps_request_code_word, 0x102160 - 0x10215e);
CHECK_SIZE(gps_script_step, 0x102172 - 0x10215e);
CHECK_FIELD(gps_script_step, argument_mode, 0x102160 - 0x10215e);
CHECK_FIELD(gps_script_step, arguments, 0x102162 - 0x10215e);

/* selftest at 0x100ee2 (the 58503A 3704's at 0x100e02), each field where
 * the Z3815A's code addresses it.  Its latch is a 16-bit word at +0x0c,
 * where the 58503A's is a byte, so from lamp_mask on the fields move on
 * by two: summary at +0x10, eeprom at +0x12, fpga at +0x14
 * (counter_selftest), the interpolator's from +0x1a (interpolator_selftest
 * and its kin), selftest_abort at +0x22 (0x100f04), the record 0x4a long. */

CHECK_SIZE(selftest_results, 0x4a);
CHECK_FIELD(selftest_results, uart, 0x0a);
CHECK_FIELD(selftest_results, latch, 0x100eee - 0x100ee2);
CHECK_FIELD(selftest_results, lamp_mask, 0x100ef0 - 0x100ee2);
CHECK_FIELD(selftest_results, summary, 0x100ef2 - 0x100ee2);
CHECK_FIELD(selftest_results, eeprom, 0x100ef4 - 0x100ee2);
CHECK_FIELD(selftest_results, fpga, 0x100ef6 - 0x100ee2);
CHECK_FIELD(selftest_results, gps, 0x100ef8 - 0x100ee2);
CHECK_FIELD(selftest_results, interpolator, 0x100efc - 0x100ee2);
CHECK_FIELD(selftest_results, interpolator_calibrated, 0x100efe - 0x100ee2);
CHECK_FIELD(selftest_results, interpolator_tries, 0x100eff - 0x100ee2);
CHECK_FIELD(selftest_results, interpolator_readings, 0x100f00 - 0x100ee2);
CHECK_FIELD(selftest_results, abort, 0x100f04 - 0x100ee2);
CHECK_FIELD(selftest_results, running, 0x100f05 - 0x100ee2);
CHECK_FIELD(selftest_results, power, 0x100f06 - 0x100ee2);
CHECK_FIELD(selftest_results, internal_reference, 0x100f08 - 0x100ee2);
CHECK_FIELD(selftest_results, qspi, 0x100f0a - 0x100ee2);
CHECK_FIELD(selftest_results, lamp_testing, 0x100f1a - 0x100ee2);
CHECK_FIELD(selftest_results, passed, 0x100f1c - 0x100ee2);
CHECK_FIELD(selftest_results, result, 0x100f1e - 0x100ee2);
CHECK_FIELD(selftest_results, details, 0x100f22 - 0x100ee2);
CHECK_FIELD(selftest_results, test, 0x100f2a - 0x100ee2);

/* gps_request_post's frame, 28 bytes from -0x1c(A6) (0x57a3e), and
 * gpsm_task's, which keeps the untranslated code and mode (0x57fd2). */
CHECK_SIZE(gps_request_entry, 28);
CHECK_FIELD(gps_request_entry, sender, 0x08);
CHECK_FIELD(gps_request_entry, result, 0x09);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, argument_mode, 0x0c);
CHECK_FIELD(gps_request_entry, arguments, 0x0e);
CHECK_FIELD(gps_request_entry, sent, 0x12);
CHECK_FIELD(gps_request_entry, untranslated_code, 0x14);
CHECK_FIELD(gps_request_entry, untranslated_argument_mode, 0x16);
/* gps_requests at 0x101712: read and write at +0xa8 and +0xac
 * (request_ring_put, 0x576c6). */
CHECK_FIELD(gps_request_ring, read, 0xa8);
CHECK_FIELD(gps_request_ring, write, 0xac);
CHECK_SIZE(gps_request_ring, 0xb0);

/* command_log at 0x101da8 and command_log_snapshot at 0x101f7e: read and
 * write at +0x1ce and +0x1d2 (command_log_add, 0x588ee to 0x5892c;
 * status_snapshot_update, 0x58b08 to 0x58b44), entries of 0x16 bytes
 * (0x588ec). */
CHECK_SIZE(gps_command_log_entry, 0x16);
CHECK_FIELD(gps_command_log_entry, untranslated_code, 0x02);
CHECK_FIELD(gps_command_log_entry, code, 0x04);
CHECK_FIELD(gps_command_log_entry, message, 0x06);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x08);
CHECK_FIELD(gps_command_log_entry, query, 0x0b);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0c);
CHECK_FIELD(gps_command_log_entry, date, 0x0e);
CHECK_FIELD(gps_command_log_entry, time, 0x12);
CHECK_SIZE(gps_command_log, 0x101f7e - 0x101da8);
CHECK_FIELD(gps_command_log, read, 0x101f76 - 0x101da8);
CHECK_FIELD(gps_command_log, write, 0x101f7a - 0x101da8);
CHECK_FIELD(gps_command_log, read, 0x10214c - 0x101f7e);
CHECK_FIELD(gps_command_log, write, 0x102150 - 0x101f7e);
CHECK_FIELD(gps_command_log, entries, 0x101da8 - 0x101da8);

/* gps_periodic at 0x1015a6, to status_screen_print_interval (0x1015ae):
 * its fields where gps_init clears them (0x57c2c to 0x57c38); and
 * gps_periodic_send's two sentences, copied 6 bytes, a long and a word
 * (0x57d30, 0x57d32). */
CHECK_SIZE(gps_periodic_state, 0x1015ae - 0x1015a6);
CHECK_FIELD(gps_periodic_state, query_code, 0x1015a8 - 0x1015a6);
CHECK_FIELD(gps_periodic_state, queries, 0x1015aa - 0x1015a6);
CHECK_FIELD(gps_periodic_state, sends, 0x1015ac - 0x1015a6);
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

CHECK_SIZE(lifetime_state, 0x1023b8 - 0x1023ac);
CHECK_FIELD(lifetime_state, date, 0x1023b0 - 0x1023ac);
CHECK_FIELD(lifetime_state, count, 0x1023b4 - 0x1023ac);
CHECK_FIELD(lifetime_state, efc, 0x1023ac - 0x1023ac);

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

CHECK_SIZE(input_debounce, 0x1023a4 - 0x1023a0);
CHECK_FIELD(input_debounce, count, 2);

CHECK_SIZE(puck_link, 0x1023a0 - 0x102374);
CHECK_FIELD(puck_link, efc_adc_expected, 0x102374 - 0x102374);
CHECK_FIELD(puck_link, correction, 0x102378 - 0x102374);
CHECK_FIELD(puck_link, efc_written, 0x10237c - 0x102374);
CHECK_FIELD(puck_link, correction_restart, 0x102380 - 0x102374);
CHECK_FIELD(puck_link, frames_since_efc, 0x102382 - 0x102374);
CHECK_FIELD(puck_link, adc_error_logged, 0x102386 - 0x102374);
CHECK_FIELD(puck_link, alive, 0x102387 - 0x102374);
CHECK_FIELD(puck_link, alive_until, 0x102388 - 0x102374);
CHECK_FIELD(puck_link, frame, 0x10238c - 0x102374);
CHECK_FIELD(puck_link, frame_length, 0x102393 - 0x102374);
CHECK_FIELD(puck_link, header, 0x102394 - 0x102374);
CHECK_FIELD(puck_link, status, 0x102395 - 0x102374);
CHECK_FIELD(puck_link, status1, 0x102396 - 0x102374);
CHECK_FIELD(puck_link, status2, 0x102397 - 0x102374);
CHECK_FIELD(puck_link, framing, 0x102398 - 0x102374);
CHECK_FIELD(puck_link, cursor, 0x10239a - 0x102374);
CHECK_FIELD(puck_link, bits, 0x10239e - 0x102374);

CHECK_SIZE(n3_lock_fields, 0x102d9e - 0x102d60);
CHECK_FIELD(n3_lock_fields, seating_error, 0x102d64 - 0x102d60);
CHECK_FIELD(n3_lock_fields, pll_passes, 0x102d66 - 0x102d60);
CHECK_FIELD(n3_lock_fields, phase_passes, 0x102d6a - 0x102d60);
CHECK_FIELD(n3_lock_fields, phase_offset, 0x102d6e - 0x102d60);
CHECK_FIELD(n3_lock_fields, frequency_sum, 0x102d72 - 0x102d60);
CHECK_FIELD(n3_lock_fields, averaging, 0x102d76 - 0x102d60);
CHECK_FIELD(n3_lock_fields, field_17, 0x102d77 - 0x102d60);
CHECK_FIELD(n3_lock_fields, efc_limit_count, 0x102d78 - 0x102d60);
CHECK_FIELD(n3_lock_fields, gain_ti, 0x102d7a - 0x102d60);
CHECK_FIELD(n3_lock_fields, gain_tau, 0x102d7e - 0x102d60);
CHECK_FIELD(n3_lock_fields, frequency_offset_1, 0x102d82 - 0x102d60);
CHECK_FIELD(n3_lock_fields, frequency_offset_2, 0x102d86 - 0x102d60);
CHECK_FIELD(n3_lock_fields, slew_offset, 0x102d8a - 0x102d60);
CHECK_FIELD(n3_lock_fields, passes, 0x102d8e - 0x102d60);
CHECK_FIELD(n3_lock_fields, reading_valid, 0x102d92 - 0x102d60);
CHECK_FIELD(n3_lock_fields, pass, 0x102d94 - 0x102d60);
CHECK_FIELD(n3_lock_fields, seating_count, 0x102d96 - 0x102d60);
CHECK_FIELD(n3_lock_fields, ti_average, 0x102d98 - 0x102d60);
CHECK_FIELD(n3_lock_fields, field_3c, 0x102d9c - 0x102d60);

CHECK_SIZE(efc_calibration_fields, 0x102dd4 - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, point, 0x102d9f - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, field_02, 0x102da0 - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, frequency_offset, 0x102da2 - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, adc_low, 0x102dca - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, adc_high, 0x102dce - 0x102d9e);
CHECK_FIELD(efc_calibration_fields, settle, 0x102dd2 - 0x102d9e);

CHECK_SIZE(ptest_run, 0x102708 - 0x1026fe);
CHECK_FIELD(ptest_run, passes, 0x1026ff - 0x1026fe);
CHECK_FIELD(ptest_run, start_uptime, 0x102700 - 0x1026fe);
CHECK_FIELD(ptest_run, end_uptime, 0x102704 - 0x1026fe);

CHECK_SIZE(calibration_block, 64);
CHECK_FIELD(calibration_block, time_offset, 6);
/* save_calibration_block's frame, the block at -64 (0x44fa4 to
 * 0x44fdc) */
CHECK_FIELD(calibration_block, tc_coefficient, 0x40 - 0x3e);
CHECK_FIELD(calibration_block, field_0a, 0x40 - 0x36);
CHECK_FIELD(calibration_block, efc_dac_per_adc, 0x40 - 0x34);
CHECK_FIELD(calibration_block, efc_a, 0x40 - 0x30);
CHECK_FIELD(calibration_block, efc_b, 0x40 - 0x2c);
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
CHECK_FIELD(fpga_configuration_table, last, 0x325ba - 0x325ba);

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
CHECK_FIELD(oncore_date_fields, day, 0x1014e5 - 0x1014e4);
CHECK_FIELD(oncore_date_fields, year, 0x1014e6 - 0x1014e4);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x100fd8 - 0x100fd4);
CHECK_FIELD(oncore_hold_position_fields, height, 0x100fdc - 0x100fd4);

/* @@Ek: in the 58503A 3704, 64 bytes of data in a 71-byte message, which
 * its decoder's table copies whole (0x525ed); @@Bk's (0x525e8) puts its
 * 38 bytes to the URAs first, zeroes the last two URAs and copies 14
 * more. */
CHECK_SIZE(oncore_extension_fields, 71 - 7);
CHECK_FIELD(oncore_extension_fields, ura, 0x20);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x28);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x3e);

CHECK_SIZE(aging_curve_state, 0x1026fe - 0x1026d4);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x1026d5 - 0x1026d4);
CHECK_FIELD(aging_curve_state, oldest_time, 0x1026d6 - 0x1026d4);
CHECK_FIELD(aging_curve_state, newest_time, 0x1026da - 0x1026d4);
CHECK_FIELD(aging_curve_state, a, 0x1026e6 - 0x1026d4);
CHECK_FIELD(aging_curve_state, b, 0x1026ea - 0x1026d4);
CHECK_FIELD(aging_curve_state, c, 0x1026ee - 0x1026d4);
CHECK_FIELD(aging_curve_state, rms, 0x1026f6 - 0x1026d4);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x1026fa - 0x1026d4);

/* The GPS task's state at 0x100f2c, memset(&gps, 0, 0x678) by
 * gps_init (0x57c14): each field where this image's code reaches
 * it, through gps's address or a base register loaded with it; the
 * sentences' fields where their decoders' table (0x5e32a) points. */
CHECK_SIZE(gps_state, 0x1015a4 - 0x100f2c);
CHECK_FIELD(gps_state, mask_angle, 0x100f48 - 0x100f2c);
CHECK_FIELD(gps_state, ignored_satellites, 0x100f5a - 0x100f2c);
/* the tps commit's hold position, 0x5d1c6 */
CHECK_FIELD(gps_state, hold_position, 0x100fd4 - 0x100f2c);
CHECK_FIELD(gps_state, hold_height_type, 0x100fe0 - 0x100f2c);
CHECK_FIELD(gps_state, position_hold, 0x100fe2 - 0x100f2c);
CHECK_FIELD(gps_state, utc_time, 0x100fea - 0x100f2c);
CHECK_FIELD(gps_state, pps_offset, 0x100fee - 0x100f2c);
CHECK_FIELD(gps_state, pps_cable_delay, 0x100ff2 - 0x100f2c);
CHECK_FIELD(gps_state, pps_cable_delay_given, 0x100ff6 - 0x100f2c);
CHECK_FIELD(gps_state, cable_delay_correction, 0x100ffa - 0x100f2c);
CHECK_FIELD(gps_state, status_received, 0x101000 - 0x100f2c);
CHECK_FIELD(gps_state, status_rate, 0x101001 - 0x100f2c);
/* the commits' status: the date and time (0x5cc5e), the position
 * (0x5cec6), the DOP and its type and the channels (0x5cf20) */
CHECK_FIELD(gps_state, status, 0x101002 - 0x100f2c);
CHECK_FIELD(gps_state, status.time, 0x101006 - 0x100f2c);
CHECK_FIELD(gps_state, status.fraction, 0x10100a - 0x100f2c);
CHECK_FIELD(gps_state, status.latitude, 0x101002 + 0x0c - 0x100f2c);
CHECK_FIELD(gps_state, status.height_msl, 0x10101a - 0x100f2c);
CHECK_FIELD(gps_state, status.dop, 0x101022 - 0x100f2c);
CHECK_FIELD(gps_state, status.dop_type, 0x101024 - 0x100f2c);
CHECK_FIELD(gps_state, status.tracked_count, 0x101027 - 0x100f2c);
CHECK_FIELD(gps_state, status.channels, 0x101002 + 0x26 - 0x100f2c);
CHECK_FIELD(gps_state, status.receiver_status, 0x101048 - 0x100f2c);
CHECK_FIELD(gps_state, visibility_received, 0x10104a - 0x100f2c);
CHECK_FIELD(gps_state, visibility, 0x10104c - 0x100f2c);
CHECK_FIELD(gps_state, visibility.satellites, 0x10104e - 0x100f2c);
CHECK_FIELD(gps_state, raim_satellites, 0x1010c6 - 0x100f2c);
/* gps_print_best_combinations' type, count and mode (0x53906 on) */
CHECK_FIELD(gps_state, xdop_table, 0x1010cb - 0x100f2c);
CHECK_FIELD(gps_state, xdop_table.combinations, 0x1010ce - 0x100f2c);
CHECK_FIELD(gps_state, corrections, 0x1011e6 - 0x100f2c);
CHECK_FIELD(gps_state, correction_average, 0x101226 - 0x100f2c);
CHECK_FIELD(gps_state, correction_deviation, 0x10122e - 0x100f2c);
/* the time RAIM record from gps_print_time_raim's base (0x53be4) */
CHECK_FIELD(gps_state, raim, 0x101238 - 0x100f2c);
CHECK_FIELD(gps_state, raim.raim_on, 0x101239 - 0x100f2c);
CHECK_FIELD(gps_state, raim.alarm_limit, 0x10123a - 0x100f2c);
CHECK_FIELD(gps_state, raim.pps_mode, 0x10123e - 0x100f2c);
CHECK_FIELD(gps_state, raim.pulse_sync, 0x10124d - 0x100f2c);
CHECK_FIELD(gps_state, raim.solution_status, 0x10124e - 0x100f2c);
CHECK_FIELD(gps_state, raim.raim_status, 0x10124f - 0x100f2c);
CHECK_FIELD(gps_state, raim.sigma, 0x101250 - 0x100f2c);
CHECK_FIELD(gps_state, raim.sawtooth, 0x101252 - 0x100f2c);
CHECK_FIELD(gps_state, raim_received, 0x101284 - 0x100f2c);
CHECK_FIELD(gps_state, leap_second_pending, 0x101286 - 0x100f2c);
CHECK_FIELD(gps_state, self_test, 0x101288 - 0x100f2c);
/* gps_print_extension's first and last fields (0x539e2 on) */
CHECK_FIELD(gps_state, extension, 0x101290 - 0x100f2c);
CHECK_FIELD(gps_state, extension.oscillator_offset, 0x1012ce - 0x100f2c);
CHECK_FIELD(gps_state, fix_mode, 0x10133a - 0x100f2c);
CHECK_FIELD(gps_state, receiver_id_received, 0x10133c - 0x100f2c);
CHECK_FIELD(gps_state, receiver_id, 0x10133d - 0x100f2c);
/* the tst commit's lines (0x5d298) */
CHECK_FIELD(gps_state, receiver_id.software_part_number, 0x10135f - 0x100f2c);
CHECK_FIELD(gps_state, receiver_id.model_number, 0x1013cb - 0x100f2c);
CHECK_FIELD(gps_state, receiver_id.options, 0x101437 - 0x100f2c);
/* each sentence's fields, where its table entry points */
CHECK_FIELD(gps_state, gga, 0x101454 - 0x100f2c);
CHECK_FIELD(gps_state, gga.latitude, 0x101456 - 0x100f2c);
CHECK_FIELD(gps_state, gga.longitude_hemisphere, 0x10145a - 0x100f2c);
CHECK_FIELD(gps_state, gga.longitude, 0x10145c - 0x100f2c);
CHECK_FIELD(gps_state, gga.height, 0x101460 - 0x100f2c);
CHECK_FIELD(gps_state, gga.height_offset, 0x101464 - 0x100f2c);
CHECK_FIELD(gps_state, gga.satellites, 0x101468 - 0x100f2c);
CHECK_FIELD(gps_state, gga.angle_decoded, 0x101469 - 0x100f2c);
CHECK_FIELD(gps_state, gsa, 0x10146a - 0x100f2c);
CHECK_FIELD(gps_state, gsa.satellites, 0x10146c - 0x100f2c);
CHECK_FIELD(gps_state, gsa.pdop, 0x101470 - 0x100f2c);
CHECK_FIELD(gps_state, gsa.hdop, 0x101472 - 0x100f2c);
CHECK_FIELD(gps_state, gsv, 0x101474 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.sentence, 0x101475 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.satellites, 0x101476 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.incoming, 0x1014a6 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.incoming[0].elevation, 0x1014a7 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.incoming[0].azimuth, 0x1014a8 - 0x100f2c);
CHECK_FIELD(gps_state, gsv.incoming[0].snr, 0x1014aa - 0x100f2c);
CHECK_FIELD(gps_state, gsv.incoming[1], 0x1014ac - 0x100f2c);
CHECK_FIELD(gps_state, anc, 0x1014be - 0x100f2c);
CHECK_FIELD(gps_state, anc.time, 0x1014c2 - 0x100f2c);
CHECK_FIELD(gps_state, anc.satellites, 0x1014c6 - 0x100f2c);
CHECK_FIELD(gps_state, ssd, 0x1014ca - 0x100f2c);
CHECK_FIELD(gps_state, ssd.field_z, 0x1014cb - 0x100f2c);
CHECK_FIELD(gps_state, ssd.ignored_satellites, 0x1014cc - 0x100f2c);
CHECK_FIELD(gps_state, ssd.height, 0x1014d0 - 0x100f2c);
CHECK_FIELD(gps_state, ssd.cable_delay, 0x1014d4 - 0x100f2c);
CHECK_FIELD(gps_state, tst, 0x1014d8 - 0x100f2c);
CHECK_FIELD(gps_state, tst.field_01, 0x1014d9 - 0x100f2c);
CHECK_FIELD(gps_state, tst.self_test_bits, 0x1014da - 0x100f2c);
CHECK_FIELD(gps_state, tst.software_part_number, 0x1014dc - 0x100f2c);
CHECK_FIELD(gps_state, tst.software_version, 0x1014e0 - 0x100f2c);
CHECK_FIELD(gps_state, tps, 0x1014e2 - 0x100f2c);
CHECK_FIELD(gps_state, tps.time_mode, 0x1014e3 - 0x100f2c);
CHECK_FIELD(gps_state, tps.date, 0x1014e4 - 0x100f2c);
CHECK_FIELD(gps_state, tps.time, 0x1014e8 - 0x100f2c);
CHECK_FIELD(gps_state, tps.week, 0x1014ec - 0x100f2c);
CHECK_FIELD(gps_state, tps.seconds, 0x1014ee - 0x100f2c);
CHECK_FIELD(gps_state, tps.utc_offset, 0x1014f2 - 0x100f2c);
CHECK_FIELD(gps_state, tps.leap_second, 0x1014f3 - 0x100f2c);
CHECK_FIELD(gps_state, tps.leap_date, 0x1014f4 - 0x100f2c);
CHECK_FIELD(gps_state, tps.leap_time, 0x1014f8 - 0x100f2c);
CHECK_FIELD(gps_state, tps.date_9, 0x1014fc - 0x100f2c);
CHECK_FIELD(gps_state, tps.time_9, 0x101500 - 0x100f2c);
CHECK_FIELD(gps_state, tps.position_hold, 0x101504 - 0x100f2c);
CHECK_FIELD(gps_state, tps.gps_date, 0x101506 - 0x100f2c);
CHECK_FIELD(gps_state, tps.gps_time, 0x10150a - 0x100f2c);
CHECK_FIELD(gps_state, rrm, 0x10150e - 0x100f2c);
CHECK_FIELD(gps_state, rrm.raim_status, 0x10150f - 0x100f2c);
CHECK_FIELD(gps_state, rrm.satellites, 0x101510 - 0x100f2c);
CHECK_FIELD(gps_state, rrm.sawtooth, 0x101514 - 0x100f2c);
CHECK_FIELD(gps_state, rsd, 0x101516 - 0x100f2c);
CHECK_FIELD(gps_state, rsd.pps_mode, 0x101517 - 0x100f2c);
CHECK_FIELD(gps_state, rsd.alarm_limit, 0x101518 - 0x100f2c);
/* gps_init clears +0x5ee (0x57c22) */
CHECK_FIELD(gps_state, field_5ee, 0x10151a - 0x100f2c);
CHECK_FIELD(gps_state, average_count, 0x10151c - 0x100f2c);
CHECK_FIELD(gps_state, average_latitude, 0x101520 - 0x100f2c);
CHECK_FIELD(gps_state, average_longitude, 0x101528 - 0x100f2c);
CHECK_FIELD(gps_state, average_height_gps, 0x101530 - 0x100f2c);
CHECK_FIELD(gps_state, average_height_msl, 0x101538 - 0x100f2c);
CHECK_FIELD(gps_state, references, 0x101540 - 0x100f2c);
CHECK_FIELD(gps_state, references[1], 0x101556 - 0x100f2c);
CHECK_FIELD(gps_state, surveying, 0x10156c - 0x100f2c);
CHECK_FIELD(gps_state, field_641, 0x10156d - 0x100f2c);
CHECK_FIELD(gps_state, raim_assessment, 0x10156e - 0x100f2c);
CHECK_FIELD(gps_state, pps_check_disabled, 0x10156f - 0x100f2c);
CHECK_FIELD(gps_state, receiver_health_failed, 0x101570 - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x101572 - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_state, 0x101573 - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_query, 0x101574 - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_time, 0x101578 - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_error, 0x10157c - 0x100f2c);
CHECK_FIELD(gps_state, command_monitor_failed, 0x10157d - 0x100f2c);
CHECK_FIELD(gps_state, status_monitor_enabled, 0x10157e - 0x100f2c);
CHECK_FIELD(gps_state, status_monitor_state, 0x10157f - 0x100f2c);
CHECK_FIELD(gps_state, status_last_time, 0x101584 - 0x100f2c);
CHECK_FIELD(gps_state, status_monitor_failed, 0x10158a - 0x100f2c);
CHECK_FIELD(gps_state, channel_count, 0x10158c - 0x100f2c);
CHECK_FIELD(gps_state, field_592, 0x10158e - 0x100f2c);
CHECK_FIELD(gps_state, tracking_log, 0x101590 - 0x100f2c);
CHECK_FIELD(gps_state, tracking_log_active, 0x101591 - 0x100f2c);
CHECK_FIELD(gps_state, track_words, 0x101592 - 0x100f2c);
CHECK_FIELD(gps_state, channel_word, 0x10159e - 0x100f2c);
CHECK_FIELD(gps_state, channel_word_lowest, 0x1015a2 - 0x100f2c);

/* the sentences' records, from their fields above */
CHECK_SIZE(nmea_gga_fields, 0x53e - 0x528);
CHECK_SIZE(nmea_gsa_fields, 0x548 - 0x53e);
CHECK_SIZE(nmea_gsv_satellite, 0x580 - 0x57a);
CHECK_SIZE(nmea_tps_fields, 0x5e2 - 0x5b6);
CHECK_SIZE(oncore_status_fields, 0x47);

CHECK_SIZE(oncore_visibility_fields, 0x7a);

CHECK_SIZE(oncore_xdop_table_fields, 75);

CHECK_SIZE(oncore_almanac_status_fields, 16);

CHECK_SIZE(oncore_corrections_fields, 64);

/* to raim_received (0x57456) */
CHECK_SIZE(oncore_raim_fields, 0x358 - 0x30c);

/* the two reference records, 0x56c5c's (0x101540, 0x101556) */

CHECK_SIZE(gps_reference_record, 0x101556 - 0x101540);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* 0x4b858 stores the GPS's at 0x1013a6 and 0x1013a8 */
CHECK_FIELD(gps_reference_record, hysteresis, 0x101552 - 0x101540);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x101554 - 0x101540);

/* The loop's state at 0x10286e, memset(&pll, 0, 0x3ee) by pll_init
 * (0x50b4c): each field where this image's code reaches it, through
 * pll's address or a base register loaded with it (make record-access);
 * the hold causes a word (diag_pr_hold_cause). */
CHECK_SIZE(pll_state, 0x102c5c - 0x10286e);
CHECK_SIZE(pll_hold_cause, 2);
CHECK_FIELD(pll_state, previous_stage, 0x10286f - 0x10286e);
CHECK_FIELD(pll_state, powerup_countdown, 0x102870 - 0x10286e);
CHECK_FIELD(pll_state, powerup_state, 0x102871 - 0x10286e);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x102872 - 0x10286e);
CHECK_FIELD(pll_state, reference_check_state, 0x102874 - 0x10286e);
CHECK_FIELD(pll_state, startup_state, 0x10287a - 0x10286e);
CHECK_FIELD(pll_state, ti0, 0x10287c - 0x10286e);
CHECK_FIELD(pll_state, ti1, 0x102880 - 0x10286e);
CHECK_FIELD(pll_state, ti2, 0x102884 - 0x10286e);
CHECK_FIELD(pll_state, n3_lock_state, 0x102888 - 0x10286e);
CHECK_FIELD(pll_state, normal_state, 0x10288a - 0x10286e);
CHECK_FIELD(pll_state, hold_causes, 0x10288c - 0x10286e);
CHECK_FIELD(pll_state, holdover_state, 0x10288e - 0x10286e);
CHECK_FIELD(pll_state, manual, 0x10288f - 0x10286e);
CHECK_FIELD(pll_state, ti_limit_override, 0x102890 - 0x10286e);
CHECK_FIELD(pll_state, recovery_countdown, 0x102892 - 0x10286e);
CHECK_FIELD(pll_state, recovery_state, 0x102893 - 0x10286e);
CHECK_FIELD(pll_state, coarse_state, 0x102894 - 0x10286e);
CHECK_FIELD(pll_state, fine_state, 0x102895 - 0x10286e);
CHECK_FIELD(pll_state, frequency_adjusted, 0x102896 - 0x10286e);
CHECK_FIELD(pll_state, fine_index, 0x102897 - 0x10286e);
CHECK_FIELD(pll_state, fine_retries, 0x102898 - 0x10286e);
CHECK_FIELD(pll_state, fine_d, 0x10289a - 0x10286e);
CHECK_FIELD(pll_state, aging_state, 0x10289e - 0x10286e);
CHECK_FIELD(pll_state, gps_diag_state, 0x1028a2 - 0x10286e);
CHECK_FIELD(pll_state, gps_diag_substate, 0x1028a3 - 0x10286e);
CHECK_FIELD(pll_state, gps_init_state, 0x1028a4 - 0x10286e);
CHECK_FIELD(pll_state, leap_check_timer, 0x1028a6 - 0x10286e);
CHECK_FIELD(pll_state, leap_check_requested, 0x1028a8 - 0x10286e);
CHECK_FIELD(pll_state, leap_check_state, 0x1028a9 - 0x10286e);
CHECK_FIELD(pll_state, leap_ack_state, 0x1028aa - 0x10286e);
CHECK_FIELD(pll_state, leap_ack_requested, 0x1028ab - 0x10286e);
CHECK_FIELD(pll_state, leap_ack_done, 0x1028ac - 0x10286e);
CHECK_FIELD(pll_state, leap_ack_again, 0x1028ae - 0x10286e);
CHECK_FIELD(pll_state, efc, 0x1028b0 - 0x10286e);
CHECK_FIELD(pll_state, tfom, 0x1028b4 - 0x10286e);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x1028b8 - 0x10286e);
CHECK_FIELD(pll_state, measured_uncertainty, 0x1028bc - 0x10286e);
CHECK_FIELD(pll_state, frequency_tolerance, 0x1028c8 - 0x10286e);
CHECK_FIELD(pll_state, efc_history, 0x1028cc - 0x10286e);
CHECK_FIELD(pll_state, aging_term, 0x102c20 - 0x10286e);
CHECK_FIELD(pll_state, loop_efc_step, 0x102c24 - 0x10286e);
CHECK_FIELD(pll_state, ti_filtered, 0x102c28 - 0x10286e);
CHECK_FIELD(pll_state, efc_base, 0x102c2c - 0x10286e);
CHECK_FIELD(pll_state, loop_time_constant, 0x102c30 - 0x10286e);
CHECK_FIELD(pll_state, led_gps_lock, 0x102c3c - 0x10286e);
CHECK_FIELD(pll_state, led_holdover, 0x102c3d - 0x10286e);
CHECK_FIELD(pll_state, holdover_duration, 0x102c3e - 0x10286e);
CHECK_FIELD(pll_state, holdover_start_uptime, 0x102c42 - 0x10286e);
CHECK_FIELD(pll_state, reference_uptime, 0x102c46 - 0x10286e);
CHECK_FIELD(pll_state, reference_time, 0x102c4a - 0x10286e);
CHECK_FIELD(pll_state, efc_control_passes, 0x102c4e - 0x10286e);
CHECK_FIELD(pll_state, reference_seconds, 0x102c52 - 0x10286e);
CHECK_FIELD(pll_state, fpga_configuration, 0x102c53 - 0x10286e);
CHECK_FIELD(pll_state, fpga_loaded, 0x102c54 - 0x10286e);
CHECK_FIELD(pll_state, loop_running, 0x102c55 - 0x10286e);
CHECK_FIELD(pll_state, sync_requested, 0x102c56 - 0x10286e);
CHECK_FIELD(pll_state, field_3e9, 0x102c57 - 0x10286e);
CHECK_FIELD(pll_state, external_reference, 0x102c58 - 0x10286e);
CHECK_FIELD(pll_state, field_3eb, 0x102c59 - 0x10286e);
CHECK_FIELD(pll_state, measured_uncertainty_valid, 0x102c5a - 0x10286e);

/* the history 0x354 bytes, at 0x1028cc: efc_history_update's (0x4a588)
 * and the fits' indexed arrays of 32, measured_uncertainty_update's fit arrays
 * (0x4a1a2 to 0x4a1ba) */
CHECK_SIZE(efc_ring, 0x102c20 - 0x1028cc);
CHECK_FIELD(efc_ring, stop_ptr, 0x1028cd - 0x1028cc);
CHECK_FIELD(efc_ring, loop_settled, 0x1028ce - 0x1028cc);
CHECK_FIELD(efc_ring, sample_count, 0x1028d0 - 0x1028cc);
CHECK_FIELD(efc_ring, summed_count, 0x1028d4 - 0x1028cc);
CHECK_FIELD(efc_ring, averages, 0x1028d6 - 0x1028cc);
CHECK_FIELD(efc_ring, replaced_average, 0x102956 - 0x1028cc);
CHECK_FIELD(efc_ring, weights, 0x10295a - 0x1028cc);
CHECK_FIELD(efc_ring, replaced_weight, 0x10297a - 0x1028cc);
CHECK_FIELD(efc_ring, times, 0x10297c - 0x1028cc);
CHECK_FIELD(efc_ring, replaced_time, 0x1029fc - 0x1028cc);
CHECK_FIELD(efc_ring, logs, 0x102a00 - 0x1028cc);
CHECK_FIELD(efc_ring, fit_a_history, 0x102a80 - 0x1028cc);
CHECK_FIELD(efc_ring, fit_b_history, 0x102b00 - 0x1028cc);
CHECK_FIELD(efc_ring, fit_c_history, 0x102b80 - 0x1028cc);
CHECK_FIELD(efc_ring, next_entry_uptime, 0x102c00 - 0x1028cc);
CHECK_FIELD(efc_ring, efc_sum, 0x102c04 - 0x1028cc);
CHECK_FIELD(efc_ring, drift, 0x102c0c - 0x1028cc);
CHECK_FIELD(efc_ring, fit_a, 0x102c14 - 0x1028cc);
CHECK_FIELD(efc_ring, fit_b, 0x102c18 - 0x1028cc);
CHECK_FIELD(efc_ring, fit_c, 0x102c1c - 0x1028cc);

/* Records the printf calls name; in the 58503A 3704 @@Cj's decoder's
 * table (0x52620) lays the receiver ID out as the Z3805A 3543B's: 34
 * bytes, then 9 lines of 27. */

/* The receiver ID's lines (0x5262c): 12 bytes each, the table ending
 * at 0x52698, its tenth; the model number's and the options' texts at
 * 0x1012e7 and 0x101353, in gps.receiver_id from 0x101259. */

CHECK_SIZE(gps_statistics, 0x3c);
CHECK_FIELD(gps_statistics, field_0e, 0x10162a - 0x10161c);
CHECK_FIELD(gps_statistics, responses_read, 0x10163e - 0x10161c);
CHECK_FIELD(gps_statistics, e_retries, 0x101642 - 0x10161c);
CHECK_FIELD(gps_statistics, t_retries, 0x101644 - 0x10161c);
CHECK_FIELD(gps_statistics, raim_detected, 0x101646 - 0x10161c);
CHECK_FIELD(gps_statistics, raim_alarms, 0x101648 - 0x10161c);
CHECK_FIELD(gps_statistics, unknown_response, 0x10164a - 0x10161c);
CHECK_FIELD(gps_statistics, frame_errors, 0x10164c - 0x10161c);
CHECK_FIELD(gps_statistics, checksum_errors, 0x10164e - 0x10161c);
CHECK_FIELD(gps_statistics, address_errors, 0x101650 - 0x10161c);
CHECK_FIELD(gps_statistics, data_errors, 0x101652 - 0x10161c);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x101654 - 0x10161c);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x101656 - 0x10161c);

/* The NMEA framer's record at gps_response_buffer (0x1015b0): the
 * cursor gps_response_cursor (0x101602), the framer's counts at +0x56 and
 * +0x57 and its field offsets from +0x58 (0x5d6dc, 0x5d71a). */
CHECK_FIELD(nmea_frame, cursor, 0x101602 - 0x1015b0);
CHECK_FIELD(nmea_frame, fields, 0x56);
CHECK_FIELD(nmea_frame, checksum_digits, 0x57);
CHECK_FIELD(nmea_frame, field_starts, 0x58);
CHECK_SIZE(nmea_frame, 0x10161c - 0x1015b0);
CHECK_SIZE(exception_record, 0x100c66 - 0x100c54);
CHECK_FIELD(exception_record, name, 0x100c55 - 0x100c54);
CHECK_FIELD(exception_record, sr, 0x100c5a - 0x100c54);
CHECK_FIELD(exception_record, pc, 0x100c5c - 0x100c54);
CHECK_FIELD(exception_record, vector, 0x100c60 - 0x100c54);
CHECK_FIELD(exception_record, address, 0x100c62 - 0x100c54);
CHECK_FIELD(exception_record, pending, 0x100c54 - 0x100c54);

/* The pool: five entries from 0x10372a to 0x1037ac. */
CHECK_SIZE(klok_entry, (0x1037ac - 0x10372a) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read's page, log_page_buffer at 0x102452: entry n's fields at
 * 0x102456, 0x10245a, 0x10245e, ... + n * 0x14. */
CHECK_SIZE(log_page, 0x102492 - 0x102452);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x102456 - 0x102452);
CHECK_FIELD(log_entry, format, 0x10245e - 0x102456);
CHECK_FIELD(log_entry, argument_1, 0x102462 - 0x102456);
CHECK_SIZE(fpga_bit, 4);
/* interpolator_calibrate reads and writes param[0] to param[8]. */
CHECK_SIZE(interpolator_calibration, 9);
CHECK_FIELD(interpolator_calibration, result_valid, 4);
CHECK_FIELD(interpolator_calibration, reading_high, 8);
CHECK_FIELD(interpolator_calibration, calibrated, 0x1026cb - 0x1026ca);
CHECK_FIELD(interpolator_calibration, reading_low, 0x1026d1 - 0x1026ca);

/* The packed enums are bytes, as the fields they type. */
CHECK_SIZE(pll_stage, 1);
CHECK_SIZE(adc_channel, 1);
CHECK_SIZE(oncore_channel_mode, 1);
CHECK_SIZE(oncore_raim_status, 1);

/* nv_settings at 0x102570, 0xcf bytes, nv_restore's copy of its
 * defaults (0x46f58 to 0x46f72): each field where its descriptor's
 * value points, in nv_save's tables (nv_numeric_params, 0x474c4;
 * nv_setting_params, 0x47524); then the two descriptors' sizes. */
CHECK_SIZE(nv_settings_fields, 0x10263f - 0x102570);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x102572 - 0x102570);
CHECK_FIELD(nv_settings_fields, antenna_delay, 0x102576 - 0x102570);
CHECK_FIELD(nv_settings_fields, douglas_duration, 0x10257a - 0x102570);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x10257e - 0x102570);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x10257f - 0x102570);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x102580 - 0x102570);
CHECK_FIELD(nv_settings_fields, external_questionable_hysteresis,
            0x102584 - 0x102570);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x102588 - 0x102570);
CHECK_FIELD(nv_settings_fields, active_holdover_threshold, 0x10258c - 0x102570);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x102590 - 0x102570);
CHECK_FIELD(nv_settings_fields, holdover_limit_threshold, 0x102594 - 0x102570);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x102598 - 0x102570);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x102599 - 0x102570);
CHECK_FIELD(nv_settings_fields, position, 0x10259a - 0x102570);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x1025a6 - 0x102570);
CHECK_FIELD(nv_settings_fields, time_zone, 0x1025aa - 0x102570);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x1025b2 - 0x102570);
CHECK_FIELD(nv_settings_fields, leap_date, 0x1025b6 - 0x102570);
CHECK_FIELD(nv_settings_fields, leap_minute_length, 0x1025ba - 0x102570);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x1025be - 0x102570);
CHECK_FIELD(nv_settings_fields, raim_state, 0x1025bf - 0x102570);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x1025c2 - 0x102570);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x1025ca - 0x102570);
CHECK_FIELD(nv_settings_fields, status_enable, 0x1025ce - 0x102570);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x1025ea - 0x102570);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x1025fe - 0x102570);
CHECK_FIELD(nv_settings_fields, log_wrap, 0x102612 - 0x102570);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x102613 - 0x102570);
CHECK_FIELD(nv_settings_fields, serial_restricted, 0x102614 - 0x102570);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x102615 - 0x102570);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x102616 - 0x102570);
CHECK_FIELD(nv_settings_fields, pulse_start_time, 0x10261a - 0x102570);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x102626 - 0x102570);
CHECK_FIELD(nv_settings_fields, field_c2, 0x102632 - 0x102570);
CHECK_FIELD(nv_settings_fields, output_state, 0x102636 - 0x102570);
CHECK_FIELD(nv_settings_fields, format_data, 0x102637 - 0x102570);
CHECK_FIELD(nv_settings_fields, memory_save, 0x102638 - 0x102570);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x102639 - 0x102570);
CHECK_FIELD(nv_settings_fields, douglas_state_powerup, 0x10263a - 0x102570);
CHECK_FIELD(nv_settings_fields, tcode_format, 0x10263b - 0x102570);
CHECK_FIELD(nv_settings_fields, gps_utc, 0x10263c - 0x102570);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x10263d - 0x102570);
CHECK_FIELD(nv_settings_fields, debug_output, 0x10263e - 0x102570);

/* each is followed by the next descriptor's widths */
CHECK_SIZE(scpi_setting_param, 0x33ca8 - 0x33c8a - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* the group cells, from the descriptors' pointers up to interval_reading */
CHECK_SIZE(scpi_group_cells_fields, 0x10268c - 0x102680);
CHECK_FIELD(scpi_group_cells_fields, gps_initial, 0x102682 - 0x102680);
CHECK_FIELD(scpi_group_cells_fields, pulse, 0x102684 - 0x102680);
CHECK_FIELD(scpi_group_cells_fields, serial_2, 0x102686 - 0x102680);
CHECK_FIELD(scpi_group_cells_fields, status, 0x102688 - 0x102680);
CHECK_FIELD(scpi_group_cells_fields, rest, 0x10268a - 0x102680);
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
CHECK_SIZE(oncore_message_entry, (0x5e9cc - 0x5dcac) / 56);
CHECK_FIELD(oncore_message_entry, filter, 0x38);
CHECK_FIELD(oncore_message_entry, kind, 0x4fed2 - 0x4fece);
CHECK_FIELD(oncore_message_entry, encoders, 0x4fed4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, query, 0x4fee4 - 0x4fece);
CHECK_FIELD(oncore_message_entry, timeout, 0x4fee8 - 0x4fece);
CHECK_FIELD(oncore_message_entry, length, 0x4feea - 0x4fece);
CHECK_FIELD(oncore_message_entry, match_kind, 0x4feec - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders, 0x4feee - 0x4fece);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x4fef2 - 0x4fece);

/* The time-interval counter's two records: the reading at 0x10268c, its
 * flag at 0x102694 and armed flag at 0x102698; the configuration at
 * 0x1026a2, ending where interpolator_calibration_state begins
 * (0x1026ca), each field where counter_configuration_init (0x49aba to
 * 0x49af0), counter_load_registers (0x49a56), counter_calibrate (0x49118
 * to 0x49194), counter_read (0x4992a, 0x49938) and counter_arm (0x49862
 * to 0x4989c) reach it. */
CHECK_SIZE(interval_reading_fields, 0x102699 - 0x10268c);
CHECK_FIELD(interval_reading_fields, valid, 0x102694 - 0x10268c);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x102698 - 0x10268c);
CHECK_FIELD(interval_reading_fields, value, 0x10268c - 0x10268c);

CHECK_SIZE(counter_configuration_fields, 0x1026ca - 0x1026a2);
CHECK_FIELD(counter_configuration_fields, fpga_registers, 0x1026a2 - 0x1026a2);
CHECK_FIELD(counter_configuration_fields, calibration, 8);
CHECK_FIELD(counter_configuration_fields, registers_mode, 16);
CHECK_FIELD(counter_configuration_fields, registers_state, 0x1026b3 - 0x1026a2);
CHECK_FIELD(counter_configuration_fields, calibrate_mode, 18);
CHECK_FIELD(counter_configuration_fields, calibrate_state, 19);
CHECK_FIELD(counter_configuration_fields, sync_mode, 20);
CHECK_FIELD(counter_configuration_fields, sync_state, 21);
CHECK_FIELD(counter_configuration_fields, sync_kind, 22);
CHECK_FIELD(counter_configuration_fields, sync_count, 24);
CHECK_FIELD(counter_configuration_fields, interpolator_offset, 28);
CHECK_FIELD(counter_configuration_fields, interpolator_scale, 32);
CHECK_FIELD(counter_configuration_fields, start_source_mode, 36);
CHECK_FIELD(counter_configuration_fields, start_source_state, 37);
CHECK_FIELD(counter_configuration_fields, start_source, 38);

/* The status snapshots, 0x176 bytes each: status_screen_print's two
 * (0x56b0c) and status_snapshot_update's latest and flagged (0x58aee,
 * 0x58b4a); each field where status_snapshot (0x55d1e to 0x56098)
 * stores it through A2, the record, and its readers read it. */
CHECK_SIZE(status_snapshot_record, 0x10193c - 0x1017c6);
CHECK_SIZE(status_snapshot_record, 0x101c2e - 0x101ab8);
CHECK_FIELD(status_snapshot_record, field_000, 0x0);
CHECK_FIELD(status_snapshot_record, redundancy_code, 0x1);
CHECK_FIELD(status_snapshot_record, stage, 0x2);
CHECK_FIELD(status_snapshot_record, mode, 0x3);
CHECK_FIELD(status_snapshot_record, powerup_suffix, 0x4);
CHECK_FIELD(status_snapshot_record, recovery_suffix, 0x5);
CHECK_FIELD(status_snapshot_record, tfom, 0x6);
CHECK_FIELD(status_snapshot_record, ffom, 0xa);
CHECK_FIELD(status_snapshot_record, ffom_valid, 0xb);
CHECK_FIELD(status_snapshot_record, interval, 0xc);
CHECK_FIELD(status_snapshot_record, interval_valid, 0x14);
CHECK_FIELD(status_snapshot_record, interval_errors, 0x15);
CHECK_FIELD(status_snapshot_record, interval_tag, 0x16);
CHECK_FIELD(status_snapshot_record, interval_mean, 0x18);
CHECK_FIELD(status_snapshot_record, interval_mean_flag, 0x1c);
CHECK_FIELD(status_snapshot_record, holdover_waiting_reason, 0x1e);
CHECK_FIELD(status_snapshot_record, holdover_limit_state, 0x1f);
CHECK_FIELD(status_snapshot_record, holdover_limit_threshold, 0x20);
CHECK_FIELD(status_snapshot_record, holdover_duration, 0x24);
CHECK_FIELD(status_snapshot_record, kenneth_present, 0x28);
CHECK_FIELD(status_snapshot_record, kenneth_present_valid, 0x2c);
CHECK_FIELD(status_snapshot_record, kenneth_present_resolution, 0x2d);
CHECK_FIELD(status_snapshot_record, kenneth_predicted, 0x2e);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_valid, 0x32);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_resolution, 0x33);
CHECK_FIELD(status_snapshot_record, channel_count, 0x34);
CHECK_FIELD(status_snapshot_record, satellites, 0x36);
CHECK_FIELD(status_snapshot_record, not_tracked, 0x60);
CHECK_FIELD(status_snapshot_record, tracked, 0xe0);
CHECK_FIELD(status_snapshot_record, status_received, 0x120);
CHECK_FIELD(status_snapshot_record, not_tracked_count, 0x121);
CHECK_FIELD(status_snapshot_record, tracked_count, 0x122);
CHECK_FIELD(status_snapshot_record, external_reference, 0x123);
CHECK_FIELD(status_snapshot_record, reference_valid, 0x124);
CHECK_FIELD(status_snapshot_record, pps_dead, 0x125);
CHECK_FIELD(status_snapshot_record, status_code, 0x126);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 0x128);
CHECK_FIELD(status_snapshot_record, external_valid, 0x12c);
CHECK_FIELD(status_snapshot_record, external_pps_dead, 0x12d);
CHECK_FIELD(status_snapshot_record, external_status_code, 0x12e);
CHECK_FIELD(status_snapshot_record, external_elapsed, 0x130);
CHECK_FIELD(status_snapshot_record, field_592, 0x134);
CHECK_FIELD(status_snapshot_record, fix_mode, 0x136);
CHECK_FIELD(status_snapshot_record, receiver_status, 0x137);
CHECK_FIELD(status_snapshot_record, time, 0x138);
CHECK_FIELD(status_snapshot_record, date, 0x13c);
CHECK_FIELD(status_snapshot_record, time_zone, 0x140);
CHECK_FIELD(status_snapshot_record, from_clock, 0x148);
CHECK_FIELD(status_snapshot_record, utc_time, 0x149);
CHECK_FIELD(status_snapshot_record, tps_time_mode, 0x14a);
CHECK_FIELD(status_snapshot_record, leap_second_pending, 0x14b);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 0x14c);
CHECK_FIELD(status_snapshot_record, mask_angle, 0x150);
CHECK_FIELD(status_snapshot_record, position_hold, 0x151);
CHECK_FIELD(status_snapshot_record, douglas_duration_infinite, 0x152);
CHECK_FIELD(status_snapshot_record, position_source, 0x154);
CHECK_FIELD(status_snapshot_record, position, 0x158);
CHECK_FIELD(status_snapshot_record, height_type, 0x164);
CHECK_FIELD(status_snapshot_record, average_count, 0x166);
CHECK_FIELD(status_snapshot_record, douglas_duration, 0x16a);
CHECK_FIELD(status_snapshot_record, hardware_condition, 0x16e);
CHECK_FIELD(status_snapshot_record, alarms_upper, 0x170);
CHECK_FIELD(status_snapshot_record, alarms, 0x172);
CHECK_FIELD(status_snapshot_record, alarm_clock_deviation, 0x174);
/* status_screen_card_lines, six entries of 6 bytes from 0x59716 */
CHECK_SIZE(status_screen_card_line[6], 0x5973a - 0x59716);
CHECK_FIELD(status_screen_card_line, text, 0x5971e - 0x5971c);
CHECK_SIZE(satellite_list_entry, 8);
CHECK_SIZE(satellite_sets, 0x60 - 0x36);

/* The preserved region at 0x100000, the 0xc04 bytes record_checksum
 * sums (system_init, 0x22dd0): gps's copy at 0x100004, pll's at
 * 0x10067c, adc_monitors' at 0x100a6a and retained_block's 0x19 bytes at
 * 0x100bea (0x22e06 to 0x22e48). */
CHECK_SIZE(preserved_region_fields, 0xc04);
CHECK_FIELD(preserved_region_fields, gps, 0x100004 - 0x100000);
CHECK_FIELD(preserved_region_fields, pll, 0x10067c - 0x100000);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0x100a6a - 0x100000);
CHECK_FIELD(preserved_region_fields, values, 0x100bea - 0x100000);

/* the 0x180 bytes pllp_task and system_init copy from 0x101e78: the
 * ADC monitors, which monitor_average_add indexes by 0x30 (0x21d40) */

/* The clock record at 0x100c6e: the routines' offsets, at the address
 * the derivation from the 58503A's code gives (layout_addresses.py). */
CHECK_SIZE(clock_record, 0x100ca1 - 0x100c6e);
CHECK_FIELD(clock_record, uptime, 0x100c70 - 0x100c6e);
CHECK_FIELD(clock_record, time, 0x100c74 - 0x100c6e);
CHECK_FIELD(clock_record, date, 0x100c78 - 0x100c6e);
CHECK_FIELD(clock_record, month_days, 0x100c7c - 0x100c6e);
CHECK_FIELD(clock_record, gps_time, 0x100c88 - 0x100c6e);
CHECK_FIELD(clock_record, gps_date, 0x100c8c - 0x100c6e);
CHECK_FIELD(clock_record, gps_month_days, 0x100c90 - 0x100c6e);
CHECK_FIELD(clock_record, minute_length, 0x100c9c - 0x100c6e);
CHECK_FIELD(clock_record, set, 0x100c9d - 0x100c6e);
CHECK_FIELD(clock_record, gps_utc_offset, 0x100c9e - 0x100c6e);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x100ca0 - 0x100c6e);

/* pps_message at 0x100e64, pps_exchange after the cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x100e7c - 0x100e64);
CHECK_FIELD(pps_exchange_message, gps_time, 0x100e6c - 0x100e64);
CHECK_FIELD(pps_exchange_message, gps_date, 0x100e70 - 0x100e64);

CHECK_SIZE(gps_process_state, 1);

/* the 58503A 3704's three status snapshots, 0x1016ae (status_screen_print,
 * 0x4b6ec), 0x10181e and 0x101988, with the two cells at 0x101818
 * between the first two; each field where status_snapshot (0x4ac56)
 * stores it */

/* status_screen_mode_lines' five entries, the last a terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x4d050 - 0x4d032);
CHECK_FIELD(status_screen_mode_line, text, 0x02);

/* the event log's write page at 0x10240c, its number after it */
CHECK_SIZE(log_page, 0x10244c - 0x10240c);

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

/* 0x29740's frame: link -6, the cursor's text at -6 and severity at -2 */
CHECK_SIZE(alarm_text_cursor, 6);
CHECK_FIELD(alarm_text_cursor, severity, -2 - -6);
/* the tree's root at 0x29b42, its entries' table at 0x29ac2 to the
 * names at 0x29b1c, nine 10-byte entries, the last empty */

CHECK_FIELD(alarm_text_node, entries, 0x3d206 - 0x3d204);

/* port_error_reports at 0x102e48, as scpi_session_init fills it:
 * the masks at +0, +4, +8 and +0xc, the conditions at +2 to +0xe
 * (0x28dfc to 0x28e24) */
CHECK_SIZE(port_error_report, 4);
CHECK_FIELD(port_error_report, condition, 2);

/* gps_sentence_descriptors, codes 0x39 to 0x4c from 0x5e3cc to 0x5e64c,
 * gps_request_descriptor's 0x20 a code (0x5c11c); set,M's address, its
 * encoder, its query and its timeout, match kind and test where
 * gps_request_build (0x5d954 to 0x5da5e), gps_response_timed_out
 * (0x575c6) and gpsm_task (0x58232) read them. */
CHECK_SIZE(gps_sentence_descriptor[20], 0x5e64c - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, kind, 0x5e3d0 - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, encoders, 0x5e3d2 - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, query, 0x5e3e2 - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, timeout, 0x5e3e6 - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, match_kind, 0x5e3e7 - 0x5e3cc);
CHECK_FIELD(gps_sentence_descriptor, test, 0x5e3e8 - 0x5e3cc);
