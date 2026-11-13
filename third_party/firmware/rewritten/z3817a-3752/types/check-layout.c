/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in an image it is written as their
 * difference.  This is the Z3817A 3752's copy of the Z3815A 4010's, its
 * base (docs/firmware/z3817a-3752.md): the records the Z3817A's types
 * lay otherwise are checked at the Z3817A's addresses; the rest are as
 * they stood when the Z3817A was seeded.  A block's addresses are the
 * image's its comment names, the Z3817A's where it names none, to the
 * next blank line; a check under no comment may give any image's.  Compiled,
 * never run, by "make check-types" with an m68k compiler, so that int, long and
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

CHECK_SIZE(holdover_steering_state, 0x1c);
CHECK_FIELD(holdover_steering_state, aging_limit, 0x102580 - 0x102578);
CHECK_FIELD(holdover_steering_state, ti_sum, 0x102584 - 0x102578);
CHECK_FIELD(holdover_steering_state, ti_count, 0x102590 - 0x102578);
CHECK_FIELD(holdover_steering_state, ti_largest, 0x10258c - 0x102578);
CHECK_FIELD(holdover_steering_state, ti_weighted_sum, 0x102588 - 0x102578);

CHECK_SIZE(loop_steering_state, 0x1025ac - 0x102590);
CHECK_FIELD(loop_steering_state, seconds, 0x10259c - 0x102590);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x1025a0 - 0x102590);
CHECK_FIELD(loop_steering_state, filter_weight, 0x1025a8 - 0x102590);

CHECK_SIZE(serial_ring, 0x1036e9 - 0x1035e7);
CHECK_SIZE(serial_ring, 0x1037f7 - 0x1036f5);
CHECK_SIZE(serial_ring, 0x10b006 - 0x10af04);
CHECK_FIELD(serial_ring, data, 0x1035e9 - 0x1035e7);
CHECK_FIELD(serial_ring, read, 0x1035e8 - 0x1035e7);
CHECK_FIELD(serial_ring, write, 0x1035e7 - 0x1035e7);

/* duarta at 0x1036ea, up to duartb_receive_errors at 0x1036f4 (cleared at
 * 0x2df90): its size a number, as layout_addresses.py's references map the
 * Z3815A 4010's duartb_receive_errors to sci.receive_errors (0x103956). */
CHECK_SIZE(duarta_driver, 10);
CHECK_FIELD(duarta_driver, overrun, 0x1036f0 - 0x1036ea);
CHECK_FIELD(duarta_driver, least_free, 0x1036f3 - 0x1036ea);
CHECK_FIELD(duarta_driver, held, 0x1036f2 - 0x1036ea);
CHECK_FIELD(duarta_driver, transmit_count, 0x1036ee - 0x1036ea);
CHECK_FIELD(duarta_driver, transmit_next, 0x1036ea - 0x1036ea);

/* In the Z3815A 4010, the SCI's settings at 0x102640, as sci_reconfigure
 * reads them (0x30730 on), where this image's are at 0x10249e; and the
 * installer's at 0x10acb0, in both, as
 * installer_parameter_record_read loads them (installer::0x108398 on). */
CHECK_SIZE(sci_settings_record, 0x1024a4 - 0x10249e);
CHECK_FIELD(sci_settings_record, baud, 0x1024a0 - 0x10249e);
CHECK_FIELD(sci_settings_record, parity, 0x1024a1 - 0x10249e);
CHECK_FIELD(sci_settings_record, xon_xoff, 0x1024a2 - 0x10249e);
CHECK_FIELD(sci_settings_record, echo, 0x1024a3 - 0x10249e);
CHECK_FIELD(sci_settings_record, baud, 0x10acb2 - 0x10acb0);
CHECK_FIELD(sci_settings_record, echo, 0x10acb5 - 0x10acb0);
/* DUART channel B's settings at 0x1024a6: the only code that reaches
 * them, the restricted-serial check (0x41358 to 0x4138c), tests parity
 * at 0x1024a9 and eight_bits at 0x1024ac; parameter_record_read, the
 * SER1 setting descriptors and duartb_settings_preset address
 * sci_settings instead, so that layout_addresses.py's references map the
 * Z3815A 4010's duartb_settings there, and the offset is a number.  The
 * installer's applied settings at 0x10aecc; the primary has none. */
CHECK_FIELD(duartb_settings_record, eight_bits, 6);
CHECK_SIZE(duartb_applied_settings, 0x10aed0 - 0x10aecc);
CHECK_FIELD(duartb_applied_settings, baud, 0x10aecc - 0x10aecc);
CHECK_SIZE(duart_shadow_registers, 0x1035e7 - 0x1035da);
CHECK_FIELD(duart_shadow_registers, imr, 0x1035de - 0x1035da);
CHECK_FIELD(duart_shadow_registers, mr1b, 0x1035e1 - 0x1035da);
CHECK_FIELD(duart_shadow_registers, output_reset, 0x1035e5 - 0x1035da);

CHECK_SIZE(exchange_token, 16);
/* In the Z3815A 4010, the SCI's exchanges at 0x103cc2, as sci_init and
 * sci_lock address them (0x3047a, 0x303de), where this image's are at
 * 0x10380e; its ring at 0x103cfa, as sci_getc does, and
 * its driver at 0x103dfc, each field where the driver's code addresses
 * it (sci_puts, sci_getc, sci_reconfigure, sci_send_async,
 * sci_transmitting_set, sci_transmit_end_schedule, sci_gets). */
CHECK_SIZE(sci_exchange_state, 0x103846 - 0x10380e);
CHECK_FIELD(sci_exchange_state, read_token, 0x103812 - 0x10380e);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x10382a - 0x10380e);
CHECK_FIELD(sci_exchange_state, write_token, 0x10382e - 0x10380e);
CHECK_SIZE(serial_ring, 0x103948 - 0x103846);

/* The SCI's driver, sci at 0x103948, after sci_ring (0x103846) and
 * before qspi (0x103968), as the driver's code addresses it: flow at
 * 0x103955 (sci_transmit_allowed, 0x2f412), the echo at 0x103962. */
CHECK_SIZE(sci_driver, 0x103968 - 0x103948);
CHECK_FIELD(sci_driver, transmit_count, 0x10394c - 0x103948);
CHECK_FIELD(sci_driver, async_next, 0x10394e - 0x103948);
CHECK_FIELD(sci_driver, async_count, 0x103952 - 0x103948);
CHECK_FIELD(sci_driver, async_pending, 0x103953 - 0x103948);
CHECK_FIELD(sci_driver, puts_pending, 0x103954 - 0x103948);
CHECK_FIELD(sci_driver, flow, 0x103955 - 0x103948);
CHECK_FIELD(sci_driver, receive_errors, 0x103956 - 0x103948);
CHECK_FIELD(sci_driver, read_errors, 0x103957 - 0x103948);
CHECK_FIELD(sci_driver, xon_xoff, 0x103958 - 0x103948);
CHECK_FIELD(sci_driver, mark_bit, 0x103959 - 0x103948);
CHECK_FIELD(sci_driver, seven_bits, 0x10395a - 0x103948);
CHECK_FIELD(sci_driver, reconfiguring, 0x10395b - 0x103948);
CHECK_FIELD(sci_driver, in_transmit_interrupt, 0x10395c - 0x103948);
CHECK_FIELD(sci_driver, async_allowed, 0x10395d - 0x103948);
CHECK_FIELD(sci_driver, field_16, 0x10395e - 0x103948);
CHECK_FIELD(sci_driver, echo, 0x103962 - 0x103948);
CHECK_FIELD(sci_driver, last_terminator, 0x103966 - 0x103948);
CHECK_SIZE(qspi_state, 0x1039b6 - 0x103968);
CHECK_FIELD(qspi_state, waiting, 0x10396c - 0x103968);
CHECK_FIELD(qspi_state, busy, 0x1039ac - 0x103968);
CHECK_FIELD(qspi_state, receive_buffer, 0x1039ae - 0x103968);
CHECK_FIELD(qspi_state, running, 0x1039b5 - 0x103968);
CHECK_FIELD(qspi_state, copy_pending, 0x1039b4 - 0x103968);
CHECK_FIELD(qspi_state, field_00, 0x103968 - 0x103968);
CHECK_FIELD(qspi_state, read, 0x10396a - 0x103968);
CHECK_FIELD(qspi_state, receive_count, 0x1039b2 - 0x103968);
CHECK_FIELD(qspi_state, write, 0x103969 - 0x103968);

CHECK_SIZE(eeprom_lock_token, 0x102248 - 0x102238);
CHECK_FIELD(eeprom_lock_token, owner, 0x102240 - 0x102238);
CHECK_FIELD(eeprom_lock_token, depth, 0x102244 - 0x102238);
CHECK_FIELD(eeprom_lock_token, exchange, 0x10223c - 0x102238);
CHECK_FIELD(eeprom_lock_token, status, 0x102238 - 0x102238);

CHECK_FIELD(sci_driver, baud_constant, 0x103960 - 0x103948);

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
CHECK_SIZE(psos_configuration, 0x102c06 - 0x102ba4);
CHECK_FIELD(psos_configuration, data_end, 0x102ba8 - 0x102ba4);
CHECK_FIELD(psos_configuration, region_2_start, 0x102bb0 - 0x102ba4);
CHECK_FIELD(psos_configuration, task_records, 0x102bbc - 0x102ba4);
CHECK_FIELD(psos_configuration, exchange_records, 0x102bbe - 0x102ba4);
CHECK_FIELD(psos_configuration, message_buffers, 0x102bc0 - 0x102ba4);
CHECK_FIELD(psos_configuration, last_device, 0x102bc6 - 0x102ba4);
CHECK_FIELD(psos_configuration, io_table, 0x102bc8 - 0x102ba4);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x102bcc - 0x102ba4);
CHECK_FIELD(psos_configuration, root_entry, 0x102bd0 - 0x102ba4);
CHECK_FIELD(psos_configuration, root_priority, 0x102bd8 - 0x102ba4);
CHECK_FIELD(psos_configuration, start_hook, 0x102bda - 0x102ba4);
CHECK_FIELD(psos_configuration, delete_hook, 0x102bde - 0x102ba4);
CHECK_FIELD(psos_configuration, debug_hooks, 0x102be6 - 0x102ba4);
CHECK_FIELD(psos_configuration, task_hooks, 0x102bea - 0x102ba4);
CHECK_FIELD(psos_configuration, data_area, 0x102ba4 - 0x102ba4);
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

/* The kernel's I/O table, io_table, three devices', 0x6c bytes that
 * psos_config_init copies from 0x59286 to 0x102b38 (0x58bd8), up to
 * psos_config. */

CHECK_SIZE(io_device_jumps[3], 0x102ba4 - 0x102b38);
CHECK_SIZE(io_jump, 6);

/* In the Z3815A 4010, task_table: the task at 0x102ed2 + 6n, its state
 * at 0x102ed0 + 6n, 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x102c12 - 0x102c10);
CHECK_SIZE(task_table_entry[17], 0x102c76 - 0x102c10);
CHECK_FIELD(task_table_entry, state, 0x102c10 - 0x102c10);

CHECK_SIZE(spoo_request, 0x10322e - 0x103216);
CHECK_FIELD(spoo_request, function, 0x10321e - 0x103216);
CHECK_FIELD(spoo_request, arguments, 0x103222 - 0x103216);
CHECK_SIZE(early_status_events, 0x103440 - 0x103438);

CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x102ab2 - 0x102a9e);
CHECK_FIELD(efc_gain_values, reciprocal_ti, 0x102aa6 - 0x102a9e);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x102aaa - 0x102a9e);
CHECK_FIELD(efc_gain_values, step_limit, 0x102aae - 0x102a9e);
CHECK_SIZE(efc_range_state, 0x1021d1 - 0x1021ce);
CHECK_FIELD(efc_range_state, near_end_checks, 0x1021d0 - 0x1021ce);
CHECK_FIELD(efc_range_state, clamped, 0x1021ce - 0x1021ce);
CHECK_FIELD(efc_range_state, near_end, 0x1021cf - 0x1021ce);

CHECK_SIZE(gps_command_mailbox, 0x102ac8 - 0x102ab2);
CHECK_FIELD(gps_command_mailbox, arguments, 0x102ab4 - 0x102ab2);
CHECK_FIELD(gps_command_mailbox, request, 0x102ac4 - 0x102ab2);
CHECK_FIELD(gps_command_mailbox, code, 0x102ab2 - 0x102ab2);

/* In the Z3815A 4010, adc_monitors at 0x1021e4, eight, efc_range_state
 * after them, and its hmon_channels from 0x33c9c */
CHECK_SIZE(adc_monitor, 0x10207e - 0x10204e);
CHECK_SIZE(adc_monitor[8], 0x1021ce - 0x10204e);
CHECK_FIELD(adc_monitor, value, 0x102076 - 0x10204e);
CHECK_FIELD(adc_monitor, valid, 0x10207a - 0x10204e);
CHECK_FIELD(adc_monitor, sample_index, 0x10207b - 0x10204e);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x1020ac - 0x10207e);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x10207d - 0x10204e);
CHECK_SIZE(hmon_channel_entry, 0x324c2 - 0x32498);
CHECK_SIZE(hmon_channel_entry[8], 0x325e8 - 0x32498);
CHECK_FIELD(hmon_channel_entry, add, 0x3249c - 0x32498);
CHECK_FIELD(hmon_channel_entry, check, 0x324a0 - 0x32498);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x324a4 - 0x32498);
CHECK_FIELD(hmon_channel_entry, report, 0x324a8 - 0x32498);
CHECK_FIELD(hmon_channel_entry, minimum, 0x324ac - 0x32498);
CHECK_FIELD(hmon_channel_entry, maximum, 0x324b0 - 0x32498);
CHECK_FIELD(hmon_channel_entry, nominal, 0x324b4 - 0x32498);
CHECK_FIELD(hmon_channel_entry, name, 0x324b8 - 0x32498);
CHECK_FIELD(hmon_channel_entry, events, 0x324bc - 0x32498);
CHECK_FIELD(hmon_channel_entry, bit, 0x324be - 0x32498);
CHECK_FIELD(hmon_channel_entry, field_28, 0x324c0 - 0x32498);
/* In the Z3815A 4010, gps_request_step at 0x10215e */
CHECK_SIZE(gps_request_code_word, 0x10201e - 0x10201c);
CHECK_SIZE(gps_script_step, 0x102030 - 0x10201c);
CHECK_FIELD(gps_script_step, argument_mode, 0x10201e - 0x10201c);
CHECK_FIELD(gps_script_step, arguments, 0x102020 - 0x10201c);

/* selftest_results at selftest, 0x100e26, up to the GPS record at
 * 0x100e66: each field where the image's code reaches it. */
CHECK_SIZE(selftest_results, 0x100e66 - 0x100e26);
CHECK_FIELD(selftest_results, uart, 0x100e30 - 0x100e26);
CHECK_FIELD(selftest_results, latch, 0x100e32 - 0x100e26);
CHECK_FIELD(selftest_results, lamp_mask, 0x100e33 - 0x100e26);
CHECK_FIELD(selftest_results, summary, 0x100e34 - 0x100e26);
CHECK_FIELD(selftest_results, eeprom, 0x100e36 - 0x100e26);
CHECK_FIELD(selftest_results, fpga, 0x100e38 - 0x100e26);
CHECK_FIELD(selftest_results, gps, 0x100e3a - 0x100e26);
CHECK_FIELD(selftest_results, abort, 0x100e3e - 0x100e26);
CHECK_FIELD(selftest_results, running, 0x100e3f - 0x100e26);
CHECK_FIELD(selftest_results, power, 0x100e40 - 0x100e26);
CHECK_FIELD(selftest_results, internal_reference, 0x100e42 - 0x100e26);
CHECK_FIELD(selftest_results, qspi, 0x100e44 - 0x100e26);
CHECK_FIELD(selftest_results, lamp_testing, 0x100e54 - 0x100e26);
CHECK_FIELD(selftest_results, passed, 0x100e56 - 0x100e26);
CHECK_FIELD(selftest_results, result, 0x100e58 - 0x100e26);
CHECK_FIELD(selftest_results, details, 0x100e5c - 0x100e26);
CHECK_FIELD(selftest_results, test, 0x100e64 - 0x100e26);
CHECK_FIELD(selftest_results, in_run, 0x100e65 - 0x100e26);

/* gpsx_send's frame, 26 bytes from -0x1a(A6) (0x4bb50), and
 * request_ring_put's copy, six longs and a word (0x524b6 to 0x524c2):
 * the 58503A 3704's. */
CHECK_SIZE(gps_request_entry, 26);
CHECK_FIELD(gps_request_entry, sender, 0x08);
CHECK_FIELD(gps_request_entry, result, 0x09);
CHECK_FIELD(gps_request_entry, code, 0x0a);
CHECK_FIELD(gps_request_entry, argument_mode, 0x0c);
CHECK_FIELD(gps_request_entry, arguments, 0x0e);
CHECK_FIELD(gps_request_entry, sent, 0x12);
/* gps_requests at 0x10163c: read and write at +0x9c and +0xa0
 * (request_ring_clear, 0x5247c and 0x52480; request_ring_put, 0x52490). */
CHECK_FIELD(gps_request_ring, read, 0x9c);
CHECK_FIELD(gps_request_ring, write, 0xa0);
CHECK_SIZE(gps_request_ring, 0xa4);

/* command_log at 0x101cac and command_log_snapshot at 0x101e58: read and
 * write at +0x1a4 and +0x1a8 (command_log_add, 0x532f8 to 0x53336),
 * entries of 0x14 bytes (0x532f6). */
CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x00);
CHECK_FIELD(gps_command_log_entry, code, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0a);
CHECK_FIELD(gps_command_log_entry, date, 0x0c);
CHECK_FIELD(gps_command_log_entry, time, 0x10);
CHECK_SIZE(gps_command_log, 0x101e58 - 0x101cac);
CHECK_FIELD(gps_command_log, read, 0x101e50 - 0x101cac);
CHECK_FIELD(gps_command_log, write, 0x101e54 - 0x101cac);
CHECK_FIELD(gps_command_log, entries, 0x101cac - 0x101cac);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x102212 - 0x102206);
CHECK_FIELD(lifetime_state, date, 0x10220a - 0x102206);
CHECK_FIELD(lifetime_state, count, 0x10220e - 0x102206);
CHECK_FIELD(lifetime_state, efc, 0x102206 - 0x102206);

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

/* puck_link: puck at 0x1021de; puck_frame_receive's frame (0x1021f4),
 * its flag (0x1021fa) and cursor (0x1021fc), efc_adc_correct's sum
 * (0x1021f0). */
CHECK_SIZE(puck_link, 0x102200 - 0x1021de);
CHECK_FIELD(puck_link, error_sum, 0x1021f0 - 0x1021de);
CHECK_FIELD(puck_link, frame, 0x1021f4 - 0x1021de);
CHECK_FIELD(puck_link, framing, 0x1021fa - 0x1021de);
CHECK_FIELD(puck_link, cursor, 0x1021fc - 0x1021de);

CHECK_SIZE(efc_calibration_fields, 0x102b38 - 0x102b02);
CHECK_FIELD(efc_calibration_fields, point, 0x102b03 - 0x102b02);
CHECK_FIELD(efc_calibration_fields, field_02, 0x102b04 - 0x102b02);
CHECK_FIELD(efc_calibration_fields, frequency_offset, 0x102b06 - 0x102b02);
CHECK_FIELD(efc_calibration_fields, adc_high, 0x102b32 - 0x102b02);
CHECK_FIELD(efc_calibration_fields, settle, 0x102b36 - 0x102b02);
/* adc_low at 0x102b2e, efc_calibration_measure's d2 beside a2 at
 * 0x102b02 (0x4da48, 0x4da5a): a number, as layout_addresses.py aligns
 * that move with the Z3815A 4010's of another register, naming 0x2e3f2. */
CHECK_FIELD(efc_calibration_fields, adc_low, 0x2c);

CHECK_SIZE(ptest_run, 0x102546 - 0x10253c);
CHECK_FIELD(ptest_run, passes, 0x10253d - 0x10253c);
CHECK_FIELD(ptest_run, start_uptime, 0x10253e - 0x10253c);
CHECK_FIELD(ptest_run, end_uptime, 0x102542 - 0x10253c);

CHECK_SIZE(calibration_block, 64);
CHECK_FIELD(calibration_block, time_offset, 6);
/* save_calibration_block's frame, the block at -64 (0x41748 to
 * 0x4179e) */
CHECK_FIELD(calibration_block, tc_coefficient, 0x40 - 0x3e);
CHECK_FIELD(calibration_block, field_0a, 0x40 - 0x36);
CHECK_FIELD(calibration_block, efc_dac_per_adc, 0x40 - 0x34);
CHECK_FIELD(calibration_block, efc_a, 0x40 - 0x30);
CHECK_FIELD(calibration_block, efc_b, 0x40 - 0x2c);
CHECK_FIELD(calibration_block, tc_coefficient_1, 0x40 - 0x28);
CHECK_FIELD(calibration_block, oscillator, 0x40 - 0x24);
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
CHECK_SIZE(scpi_multiplier[13], 0x274ee - 0x274ba);
CHECK_SIZE(scpi_unit[2], 0x5bb44 - 0x5bb38);
CHECK_FIELD(scpi_keyword, text, 2);
CHECK_SIZE(scpi_keyword, 0x14);
CHECK_FIELD(scpi_unit, exponent, 4);
CHECK_SIZE(scpi_error_message[101], 0x2843c - 0x281de);
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
CHECK_FIELD(fpga_configuration_table, last, 0x30ef8 - 0x30ef8);

/* status_create allocates 0x16 bytes and four a register; the first
 * four of those are registers[0]. */
CHECK_SIZE(status_model, 0x12 + 7 * 4);
CHECK_FIELD(status_model, output, 0x02);
CHECK_FIELD(status_model, queue, 0x06);
CHECK_FIELD(status_model, conditions, 0x0a);
CHECK_FIELD(status_model, field_0e, 0x0e);
CHECK_FIELD(status_model, registers, 0x12);

/* In the Z3815A 4010, the Oncore records its gps holds: tps's date
 * (0x58506) and the hold position the tps commit fills (0x5d1c6). */
CHECK_FIELD(oncore_date_fields, day, 0x10141f - 0x10141e);
CHECK_FIELD(oncore_date_fields, year, 0x101420 - 0x10141e);
CHECK_FIELD(oncore_hold_position_fields, longitude, 0x100f12 - 0x100f0e);
CHECK_FIELD(oncore_hold_position_fields, height, 0x100f16 - 0x100f0e);

/* @@Ek: in the 58503A 3704, 64 bytes of data in a 71-byte message, which
 * its decoder's table copies whole (0x525ed); @@Bk's (0x525e8) puts its
 * 38 bytes to the URAs first, zeroes the last two URAs and copies 14
 * more. */
CHECK_SIZE(oncore_extension_fields, 71 - 7);
CHECK_FIELD(oncore_extension_fields, ura, 0x20);
CHECK_FIELD(oncore_extension_fields, least_squares_inverse, 0x28);
CHECK_FIELD(oncore_extension_fields, oscillator_offset, 0x3e);

CHECK_SIZE(aging_curve_state, 0x10253c - 0x102512);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x102513 - 0x102512);
CHECK_FIELD(aging_curve_state, oldest_time, 0x102514 - 0x102512);
CHECK_FIELD(aging_curve_state, newest_time, 0x102518 - 0x102512);
CHECK_FIELD(aging_curve_state, a, 0x102524 - 0x102512);
CHECK_FIELD(aging_curve_state, b, 0x102528 - 0x102512);
CHECK_FIELD(aging_curve_state, c, 0x10252c - 0x102512);
CHECK_FIELD(aging_curve_state, rms, 0x102534 - 0x102512);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x102538 - 0x102512);

/* In the Z3815A 4010, the GPS task's state at 0x100f2c, memset(&gps, 0,
 * 0x678) by gps_init (0x57c14): each field where its code reaches it,
 * through gps's address or a base register loaded with it; the
 * sentences' fields where their decoders' table (0x5e32a) points. */
/* gps_state: system_init copies 0x170 longs from preserved_region to it
 * (0x22df6); its records where the message table points their decoders
 * (docs/firmware/oncore/z3817a-3752.txt) and its fields where the
 * image's code reaches them (gps, 0x100e66). */
CHECK_SIZE(gps_state, 0x170 * 4);
CHECK_FIELD(gps_state, status_received, 0x100f38 - 0x100e66);
CHECK_FIELD(gps_state, status, 0x100f3a - 0x100e66);
CHECK_FIELD(gps_state, visibility, 0x100f84 - 0x100e66);
CHECK_FIELD(gps_state, xdop_table, 0x101003 - 0x100e66);
CHECK_FIELD(gps_state, almanac, 0x101050 - 0x100e66);
CHECK_FIELD(gps_state, corrections, 0x10111e - 0x100e66);
CHECK_FIELD(gps_state, correction_average, 0x10115e - 0x100e66);
CHECK_FIELD(gps_state, correction_deviation, 0x101166 - 0x100e66);
CHECK_FIELD(gps_state, raim, 0x101170 - 0x100e66);
/* @@Bn's and @@En's record: the decoders' tables (0x58a6f, 0x58a83) */
CHECK_SIZE(oncore_raim_fields, 0x1011bc - 0x101170);
CHECK_FIELD(oncore_raim_fields, pps_mode, 0x101176 - 0x101170);
CHECK_FIELD(oncore_raim_fields, pps_rate, 0x101178 - 0x101170);
CHECK_FIELD(oncore_raim_fields, next_hour, 0x101181 - 0x101170);
CHECK_FIELD(oncore_raim_fields, sawtooth, 0x10118a - 0x101170);
CHECK_FIELD(oncore_raim_fields, channels, 0x10118c - 0x101170);
CHECK_FIELD(gps_state, raim_received, 0x1011bc - 0x100e66);
CHECK_FIELD(gps_state, leap_second_pending, 0x1011be - 0x100e66);
CHECK_FIELD(gps_state, self_test, 0x1011c0 - 0x100e66);
CHECK_FIELD(gps_state, extension, 0x1011c8 - 0x100e66);
CHECK_FIELD(gps_state, fix_mode, 0x101272 - 0x100e66);
CHECK_FIELD(gps_state, receiver_id_received, 0x101274 - 0x100e66);
CHECK_FIELD(gps_state, receiver_id, 0x101275 - 0x100e66);
CHECK_FIELD(gps_state, power_on_failure, 0x10138a - 0x100e66);
CHECK_FIELD(gps_state, average_count, 0x10138c - 0x100e66);
CHECK_FIELD(gps_state, average_latitude, 0x101390 - 0x100e66);
CHECK_FIELD(gps_state, average_height_msl, 0x1013a8 - 0x100e66);
CHECK_FIELD(gps_state, references, 0x1013b0 - 0x100e66);
CHECK_FIELD(gps_state, surveying, 0x1013dc - 0x100e66);
CHECK_FIELD(gps_state, raim_assessment, 0x1013df - 0x100e66);
CHECK_FIELD(gps_state, pps_check_disabled, 0x1013e0 - 0x100e66);
CHECK_FIELD(gps_state, receiver_health_failed, 0x1013e1 - 0x100e66);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x1013e2 - 0x100e66);
CHECK_FIELD(gps_state, command_monitor_query, 0x1013e4 - 0x100e66);
CHECK_FIELD(gps_state, command_monitor_time, 0x1013e8 - 0x100e66);
CHECK_FIELD(gps_state, command_monitor_error, 0x1013ec - 0x100e66);
CHECK_FIELD(gps_state, command_monitor_failed, 0x1013ed - 0x100e66);
CHECK_FIELD(gps_state, status_monitor_enabled, 0x1013ee - 0x100e66);
CHECK_FIELD(gps_state, status_monitor_state, 0x1013ef - 0x100e66);
CHECK_FIELD(gps_state, status_monitor_time, 0x1013f0 - 0x100e66);
CHECK_FIELD(gps_state, raim_time, 0x1013f4 - 0x100e66);
CHECK_FIELD(gps_state, status_monitor_start, 0x1013f8 - 0x100e66);
CHECK_FIELD(gps_state, status_last_time, 0x1013fc - 0x100e66);
CHECK_FIELD(gps_state, status_repeat_count, 0x101400 - 0x100e66);
CHECK_FIELD(gps_state, status_monitor_failed, 0x101401 - 0x100e66);
CHECK_FIELD(gps_state, echoes_aged, 0x101402 - 0x100e66);
CHECK_FIELD(gps_state, echoes, 0x101406 - 0x100e66);
CHECK_FIELD(gps_state, echo_ages, 0x101408 - 0x100e66);
/* gps_echo_entries, six entries and an end from 0x58b9a (gps_echo_mark) */
CHECK_SIZE(gps_echo_entry[7], 0x58bb6 - 0x58b9a);
CHECK_FIELD(gps_echo_entry, echo, 0x58b9e - 0x58b9c);
CHECK_FIELD(gps_state, channel_count, 0x10140e - 0x100e66);
CHECK_FIELD(gps_state, field_592, 0x101410 - 0x100e66);
CHECK_FIELD(gps_state, tracking_log, 0x101412 - 0x100e66);
CHECK_FIELD(gps_state, track_words, 0x101414 - 0x100e66);
CHECK_FIELD(gps_state, channel_word, 0x101420 - 0x100e66);
CHECK_FIELD(gps_state, channel_word_lowest, 0x101424 - 0x100e66);
/* In the Z3815A 4010, the tps commit's hold position, 0x5d1c6 */
/* in the Z3815A 4010, the commits' status: the date and time (0x5cc5e),
 * the position (0x5cec6), the DOP and its type and the channels
 * (0x5cf20) */
/* in the Z3815A 4010, gps_print_best_combinations' type, count and mode
 * (0x53906 on) */
/* in the Z3815A 4010, the time RAIM record from gps_print_time_raim's
 * base (0x53be4) */
/* in the Z3815A 4010, gps_print_extension's first and last fields
 * (0x539e2 on) */
/* in the Z3815A 4010, the tst commit's lines (0x5d298) */
/* in the Z3815A 4010, each sentence's fields, where its table entry
 * points */
/* in the Z3815A 4010, gps_init clears +0x5ee (0x57c22) */

/* In the Z3815A 4010, the sentences' records, from their fields above */
CHECK_SIZE(nmea_gga_fields, 0x53e - 0x528);
CHECK_SIZE(nmea_gsa_fields, 0x548 - 0x53e);
CHECK_SIZE(nmea_gsv_satellite, 0x580 - 0x57a);
CHECK_SIZE(nmea_tps_fields, 0x5e2 - 0x5b6);
CHECK_SIZE(oncore_status_fields, 0x47);

CHECK_SIZE(oncore_visibility_fields, 0x7a);

CHECK_SIZE(oncore_xdop_table_fields, 75);

CHECK_SIZE(oncore_almanac_status_fields, 16);

CHECK_SIZE(oncore_corrections_fields, 64);

/* In the Z3815A 4010, the two reference records, 0x56c5c's (0x101540,
 * 0x101556), where this image's are at 0x1013b0 and 0x1013c6 */

CHECK_SIZE(gps_reference_record, 0x1013c6 - 0x1013b0);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* in the 58503A 3704, 0x4b858 stores the GPS's at 0x1013a6 and 0x1013a8;
 * in the Z3815A 4010, the offsets checked */
CHECK_FIELD(gps_reference_record, hysteresis, 0x1013c2 - 0x1013b0);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x1013c4 - 0x1013b0);

/* The loop's state at 0x10269a, memset(&pll, 0, 0x3ee) by pll_init
 * (0x4c9ae): each field where this image's code reaches it, through
 * pll's address or a base register loaded with it (make record-access
 * IMAGE=z3817a-3752 RECORD='0x10269a 0x3ee --fit pll_state'); the hold
 * causes a word (diag_pr_hold_cause). */
CHECK_SIZE(pll_state, 0x102a88 - 0x10269a);
CHECK_SIZE(pll_hold_cause, 2);
CHECK_SIZE(reference_pulse_mode, 1);
CHECK_FIELD(pll_state, previous_stage, 0x10269b - 0x10269a);
CHECK_FIELD(pll_state, powerup_countdown, 0x10269c - 0x10269a);
CHECK_FIELD(pll_state, powerup_state, 0x10269d - 0x10269a);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x10269e - 0x10269a);
CHECK_FIELD(pll_state, reference_check_state, 0x1026a0 - 0x10269a);
CHECK_FIELD(pll_state, startup_state, 0x1026a6 - 0x10269a);
CHECK_FIELD(pll_state, normal_state, 0x1026a8 - 0x10269a);
CHECK_FIELD(pll_state, hold_causes, 0x1026aa - 0x10269a);
CHECK_FIELD(pll_state, holdover_state, 0x1026ac - 0x10269a);
CHECK_FIELD(pll_state, manual, 0x1026ad - 0x10269a);
CHECK_FIELD(pll_state, ti_limit_override, 0x1026ae - 0x10269a);
CHECK_FIELD(pll_state, holdover_offset, 0x1026b0 - 0x10269a);
CHECK_FIELD(pll_state, holdover_offset_step, 0x1026b4 - 0x10269a);
CHECK_FIELD(pll_state, recovery_countdown, 0x1026b8 - 0x10269a);
CHECK_FIELD(pll_state, recovery_state, 0x1026b9 - 0x10269a);
CHECK_FIELD(pll_state, coarse_state, 0x1026ba - 0x10269a);
CHECK_FIELD(pll_state, fine_state, 0x1026bb - 0x10269a);
CHECK_FIELD(pll_state, frequency_adjusted, 0x1026bc - 0x10269a);
CHECK_FIELD(pll_state, fine_index, 0x1026bd - 0x10269a);
CHECK_FIELD(pll_state, fine_readings, 0x1026be - 0x10269a);
CHECK_FIELD(pll_state, fine_retries, 0x1026c0 - 0x10269a);
CHECK_FIELD(pll_state, fine_d, 0x1026c2 - 0x10269a);
CHECK_FIELD(pll_state, aging_state, 0x1026c6 - 0x10269a);
CHECK_FIELD(pll_state, gps_diag_state, 0x1026ca - 0x10269a);
CHECK_FIELD(pll_state, gps_diag_substate, 0x1026cb - 0x10269a);
CHECK_FIELD(pll_state, gps_init_state, 0x1026cc - 0x10269a);
CHECK_FIELD(pll_state, leap_check_timer, 0x1026ce - 0x10269a);
CHECK_FIELD(pll_state, leap_check_requested, 0x1026d0 - 0x10269a);
CHECK_FIELD(pll_state, leap_check_state, 0x1026d1 - 0x10269a);
CHECK_FIELD(pll_state, leap_ack_state, 0x1026d2 - 0x10269a);
CHECK_FIELD(pll_state, leap_ack_requested, 0x1026d3 - 0x10269a);
CHECK_FIELD(pll_state, leap_ack_done, 0x1026d4 - 0x10269a);
CHECK_FIELD(pll_state, leap_ack_again, 0x1026d6 - 0x10269a);
CHECK_FIELD(pll_state, cable_delay_changed, 0x1026d8 - 0x10269a);
CHECK_FIELD(pll_state, cable_delay_known, 0x1026d9 - 0x10269a);
CHECK_FIELD(pll_state, cable_delay, 0x1026da - 0x10269a);
CHECK_FIELD(pll_state, hardware_fault_seconds, 0x1026de - 0x10269a);
CHECK_FIELD(pll_state, reference_hunt_seconds, 0x1026e0 - 0x10269a);
CHECK_FIELD(pll_state, not_ready_seconds, 0x1026e2 - 0x10269a);
CHECK_FIELD(pll_state, oscillator_was_ready, 0x1026e4 - 0x10269a);
CHECK_FIELD(pll_state, timeref_select, 0x1026e5 - 0x10269a);
CHECK_FIELD(pll_state, unit_failed, 0x1026e6 - 0x10269a);
CHECK_FIELD(pll_state, timeref_switch_requested, 0x1026e7 - 0x10269a);
CHECK_FIELD(pll_state, efc, 0x1026e8 - 0x10269a);
CHECK_FIELD(pll_state, tfom, 0x1026ec - 0x10269a);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x1026f0 - 0x10269a);
CHECK_FIELD(pll_state, measured_uncertainty, 0x1026f4 - 0x10269a);
CHECK_FIELD(pll_state, frequency_tolerance, 0x102700 - 0x10269a);
CHECK_FIELD(pll_state, efc_history, 0x102704 - 0x10269a);
CHECK_FIELD(pll_state, aging_term, 0x102a52 - 0x10269a);
CHECK_FIELD(pll_state, loop_efc_step, 0x102a56 - 0x10269a);
CHECK_FIELD(pll_state, ti_filtered, 0x102a5a - 0x10269a);
CHECK_FIELD(pll_state, efc_base, 0x102a5e - 0x10269a);
CHECK_FIELD(pll_state, loop_time_constant, 0x102a62 - 0x10269a);
CHECK_FIELD(pll_state, led_gps_lock, 0x102a67 - 0x10269a);
CHECK_FIELD(pll_state, led_holdover, 0x102a68 - 0x10269a);
CHECK_FIELD(pll_state, holdover_duration, 0x102a6a - 0x10269a);
CHECK_FIELD(pll_state, holdover_start_uptime, 0x102a6e - 0x10269a);
CHECK_FIELD(pll_state, reference_uptime, 0x102a72 - 0x10269a);
CHECK_FIELD(pll_state, reference_time, 0x102a76 - 0x10269a);
CHECK_FIELD(pll_state, efc_control_passes, 0x102a7a - 0x10269a);
CHECK_FIELD(pll_state, reference_seconds, 0x102a7e - 0x10269a);
CHECK_FIELD(pll_state, fpga_configuration, 0x102a7f - 0x10269a);
CHECK_FIELD(pll_state, fpga_loaded, 0x102a80 - 0x10269a);
CHECK_FIELD(pll_state, loop_running, 0x102a81 - 0x10269a);
CHECK_FIELD(pll_state, sync_requested, 0x102a82 - 0x10269a);
CHECK_FIELD(pll_state, field_3e9, 0x102a83 - 0x10269a);
CHECK_FIELD(pll_state, pulse_mode, 0x102a84 - 0x10269a);
CHECK_FIELD(pll_state, field_3eb, 0x102a85 - 0x10269a);
CHECK_FIELD(pll_state, block_length, 0x102a86 - 0x10269a);
CHECK_FIELD(pll_state, measured_uncertainty_valid, 0x102a87 - 0x10269a);

/* the history 0x34e bytes, at 0x102704: efc_history_update's (0x46422)
 * and the fits' indexed arrays of 32, measured_uncertainty_update's fit
 * arrays (0x4603c to 0x46054) */
CHECK_SIZE(efc_ring, 0x102a52 - 0x102704);
CHECK_FIELD(efc_ring, stop_ptr, 0x102705 - 0x102704);
CHECK_FIELD(efc_ring, loop_settled, 0x102706 - 0x102704);
CHECK_FIELD(efc_ring, summed_count, 0x10270a - 0x102704);
CHECK_FIELD(efc_ring, averages, 0x10270c - 0x102704);
CHECK_FIELD(efc_ring, replaced_average, 0x10278c - 0x102704);
CHECK_FIELD(efc_ring, weights, 0x102790 - 0x102704);
CHECK_FIELD(efc_ring, replaced_weight, 0x1027b0 - 0x102704);
CHECK_FIELD(efc_ring, times, 0x1027b2 - 0x102704);
CHECK_FIELD(efc_ring, replaced_time, 0x102832 - 0x102704);
CHECK_FIELD(efc_ring, logs, 0x102836 - 0x102704);
CHECK_FIELD(efc_ring, fit_a_history, 0x1028b6 - 0x102704);
CHECK_FIELD(efc_ring, fit_b_history, 0x102936 - 0x102704);
CHECK_FIELD(efc_ring, fit_c_history, 0x1029b6 - 0x102704);
CHECK_FIELD(efc_ring, next_entry_uptime, 0x102a36 - 0x102704);
CHECK_FIELD(efc_ring, efc_sum, 0x102a3a - 0x102704);
CHECK_FIELD(efc_ring, efc_origin, 0x102a3e - 0x102704);
CHECK_FIELD(efc_ring, fit_a, 0x102a46 - 0x102704);
CHECK_FIELD(efc_ring, fit_b, 0x102a4a - 0x102704);
CHECK_FIELD(efc_ring, fit_c, 0x102a4e - 0x102704);

/* Records the printf calls name; in the 58503A 3704 @@Cj's decoder's
 * table (0x52620) lays the receiver ID out as the Z3805A 3543B's: 34
 * bytes, then 9 lines of 27. */

/* In the 58503A 3704, the receiver ID's lines (0x5262c): 12 bytes each,
 * the table ending at 0x52698, its tenth; the model number's and the
 * options' texts at 0x1012e7 and 0x101353, in gps.receiver_id from
 * 0x101259. */

/* The Z3817A 3752's own, from gps_counts (0x101552): the process
 * monitor's reads (0x536ec to 0x53850) and gps_receive's and the
 * monitors' counts (0x522e0, 0x51e20, 0x5202c). */
CHECK_SIZE(gps_statistics, 0x40);
CHECK_FIELD(gps_statistics, responses_read, 0x22);
CHECK_FIELD(gps_statistics, e_retries, 0x26);
CHECK_FIELD(gps_statistics, t_retries, 0x28);
CHECK_FIELD(gps_statistics, raim_detected, 0x10157c - 0x101552);
CHECK_FIELD(gps_statistics, raim_alarms, 0x10157e - 0x101552);
CHECK_FIELD(gps_statistics, unknown_response, 0x36);
CHECK_FIELD(gps_statistics, frame_errors, 0x10158a - 0x101552);
CHECK_FIELD(gps_statistics, checksum_errors, 0x3a);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x10158e - 0x101552);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x101590 - 0x101552);

CHECK_SIZE(exception_record, 0x100bb2 - 0x100ba0);
CHECK_FIELD(exception_record, name, 0x100ba1 - 0x100ba0);
CHECK_FIELD(exception_record, sr, 0x100ba6 - 0x100ba0);
CHECK_FIELD(exception_record, pc, 0x100ba8 - 0x100ba0);
CHECK_FIELD(exception_record, vector, 0x100bac - 0x100ba0);
CHECK_FIELD(exception_record, address, 0x100bae - 0x100ba0);
CHECK_FIELD(exception_record, pending, 0x100ba0 - 0x100ba0);

/* In the Z3805A 3543B, the pool: five entries from 0x10372a to
 * 0x1037ac. */
CHECK_SIZE(klok_entry, (0x1032c0 - 0x10323e) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* In the Z3815A 4010, log_read's page, log_page_buffer at 0x102452:
 * entry n's fields at 0x102456, 0x10245a, 0x10245e, ... + n * 0x14. */
CHECK_SIZE(log_page, 0x1022f0 - 0x1022b0);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x1022b4 - 0x1022b0);
CHECK_FIELD(log_entry, format, 0x1022bc - 0x1022b4);
CHECK_FIELD(log_entry, argument_1, 0x1022c0 - 0x1022b4);
CHECK_SIZE(fpga_bit, 4);

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
CHECK_SIZE(nv_settings_fields, 0x10249d - 0x1023d2);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x1023d4 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, antenna_delay, 0x1023d8 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, douglas_duration, 0x1023dc - 0x1023d2);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x1023e0 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x1023e1 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x1023e2 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, external_questionable_hysteresis,
            0x1023e6 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x1023ea - 0x1023d2);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x1023ee - 0x1023d2);
CHECK_FIELD(nv_settings_fields, holdover_limit_threshold, 0x1023f2 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x1023f6 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x1023f7 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, position, 0x1023f8 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x102404 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, time_zone, 0x102408 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x102410 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, leap_date, 0x102414 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, leap_minute_length, 0x102418 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x10241c - 0x1023d2);
CHECK_FIELD(nv_settings_fields, raim_state, 0x10241d - 0x1023d2);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x102420 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x102428 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, status_enable, 0x10242c - 0x1023d2);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x102448 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x10245c - 0x1023d2);
CHECK_FIELD(nv_settings_fields, log_wrap, 0x102470 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x102471 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, serial_restricted, 0x102472 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x102473 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x102474 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, pulse_start_time, 0x102478 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x102484 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, field_be, 0x102490 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, output_state, 0x102494 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, format_data, 0x102495 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, memory_save, 0x102496 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x102497 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, douglas_state_powerup, 0x102498 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, tcode_format, 0x102499 - 0x1023d2);
CHECK_FIELD(nv_settings_fields, gps_utc, 0x10249a - 0x1023d2);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x10249b - 0x1023d2);
CHECK_FIELD(nv_settings_fields, debug_output, 0x10249c - 0x1023d2);

/* SER1's :RECeive:PACE descriptor at 0x445bc, followed by the next
 * descriptor's widths (0x445d8) and that descriptor at 0x445da */
CHECK_SIZE(scpi_setting_param, 0x445da - 0x445bc - 2);
CHECK_FIELD(scpi_setting_param, maximum, 0x06);
CHECK_FIELD(scpi_setting_param, gps_request, 0x16);
CHECK_FIELD(scpi_setting_param, group_cell, 0x18);
CHECK_FIELD(scpi_setting_param, value, 0xc);
/* 15 action descriptors, each referred to at its own address, from
 * 0x45d90 to the last at 0x45de4 */
CHECK_SIZE(scpi_action_param, (0x45de4 - 0x45d90) / 14);
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
/* oncore_messages, 57 entries from 0x575d8 (oncore_message_entry_at,
 * 0x5701a to 0x57022); the fields' offsets in its first entry */
CHECK_SIZE(oncore_message_entry[57], 0x58334 - 0x575d8);
CHECK_FIELD(oncore_message_entry, filter, 0x38);
CHECK_FIELD(oncore_message_entry, kind, 0x575dc - 0x575d8);
CHECK_FIELD(oncore_message_entry, encoders, 0x575de - 0x575d8);
CHECK_FIELD(oncore_message_entry, query, 0x575ee - 0x575d8);
CHECK_FIELD(oncore_message_entry, timeout, 0x575f2 - 0x575d8);
CHECK_FIELD(oncore_message_entry, length, 0x575f4 - 0x575d8);
CHECK_FIELD(oncore_message_entry, match_kind, 0x575f6 - 0x575d8);
CHECK_FIELD(oncore_message_entry, decoders, 0x575f8 - 0x575d8);
CHECK_FIELD(oncore_message_entry, decoders[0].record, 0x575fc - 0x575d8);

/* The time-interval counter's two records: the reading at the Z3815A
 * 4010's 0x10268c, its flag at 0x102694 and armed flag at 0x102698; the
 * configuration at the Z3817A 3752's 0x1024fc, ending where aging_curve
 * begins (0x102512), each field where counter_configuration_init
 * (0x459bc to 0x459d2), counter_arm (0x45786 to 0x457fa) and
 * counter_read (0x45896) reach it. */
CHECK_SIZE(interval_reading_fields, 0x1024f7 - 0x1024ea);
CHECK_FIELD(interval_reading_fields, valid, 0x1024f2 - 0x1024ea);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x1024f6 - 0x1024ea);
CHECK_FIELD(interval_reading_fields, value, 0x1024ea - 0x1024ea);

CHECK_SIZE(counter_configuration_fields, 0x102512 - 0x1024fc);
CHECK_FIELD(counter_configuration_fields, fpga_registers, 0);
CHECK_FIELD(counter_configuration_fields, calibration, 4);
CHECK_FIELD(counter_configuration_fields, registers_mode, 0xc);
CHECK_FIELD(counter_configuration_fields, registers_state, 0xd);
CHECK_FIELD(counter_configuration_fields, sync_mode, 0xe);
CHECK_FIELD(counter_configuration_fields, sync_state, 0xf);
CHECK_FIELD(counter_configuration_fields, sync_kind, 0x10);
CHECK_FIELD(counter_configuration_fields, sync_count, 0x12);

/* The status snapshots, 0x170 bytes each: status_snapshot_update's
 * latest and flagged (0x1019c8, 0x101b38); each field where
 * status_snapshot (0x50c3c to 0x50ed2) stores it through A2, the record
 * (make record-access). */
CHECK_SIZE(status_snapshot_record, 0x101b38 - 0x1019c8);
CHECK_FIELD(status_snapshot_record, stage, 0x0);
CHECK_FIELD(status_snapshot_record, mode, 0x1);
CHECK_FIELD(status_snapshot_record, powerup_suffix, 0x2);
CHECK_FIELD(status_snapshot_record, recovery_suffix, 0x3);
CHECK_FIELD(status_snapshot_record, tfom, 0x4);
CHECK_FIELD(status_snapshot_record, ffom, 0x8);
CHECK_FIELD(status_snapshot_record, ffom_valid, 0x9);
CHECK_FIELD(status_snapshot_record, interval, 0xa);
CHECK_FIELD(status_snapshot_record, interval_valid, 0x12);
CHECK_FIELD(status_snapshot_record, interval_errors, 0x13);
CHECK_FIELD(status_snapshot_record, interval_tag, 0x14);
CHECK_FIELD(status_snapshot_record, interval_mean, 0x16);
CHECK_FIELD(status_snapshot_record, interval_mean_flag, 0x1a);
CHECK_FIELD(status_snapshot_record, holdover_waiting_reason, 0x1c);
CHECK_FIELD(status_snapshot_record, holdover_limit_state, 0x1d);
CHECK_FIELD(status_snapshot_record, holdover_limit_threshold, 0x1e);
CHECK_FIELD(status_snapshot_record, holdover_duration, 0x22);
CHECK_FIELD(status_snapshot_record, kenneth_present, 0x26);
CHECK_FIELD(status_snapshot_record, kenneth_present_valid, 0x2a);
CHECK_FIELD(status_snapshot_record, kenneth_present_resolution, 0x2b);
CHECK_FIELD(status_snapshot_record, kenneth_predicted, 0x2c);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_valid, 0x30);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_resolution, 0x31);
CHECK_FIELD(status_snapshot_record, channel_count, 0x32);
CHECK_FIELD(status_snapshot_record, satellites, 0x34);
CHECK_FIELD(status_snapshot_record, not_tracked, 0x5e);
CHECK_FIELD(status_snapshot_record, tracked, 0xde);
CHECK_FIELD(status_snapshot_record, status_received, 0x11e);
CHECK_FIELD(status_snapshot_record, not_tracked_count, 0x11f);
CHECK_FIELD(status_snapshot_record, tracked_count, 0x120);
CHECK_FIELD(status_snapshot_record, timeref_select, 0x121);
CHECK_FIELD(status_snapshot_record, pulse_mode, 0x122);
CHECK_FIELD(status_snapshot_record, reference_valid, 0x123);
CHECK_FIELD(status_snapshot_record, pps_dead, 0x124);
CHECK_FIELD(status_snapshot_record, status_code, 0x125);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 0x126);
CHECK_FIELD(status_snapshot_record, external_valid, 0x12a);
CHECK_FIELD(status_snapshot_record, external_pps_dead, 0x12b);
CHECK_FIELD(status_snapshot_record, external_status_code, 0x12c);
CHECK_FIELD(status_snapshot_record, external_elapsed, 0x12e);
CHECK_FIELD(status_snapshot_record, field_592, 0x132);
CHECK_FIELD(status_snapshot_record, gps_field_663, 0x133);
CHECK_FIELD(status_snapshot_record, fix_mode, 0x134);
CHECK_FIELD(status_snapshot_record, receiver_status, 0x135);
CHECK_FIELD(status_snapshot_record, time, 0x136);
CHECK_FIELD(status_snapshot_record, date, 0x13a);
CHECK_FIELD(status_snapshot_record, time_zone, 0x13e);
CHECK_FIELD(status_snapshot_record, from_clock, 0x146);
CHECK_FIELD(status_snapshot_record, utc_time, 0x147);
CHECK_FIELD(status_snapshot_record, leap_second_pending, 0x148);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 0x14a);
CHECK_FIELD(status_snapshot_record, mask_angle, 0x14e);
CHECK_FIELD(status_snapshot_record, position_hold, 0x14f);
CHECK_FIELD(status_snapshot_record, douglas_duration_infinite, 0x150);
CHECK_FIELD(status_snapshot_record, gps_field_576, 0x151);
CHECK_FIELD(status_snapshot_record, gps_field_577, 0x152);
CHECK_FIELD(status_snapshot_record, position_source, 0x154);
CHECK_FIELD(status_snapshot_record, position, 0x158);
CHECK_FIELD(status_snapshot_record, height_type, 0x164);
CHECK_FIELD(status_snapshot_record, average_count, 0x166);
CHECK_FIELD(status_snapshot_record, douglas_duration, 0x16a);
CHECK_FIELD(status_snapshot_record, hardware_condition, 0x16e);
/* In the Z3815A 4010, status_screen_card_lines, six entries of 6 bytes
 * from 0x59716 */
CHECK_SIZE(status_screen_card_line[6], 0x53f83 - 0x53f5f);
CHECK_FIELD(status_screen_card_line, text, 0x53f67 - 0x53f65);
CHECK_SIZE(satellite_list_entry, 8);
CHECK_SIZE(satellite_sets, 0x60 - 0x36);

/* In the Z3815A 4010, the preserved region at 0x100000, the 0xc04 bytes
 * record_checksum sums (system_init, 0x22dd0): gps's copy at 0x100004,
 * pll's at 0x10067c, adc_monitors' at 0x100a6a and retained_block's 0x19
 * bytes at 0x100bea (0x22e06 to 0x22e48). */
/* preserved_region_fields: system_init's checksum length and copies
 * (0x22da8, 0x22df2 to 0x22e2a). */
CHECK_SIZE(preserved_region_fields, 0xb50);
CHECK_FIELD(preserved_region_fields, gps, 0x100004 - 0x100000);
CHECK_FIELD(preserved_region_fields, pll, 0x1005c4 - 0x100000);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0x1009b2 - 0x100000);
CHECK_FIELD(preserved_region_fields, values, 0x100b32 - 0x100000);

/* in the 58503A 3704, the 0x180 bytes pllp_task and system_init copy
 * from 0x101e78: the ADC monitors, which monitor_average_add indexes by
 * 0x30 (0x21d40) */

/* In the Z3815A 4010, the clock record at 0x100c6e: the routines'
 * offsets, at the address the derivation from the 58503A's code gives
 * (layout_addresses.py), where this image's clock is at 0x100bba. */
CHECK_SIZE(clock_record, 0x100bed - 0x100bba);
CHECK_FIELD(clock_record, uptime, 0x100bbc - 0x100bba);
CHECK_FIELD(clock_record, time, 0x100bc0 - 0x100bba);
CHECK_FIELD(clock_record, date, 0x100bc4 - 0x100bba);
CHECK_FIELD(clock_record, month_days, 0x100bc8 - 0x100bba);
CHECK_FIELD(clock_record, gps_time, 0x100bd4 - 0x100bba);
CHECK_FIELD(clock_record, gps_date, 0x100bd8 - 0x100bba);
CHECK_FIELD(clock_record, gps_month_days, 0x100bdc - 0x100bba);
CHECK_FIELD(clock_record, minute_length, 0x100be8 - 0x100bba);
CHECK_FIELD(clock_record, set, 0x100be9 - 0x100bba);
CHECK_FIELD(clock_record, gps_utc_offset, 0x100bea - 0x100bba);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x100bec - 0x100bba);

/* In the Z3815A 4010, pps_message at 0x100e64, pps_exchange after the
 * cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x100dc8 - 0x100db0);
CHECK_FIELD(pps_exchange_message, gps_time, 0x100db8 - 0x100db0);
CHECK_FIELD(pps_exchange_message, gps_date, 0x100dbc - 0x100db0);

CHECK_SIZE(gps_process_state, 1);

/* the 58503A 3704's three status snapshots, 0x1016ae (status_screen_print,
 * 0x4b6ec), 0x10181e and 0x101988, with the two cells at 0x101818
 * between the first two; each field where status_snapshot (0x4ac56)
 * stores it */

/* status_screen_mode_lines' five entries from 0x53f0c, the last a
 * terminator, its text null (status_screen, 0x51018 to 0x5119c) */
CHECK_SIZE(status_screen_mode_line[5], 0x53f2a - 0x53f0c);
CHECK_FIELD(status_screen_mode_line, text, 0x02);

/* in the Z3815A 4010, the event log's write page at 0x10240c, its
 * number after it */
CHECK_SIZE(log_page, 0x1022aa - 0x10226a);

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
CHECK_SIZE(gps_running_command, 0x72);
CHECK_FIELD(gps_running_command, script, 0x1a);
CHECK_FIELD(gps_running_command, message, 0x1e);
CHECK_FIELD(gps_running_command, query, 0x20);
CHECK_FIELD(gps_running_command, length, 0x21);
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

/* The ROM's packed script steps, each to the next (0x5840c to
 * 0x5868c). */
CHECK_SIZE(gps_script_step_1_argument, 0x58414 - 0x5840c);
CHECK_SIZE(gps_script_step_2_arguments, 0x5859c - 0x58590);
CHECK_SIZE(gps_script_step_3_arguments, 0x58424 - 0x58414);
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

/* alarm_text's frame: link -6, the cursor's text at -6 and severity at
 * -2 (0x39fbe) */
CHECK_SIZE(alarm_text_cursor, 6);
CHECK_FIELD(alarm_text_cursor, severity, -2 - -6);
/* in the 58503A 3704, the tree's root at 0x29b42, its entries' table at
 * 0x29ac2 to the names at 0x29b1c, nine 10-byte entries, the last empty;
 * the check, the Z3815A 4010's root at 0x3d204 */

CHECK_FIELD(alarm_text_node, entries, 0x3a3bc - 0x3a3ba);

/* port_error_reports at 0x1030a0, as sci_scpi_session_init fills it:
 * the masks at +0, +4, +8 and +0xc, the conditions at +2 to +0xe
 * (0x39686 to 0x396ae) */
CHECK_SIZE(port_error_report, 4);
CHECK_FIELD(port_error_report, condition, 2);

/* duartb_driver: duartb at 0x1037f8, loopback_receive_next after it
 * (0x103802); the send's count (0x2d9f2) and the least free space
 * (0x2d8f6). */
CHECK_SIZE(duartb_driver, 0x103802 - 0x1037f8);
CHECK_FIELD(duartb_driver, transmit_count, 0x1037fc - 0x1037f8);
CHECK_FIELD(duartb_driver, least_free, 0x103800 - 0x1037f8);
