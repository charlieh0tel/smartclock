/* The layouts smartclock.h declares, checked against the image: each
 * size and offset here is one docs/firmware/structures.md gives, and
 * where it was read from addresses in the Z3805A 3543B image it is
 * written as their difference.  This is the 58503B 1.01.04's copy of
 * the Z3817A 3752's, its base (docs/firmware/58503b-1.01.04.md), as it
 * stood when the 58503B was seeded: each block's comment and addresses
 * are the Z3817A's, or the image's that comment names.  None is yet
 * read from the 58503B's code.  Compiled, never run, by "make
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

CHECK_SIZE(holdover_steering_state, 0x102aa8 - 0x102a9e);
CHECK_FIELD(holdover_steering_state, readings, 0x102aa2 - 0x102a9e);
CHECK_FIELD(holdover_steering_state, seconds, 0x102aa3 - 0x102a9e);
CHECK_FIELD(holdover_steering_state, ti_largest, 0x102aa4 - 0x102a9e);

CHECK_SIZE(loop_steering_state, 0x102ac4 - 0x102aa8);
CHECK_FIELD(loop_steering_state, seconds, 0x102ab4 - 0x102aa8);
CHECK_FIELD(loop_steering_state, proportional_gain, 0x102ab8 - 0x102aa8);
CHECK_FIELD(loop_steering_state, filter_weight, 0x102ac0 - 0x102aa8);

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

/* The 58503B's own, read from its code: acia_tx_interrupt's count
 * (0x2d4f4), acia_isr's overrun, errors and least room (0x2d47a, 0x2d48a,
 * 0x2d4e2), acia_send_message's held flag (0x2d338). */
CHECK_SIZE(acia_driver, 0x103da6 - 0x103d9c);
CHECK_FIELD(acia_driver, transmit_count, 0x103da0 - 0x103d9c);
CHECK_FIELD(acia_driver, overrun, 0x103da2 - 0x103d9c);
CHECK_FIELD(acia_driver, receive_errors, 0x103da3 - 0x103d9c);
CHECK_FIELD(acia_driver, held, 0x103da4 - 0x103d9c);
CHECK_FIELD(acia_driver, least_free, 0x103da5 - 0x103d9c);

/* The SCI's settings at 0x102640, as sci_reconfigure reads them
 * (0x30730 on), and the installer's at 0x10acb0, as
 * installer_parameter_record_read loads them (installer::0x108398 on). */
CHECK_SIZE(sci_settings_record, 0x1029a8 - 0x1029a2);
CHECK_FIELD(sci_settings_record, baud, 0x1029a4 - 0x1029a2);
CHECK_FIELD(sci_settings_record, parity, 0x1029a5 - 0x1029a2);
CHECK_FIELD(sci_settings_record, xon_xoff, 0x1029a6 - 0x1029a2);
CHECK_FIELD(sci_settings_record, echo, 0x1029a7 - 0x1029a2);
CHECK_FIELD(sci_settings_record, baud, 0x10acb2 - 0x10acb0);
CHECK_FIELD(sci_settings_record, echo, 0x10acb5 - 0x10acb0);
/* DUART channel B's settings at 0x102648, 9 bytes, which
 * parameter_record_read loads from +2 to +8 (0x452b4 to 0x452e4) and
 * duartb_settings_preset copies whole (0x45394): the fields the other
 * images assert, each where this image's code reads it (make
 * record-access), the baud in duartb_baud_from_settings (0x2dd00), echo
 * in duart_init (0x2e320), field_05 and eight_bits in their queries
 * (0x4432a, 0x445b8). */
CHECK_SIZE(duartb_settings_record, 0x1029b3 - 0x1029aa);
CHECK_FIELD(duartb_settings_record, baud, 0x1029ac - 0x1029aa);
CHECK_FIELD(duartb_settings_record, field_05, 0x1029af - 0x1029aa);
CHECK_FIELD(duartb_settings_record, eight_bits, 0x1029b0 - 0x1029aa);
CHECK_FIELD(duartb_settings_record, echo, 0x1029b2 - 0x1029aa);
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
CHECK_SIZE(sci_exchange_state, 0x103de4 - 0x103dac);
CHECK_FIELD(sci_exchange_state, read_token, 0x103db0 - 0x103dac);
CHECK_FIELD(sci_exchange_state, write_exchange, 0x103dc8 - 0x103dac);
CHECK_FIELD(sci_exchange_state, write_token, 0x103dcc - 0x103dac);
CHECK_SIZE(serial_ring, 0x103dfc - 0x103cfa);

/* The SCI's driver, sci at 0x103ee6, before qspi (0x103f04), as the
 * driver's code addresses it (make record-access): flow at 0x103ef3
 * (sci_transmit_allowed, 0x2e958), the echo at 0x103efe (0x2e5e0). */
CHECK_SIZE(sci_driver, 0x103f04 - 0x103ee6);
CHECK_FIELD(sci_driver, transmit_count, 0x103eea - 0x103ee6);
CHECK_FIELD(sci_driver, async_next, 0x103eec - 0x103ee6);
CHECK_FIELD(sci_driver, async_count, 0x103ef0 - 0x103ee6);
CHECK_FIELD(sci_driver, async_pending, 0x103ef1 - 0x103ee6);
CHECK_FIELD(sci_driver, flow, 0x103ef3 - 0x103ee6);
CHECK_FIELD(sci_driver, receive_errors, 0x103ef4 - 0x103ee6);
CHECK_FIELD(sci_driver, read_errors, 0x103ef5 - 0x103ee6);
CHECK_FIELD(sci_driver, xon_xoff, 0x103ef6 - 0x103ee6);
CHECK_FIELD(sci_driver, mark_bit, 0x103ef7 - 0x103ee6);
CHECK_FIELD(sci_driver, seven_bits, 0x103ef8 - 0x103ee6);
CHECK_FIELD(sci_driver, reconfiguring, 0x103ef9 - 0x103ee6);
CHECK_FIELD(sci_driver, async_allowed, 0x103efa - 0x103ee6);
CHECK_FIELD(sci_driver, released_for_timecode, 0x103efb - 0x103ee6);
CHECK_FIELD(sci_driver, baud_constant, 0x103efc - 0x103ee6);
CHECK_FIELD(sci_driver, echo, 0x103efe - 0x103ee6);
CHECK_FIELD(sci_driver, last_terminator, 0x103f02 - 0x103ee6);
CHECK_FIELD(sci_driver, field_0c, 0x103ef2 - 0x103ee6);
CHECK_FIELD(sci_driver, field_1d, 0x103f03 - 0x103ee6);
CHECK_SIZE(qspi_state, 0x103f52 - 0x103f04);
CHECK_FIELD(qspi_state, waiting, 0x103f08 - 0x103f04);
CHECK_FIELD(qspi_state, busy, 0x103f48 - 0x103f04);
CHECK_FIELD(qspi_state, receive_buffer, 0x103f4a - 0x103f04);
CHECK_FIELD(qspi_state, running, 0x103f51 - 0x103f04);
CHECK_FIELD(qspi_state, copy_pending, 0x103f50 - 0x103f04);
CHECK_FIELD(qspi_state, field_00, 0x103f04 - 0x103f04);
CHECK_FIELD(qspi_state, read, 0x103f06 - 0x103f04);
CHECK_FIELD(qspi_state, receive_count, 0x103f4e - 0x103f04);
CHECK_FIELD(qspi_state, write, 0x103f05 - 0x103f04);

CHECK_SIZE(eeprom_lock_token, 0x102740 - 0x102730);
CHECK_FIELD(eeprom_lock_token, owner, 0x102738 - 0x102730);
CHECK_FIELD(eeprom_lock_token, depth, 0x10273c - 0x102730);
CHECK_FIELD(eeprom_lock_token, exchange, 0x102734 - 0x102730);
CHECK_FIELD(eeprom_lock_token, status, 0x102730 - 0x102730);

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
CHECK_SIZE(psos_configuration, 0x103452 - 0x1033f0);
CHECK_FIELD(psos_configuration, data_end, 0x1033f4 - 0x1033f0);
CHECK_FIELD(psos_configuration, region_2_start, 0x1033fc - 0x1033f0);
CHECK_FIELD(psos_configuration, task_records, 0x103408 - 0x1033f0);
CHECK_FIELD(psos_configuration, exchange_records, 0x10340a - 0x1033f0);
CHECK_FIELD(psos_configuration, message_buffers, 0x10340c - 0x1033f0);
CHECK_FIELD(psos_configuration, last_device, 0x103412 - 0x1033f0);
CHECK_FIELD(psos_configuration, io_table, 0x103414 - 0x1033f0);
CHECK_FIELD(psos_configuration, supervisor_stack_size, 0x103418 - 0x1033f0);
CHECK_FIELD(psos_configuration, root_entry, 0x10341c - 0x1033f0);
CHECK_FIELD(psos_configuration, root_priority, 0x103424 - 0x1033f0);
CHECK_FIELD(psos_configuration, start_hook, 0x103426 - 0x1033f0);
CHECK_FIELD(psos_configuration, delete_hook, 0x10342a - 0x1033f0);
CHECK_FIELD(psos_configuration, debug_hooks, 0x103432 - 0x1033f0);
CHECK_FIELD(psos_configuration, task_hooks, 0x103436 - 0x1033f0);
CHECK_FIELD(psos_configuration, data_area, 0x1033f0 - 0x1033f0);
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

CHECK_SIZE(io_device_jumps[4], 0x1033f0 - 0x103360);
CHECK_SIZE(io_jump, 6);

/* task_table: the task at 0x102ed2 + 6n, its state at 0x102ed0 + 6n,
 * 17 entries up to task_count. */
CHECK_FIELD(task_table_entry, task, 0x10345e - 0x10345c);
CHECK_SIZE(task_table_entry[17], 0x1034c2 - 0x10345c);
CHECK_FIELD(task_table_entry, state, 0x10345c - 0x10345c);

CHECK_SIZE(spoo_request, 0x10371a - 0x103702);
CHECK_FIELD(spoo_request, function, 0x10370a - 0x103702);
CHECK_FIELD(spoo_request, arguments, 0x10370e - 0x103702);
CHECK_SIZE(early_status_events, 0x103a30 - 0x103a28);

CHECK_FIELD(early_status_events, events, 0x02);

CHECK_SIZE(efc_gain_values, 0x1032da - 0x1032c6);
CHECK_FIELD(efc_gain_values, reciprocal_ti, 0x1032ce - 0x1032c6);
CHECK_FIELD(efc_gain_values, reciprocal_tau, 0x1032d2 - 0x1032c6);
CHECK_FIELD(efc_gain_values, step_limit, 0x1032d6 - 0x1032c6);
CHECK_SIZE(efc_range_state, 0x1026e1 - 0x1026de);
CHECK_FIELD(efc_range_state, near_end_checks, 0x1026e0 - 0x1026de);
CHECK_FIELD(efc_range_state, clamped, 0x1026de - 0x1026de);
CHECK_FIELD(efc_range_state, near_end, 0x1026df - 0x1026de);

CHECK_SIZE(gps_command_mailbox, 0x1032f0 - 0x1032da);
CHECK_FIELD(gps_command_mailbox, arguments, 0x1032dc - 0x1032da);
CHECK_FIELD(gps_command_mailbox, request, 0x1032ec - 0x1032da);
CHECK_FIELD(gps_command_mailbox, code, 0x1032da - 0x1032da);

/* adc_monitors at 0x1021e4, eight, efc_range_state after them */
CHECK_SIZE(adc_monitor, 0x10258e - 0x10255e);
CHECK_SIZE(adc_monitor[8], 0x1026de - 0x10255e);
CHECK_FIELD(adc_monitor, value, 0x102586 - 0x10255e);
CHECK_FIELD(adc_monitor, valid, 0x10258a - 0x10255e);
CHECK_FIELD(adc_monitor, sample_index, 0x10258b - 0x10255e);
CHECK_FIELD(adc_monitor, out_of_tolerance, 0x1025bc - 0x10258e);
CHECK_FIELD(adc_monitor, out_of_range_count, 0x10258d - 0x10255e);
CHECK_SIZE(hmon_channel_entry, 0x31b3c - 0x31b12);
CHECK_SIZE(hmon_channel_entry[8], 0x31c62 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, add, 0x31b16 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, check, 0x31b1a - 0x31b12);
CHECK_FIELD(hmon_channel_entry, field_0c, 0x31b1e - 0x31b12);
CHECK_FIELD(hmon_channel_entry, report, 0x31b22 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, minimum, 0x31b26 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, maximum, 0x31b2a - 0x31b12);
CHECK_FIELD(hmon_channel_entry, nominal, 0x31b2e - 0x31b12);
CHECK_FIELD(hmon_channel_entry, name, 0x31b32 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, events, 0x31b36 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, bit, 0x31b38 - 0x31b12);
CHECK_FIELD(hmon_channel_entry, field_28, 0x31b3a - 0x31b12);
/* gps_request_step at 0x10215e */
CHECK_SIZE(gps_request_code_word, 0x102160 - 0x10215e);
CHECK_SIZE(gps_script_step, 0x102546 - 0x102532);
CHECK_FIELD(gps_script_step, argument_mode, 0x102534 - 0x102532);
CHECK_FIELD(gps_script_step, arguments, 0x102536 - 0x102532);

/* selftest at 0x101192, each field where this image's code addresses
 * it, the 58503A 3704's layout: the latch a byte at +0x0c
 * (latch_bits_set, 0x236ca), summary at +0x0e and eeprom at +0x10
 * (selftest_summary, 0x27914 on), the interpolator's from +0x18,
 * selftest_abort at +0x20 (0x1011b2), lamp_testing at +0x36 (lamp_test,
 * 0x28702), the result at +0x3a and the test at +0x46 (selftest_run). */
CHECK_SIZE(selftest_results, 0x48);
CHECK_FIELD(selftest_results, uart, 0x0a);
CHECK_FIELD(selftest_results, latch, 0x10119e - 0x101192);
CHECK_FIELD(selftest_results, summary, 0x1011a0 - 0x101192);
CHECK_FIELD(selftest_results, eeprom, 0x1011a2 - 0x101192);
CHECK_FIELD(selftest_results, fpga, 0x1011a4 - 0x101192);
CHECK_FIELD(selftest_results, gps, 0x1011a6 - 0x101192);
CHECK_FIELD(selftest_results, interpolator, 0x1011aa - 0x101192);
CHECK_FIELD(selftest_results, interpolator_calibrated, 0x1011ac - 0x101192);
CHECK_FIELD(selftest_results, interpolator_tries, 0x1011ad - 0x101192);
CHECK_FIELD(selftest_results, interpolator_readings, 0x1011ae - 0x101192);
CHECK_FIELD(selftest_results, abort, 0x1011b2 - 0x101192);
CHECK_FIELD(selftest_results, running, 0x1011b3 - 0x101192);
CHECK_FIELD(selftest_results, power, 0x1011b4 - 0x101192);
CHECK_FIELD(selftest_results, internal_reference, 0x1011b6 - 0x101192);
CHECK_FIELD(selftest_results, qspi, 0x1011b8 - 0x101192);
CHECK_FIELD(selftest_results, lamp_testing, 0x1011c8 - 0x101192);
CHECK_FIELD(selftest_results, passed, 0x1011ca - 0x101192);
CHECK_FIELD(selftest_results, result, 0x1011cc - 0x101192);
CHECK_FIELD(selftest_results, details, 0x1011d0 - 0x101192);
CHECK_FIELD(selftest_results, test, 0x1011d8 - 0x101192);

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

/* command_log at 0x101e02: read and write at +0x3fc and +0x400
 * (command_log_add, 0x53788 to 0x537c8), entries of 0x14 bytes
 * (0x53788). */
CHECK_SIZE(gps_command_log_entry, 0x14);
CHECK_FIELD(gps_command_log_entry, sender, 0x00);
CHECK_FIELD(gps_command_log_entry, code, 0x02);
CHECK_FIELD(gps_command_log_entry, message, 0x04);
CHECK_FIELD(gps_command_log_entry, argument_text, 0x06);
CHECK_FIELD(gps_command_log_entry, query, 0x09);
CHECK_FIELD(gps_command_log_entry, from_clock, 0x0a);
CHECK_FIELD(gps_command_log_entry, date, 0x0c);
CHECK_FIELD(gps_command_log_entry, time, 0x10);
CHECK_SIZE(gps_command_log, 0x102206 - 0x101e02);
CHECK_FIELD(gps_command_log, read, 0x1021fe - 0x101e02);
CHECK_FIELD(gps_command_log, write, 0x102202 - 0x101e02);
CHECK_FIELD(gps_command_log, entries, 0x101e02 - 0x101e02);

CHECK_SIZE(packed_datetime, 8);
CHECK_FIELD(packed_datetime, date, 4);

CHECK_SIZE(regression_record, 0x1a);
CHECK_FIELD(regression_record, weights, 0x0a);
CHECK_FIELD(regression_record, rms, 0x16);

CHECK_SIZE(three_point_fit, 0x10);
CHECK_FIELD(three_point_fit, vertex_x, 8);

CHECK_SIZE(lifetime_state, 0x10270a - 0x1026fe);
CHECK_FIELD(lifetime_state, date, 0x102702 - 0x1026fe);
CHECK_FIELD(lifetime_state, count, 0x102706 - 0x1026fe);
CHECK_FIELD(lifetime_state, efc, 0x1026fe - 0x1026fe);

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

CHECK_SIZE(efc_calibration_fields, 0x103360 - 0x10332a);
CHECK_FIELD(efc_calibration_fields, point, 0x10332b - 0x10332a);
CHECK_FIELD(efc_calibration_fields, field_02, 0x10332c - 0x10332a);
CHECK_FIELD(efc_calibration_fields, frequency_offset, 0x10332e - 0x10332a);
CHECK_FIELD(efc_calibration_fields, adc_low, 0x103356 - 0x10332a);
CHECK_FIELD(efc_calibration_fields, adc_high, 0x10335a - 0x10332a);
CHECK_FIELD(efc_calibration_fields, settle, 0x10335e - 0x10332a);

CHECK_SIZE(ptest_run, 0x102a5e - 0x102a54);
CHECK_FIELD(ptest_run, passes, 0x102a55 - 0x102a54);
CHECK_FIELD(ptest_run, start_uptime, 0x102a56 - 0x102a54);
CHECK_FIELD(ptest_run, end_uptime, 0x102a5a - 0x102a54);

CHECK_SIZE(calibration_block, 64);
CHECK_FIELD(calibration_block, time_offset, 6);
/* save_calibration_block's frame, the block at -64 (0x41748 to
 * 0x4179e) */
CHECK_FIELD(calibration_block, tc_coefficient, 0x40 - 0x3e);
CHECK_FIELD(calibration_block, field_0a, 0x40 - 0x36);
CHECK_FIELD(calibration_block, efc_dac_per_adc, 0x40 - 0x34);
CHECK_FIELD(calibration_block, efc_a, 0x40 - 0x30);
CHECK_FIELD(calibration_block, efc_b, 0x40 - 0x2c);
CHECK_FIELD(calibration_block, tc_coefficient_second, 0x40 - 0x28);
CHECK_FIELD(calibration_block, oscillator, 0x40 - 0x24);
/* efc_calibration_defaults' and efc_calibration_limits' index, the type
 * times 12, and their reads at +0, +4 and +8 (0x40b82 to 0x40b8a, 0x40cc0
 * to 0x40cd4) */
CHECK_SIZE(efc_oscillator_entry, 12);
CHECK_FIELD(efc_oscillator_entry, efc_a_minimum, 4);
CHECK_FIELD(efc_oscillator_entry, efc_a_maximum, 8);
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

CHECK_SIZE(aging_curve_state, 0x102a54 - 0x102a2a);
CHECK_FIELD(aging_curve_state, minimum_weight, 0x102a2b - 0x102a2a);
CHECK_FIELD(aging_curve_state, oldest_time, 0x102a2c - 0x102a2a);
CHECK_FIELD(aging_curve_state, newest_time, 0x102a30 - 0x102a2a);
CHECK_FIELD(aging_curve_state, a, 0x102a3c - 0x102a2a);
CHECK_FIELD(aging_curve_state, b, 0x102a40 - 0x102a2a);
CHECK_FIELD(aging_curve_state, c, 0x102a44 - 0x102a2a);
CHECK_FIELD(aging_curve_state, rms, 0x102a4c - 0x102a2a);
CHECK_FIELD(aging_curve_state, predicted_uncertainty, 0x102a50 - 0x102a2a);

/* gps_state: pllp_task copies 0x187 longs and a word of it to
 * preserved_region (0x4ce0a); its records where the Z3817A 3752's Oncore
 * code, the 58503A 3704's, reads them (gps, 0x100e66), the status where
 * the 58503B's @@Ba and @@Ea descriptors unpack it (0x1012b4, gps at
 * 0x1011e0). */
CHECK_SIZE(gps_state, 0x187 * 4 + 2);
CHECK_FIELD(gps_state, status_received, 0x1012b2 - 0x1011e0);
CHECK_FIELD(gps_state, status, 0x1012b4 - 0x1011e0);
CHECK_FIELD(gps_state, fix_mode, 0x10162e - 0x1011e0);
CHECK_FIELD(gps_state, field_44f, 0x10162f - 0x1011e0);
CHECK_FIELD(gps_state, receiver_id_received, 0x101630 - 0x1011e0);
CHECK_FIELD(gps_state, receiver_id, 0x101631 - 0x1011e0);
CHECK_FIELD(gps_state, field_566, 0x101746 - 0x1011e0);
CHECK_FIELD(gps_state, field_568, 0x101748 - 0x1011e0);
CHECK_FIELD(gps_state, field_569, 0x101749 - 0x1011e0);
CHECK_FIELD(gps_state, field_56a, 0x10174a - 0x1011e0);
CHECK_FIELD(gps_state, field_56c, 0x10174c - 0x1011e0);
CHECK_FIELD(gps_state, field_56d, 0x10174d - 0x1011e0);
CHECK_FIELD(gps_state, average_count, 0x10174e - 0x1011e0);
CHECK_FIELD(gps_state, average_latitude, 0x101752 - 0x1011e0);
CHECK_FIELD(gps_state, average_longitude, 0x10175a - 0x1011e0);
CHECK_FIELD(gps_state, average_height_gps, 0x101762 - 0x1011e0);
CHECK_FIELD(gps_state, average_height_msl, 0x10176a - 0x1011e0);
CHECK_FIELD(gps_state, references, 0x101772 - 0x1011e0);
CHECK_FIELD(gps_state, surveying, 0x10179e - 0x1011e0);
CHECK_FIELD(gps_state, field_5bf, 0x10179f - 0x1011e0);
CHECK_FIELD(gps_state, field_5c0, 0x1017a0 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c1, 0x1017a1 - 0x1011e0);
CHECK_FIELD(gps_state, leap_second_reported, 0x1017a2 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c3, 0x1017a3 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c4, 0x1017a4 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c5, 0x1017a5 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c6, 0x1017a6 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c8, 0x1017a8 - 0x1011e0);
CHECK_FIELD(gps_state, field_5c9, 0x1017a9 - 0x1011e0);
CHECK_FIELD(gps_state, field_5ca, 0x1017aa - 0x1011e0);
CHECK_FIELD(gps_state, field_5cb, 0x1017ab - 0x1011e0);
CHECK_FIELD(gps_state, field_5cc, 0x1017ac - 0x1011e0);
CHECK_FIELD(gps_state, field_5d0, 0x1017b0 - 0x1011e0);
CHECK_FIELD(gps_state, field_5d1, 0x1017b1 - 0x1011e0);
CHECK_FIELD(gps_state, field_5d2, 0x1017b2 - 0x1011e0);
CHECK_FIELD(gps_state, field_5d3, 0x1017b3 - 0x1011e0);
CHECK_FIELD(gps_state, leap_second_pending, 0x1017b4 - 0x1011e0);
CHECK_FIELD(gps_state, time_status, 0x1017b5 - 0x1011e0);
CHECK_FIELD(gps_state, utc_offset_received, 0x1017b6 - 0x1011e0);
CHECK_FIELD(gps_state, field_5d7, 0x1017b7 - 0x1011e0);
CHECK_FIELD(gps_state, raim_assessment, 0x1017b8 - 0x1011e0);
CHECK_FIELD(gps_state, pps_check_disabled, 0x1017b9 - 0x1011e0);
CHECK_FIELD(gps_state, receiver_health_failed, 0x1017ba - 0x1011e0);
CHECK_FIELD(gps_state, field_5db, 0x1017bb - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_enabled, 0x1017bc - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_state, 0x1017bd - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_query, 0x1017be - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_time, 0x1017c2 - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_error, 0x1017c6 - 0x1011e0);
CHECK_FIELD(gps_state, command_monitor_failed, 0x1017c7 - 0x1011e0);
CHECK_FIELD(gps_state, status_monitor_enabled, 0x1017c8 - 0x1011e0);
CHECK_FIELD(gps_state, status_monitor_state, 0x1017c9 - 0x1011e0);
CHECK_FIELD(gps_state, status_monitor_time, 0x1017ca - 0x1011e0);
CHECK_FIELD(gps_state, field_5ee, 0x1017ce - 0x1011e0);
CHECK_FIELD(gps_state, field_5f2, 0x1017d2 - 0x1011e0);
CHECK_FIELD(gps_state, status_last_time, 0x1017d6 - 0x1011e0);
CHECK_FIELD(gps_state, status_repeat_count, 0x1017da - 0x1011e0);
CHECK_FIELD(gps_state, status_monitor_timed_out, 0x1017db - 0x1011e0);
CHECK_FIELD(gps_state, status_monitor_failed, 0x1017dc - 0x1011e0);
CHECK_FIELD(gps_state, field_5fd, 0x1017dd - 0x1011e0);
CHECK_FIELD(gps_state, field_5fe, 0x1017de - 0x1011e0);
CHECK_FIELD(gps_state, field_602, 0x1017e2 - 0x1011e0);
CHECK_FIELD(gps_state, channel_count, 0x1017e6 - 0x1011e0);
CHECK_FIELD(gps_state, field_607, 0x1017e7 - 0x1011e0);
CHECK_FIELD(gps_state, field_608, 0x1017e8 - 0x1011e0);
CHECK_FIELD(gps_state, field_609, 0x1017e9 - 0x1011e0);
CHECK_FIELD(gps_state, tracking_log, 0x1017ea - 0x1011e0);
CHECK_FIELD(gps_state, tracking_log_active, 0x1017eb - 0x1011e0);
CHECK_FIELD(gps_state, track_words, 0x1017ec - 0x1011e0);
CHECK_FIELD(gps_state, channel_word, 0x1017f8 - 0x1011e0);
CHECK_FIELD(gps_state, channel_word_lowest, 0x1017fc - 0x1011e0);
CHECK_FIELD(gps_state, visibility, 0x1012fe - 0x1011e0);
/* sky_statistics, as sky_statistics_clear clears it (0x5421c to
 * 0x5426c): the map, then the hours, the ranges and the last hour, the
 * averages, the average and its count, and the Doppler bins. */
CHECK_SIZE(sky_statistics_record, 0x10252a - 0x102206);
CHECK_FIELD(sky_statistics_record, hours_seen, 0x10248e - 0x102206);
CHECK_FIELD(sky_statistics_record, used_range, 0x102492 - 0x102206);
CHECK_FIELD(sky_statistics_record, hour, 0x1024aa - 0x102206);
CHECK_FIELD(sky_statistics_record, unused_average, 0x1024ab - 0x102206);
CHECK_FIELD(sky_statistics_record, average, 0x1024c4 - 0x102206);
CHECK_FIELD(sky_statistics_record, samples, 0x1024c8 - 0x102206);
CHECK_FIELD(sky_statistics_record, doppler_bins, 0x1024ca - 0x102206);
/* The message table's records (0x59284 to 0x59770) and their readers
 * and writers (make record-access). */
CHECK_FIELD(gps_state, xdop_table, 0x10137d - 0x1011e0);
CHECK_FIELD(gps_state, almanac, 0x1013ca - 0x1011e0);
CHECK_FIELD(gps_state, corrections, 0x10149a - 0x1011e0);
CHECK_FIELD(gps_state, correction_average, 0x1014da - 0x1011e0);
CHECK_FIELD(gps_state, correction_deviation, 0x1014e2 - 0x1011e0);
CHECK_FIELD(gps_state, extension, 0x1014ee - 0x1011e0);
CHECK_FIELD(gps_state, raim, 0x10152e - 0x1011e0);
CHECK_FIELD(gps_state, raim.channels, 0x10154a - 0x1011e0);
CHECK_FIELD(gps_state, raim_received, 0x10157a - 0x1011e0);
CHECK_FIELD(gps_state, raim_alarm_date, 0x10157c - 0x1011e0);
CHECK_FIELD(gps_state, raim_alarm_time, 0x101580 - 0x1011e0);
CHECK_FIELD(gps_state, raim_alarm_sigma, 0x101584 - 0x1011e0);
CHECK_FIELD(gps_state, raim_alarm_channels, 0x101586 - 0x1011e0);
CHECK_FIELD(gps_state, raim_last_sigma, 0x1015b6 - 0x1011e0);
CHECK_FIELD(gps_state, self_test_response, 0x1015b8 - 0x1011e0);
CHECK_FIELD(gps_state, sz_received, 0x1015ba - 0x1011e0);
CHECK_FIELD(gps_state, self_test_mask, 0x1015bc - 0x1011e0);
CHECK_FIELD(gps_state, antenna_sense, 0x1015be - 0x1011e0);
CHECK_FIELD(gps_state, self_test, 0x1015c0 - 0x1011e0);
/* the tps commit's hold position, 0x5d1c6 */
/* the commits' status: the date and time (0x5cc5e), the position
 * (0x5cec6), the DOP and its type and the channels (0x5cf20) */
/* gps_print_best_combinations' type, count and mode (0x53906 on) */
/* the time RAIM record from gps_print_time_raim's base (0x53be4) */
/* gps_print_extension's first and last fields (0x539e2 on) */
/* gps_init clears +0x5ee (0x57c22) */

CHECK_SIZE(oncore_status_fields, 0x47);

CHECK_SIZE(oncore_visibility_fields, 0x7a);

CHECK_SIZE(oncore_xdop_table_fields, 75);

CHECK_SIZE(oncore_almanac_status_fields, 16);

CHECK_SIZE(oncore_corrections_fields, 64);

/* to raim_received (0x57456) */
CHECK_SIZE(oncore_raim_fields, 0x358 - 0x30c);

/* In the Z3815A 4010, the two reference records, 0x56c5c's (0x101540,
 * 0x101556), where this image's are at 0x101772 and 0x101788 */

CHECK_SIZE(gps_reference_record, 0x101788 - 0x101772);
CHECK_FIELD(gps_reference_record, override, 2);
CHECK_FIELD(gps_reference_record, pps_dead, 3);
CHECK_FIELD(gps_reference_record, elapsed, 4);
CHECK_FIELD(gps_reference_record, validation_start, 8);
CHECK_FIELD(gps_reference_record, field_0c, 0x0c);
CHECK_FIELD(gps_reference_record, validation_time, 0x10);
/* in the 58503A 3704, 0x4b858 stores the GPS's at 0x1013a6 and 0x1013a8;
 * in the Z3815A 4010, the offsets checked */
CHECK_FIELD(gps_reference_record, hysteresis, 0x101784 - 0x101772);
CHECK_FIELD(gps_reference_record, hysteresis_margin, 0x101786 - 0x101772);

/* The loop's state at 0x102bb2: pllp_task copies 0x1bf longs and a word
 * of it to preserved_region (0x4ce24); each field where this image's
 * code reaches it (make record-access); the hold causes a word
 * (diag_pr_hold_cause). */
CHECK_SIZE(pll_state, 0x1bf * 4 + 2);
CHECK_FIELD(pll_state, previous_stage, 0x102bb3 - 0x102bb2);
CHECK_FIELD(pll_state, powerup_countdown, 0x102bb4 - 0x102bb2);
CHECK_FIELD(pll_state, powerup_state, 0x102bb5 - 0x102bb2);
CHECK_FIELD(pll_state, powerup_reference_lost, 0x102bb6 - 0x102bb2);
CHECK_FIELD(pll_state, field_005, 0x102bb7 - 0x102bb2);
CHECK_FIELD(pll_state, reference_check_state, 0x102bb8 - 0x102bb2);
CHECK_FIELD(pll_state, field_007, 0x102bb9 - 0x102bb2);
CHECK_FIELD(pll_state, startup_state, 0x102bbe - 0x102bb2);
CHECK_FIELD(pll_state, field_00d, 0x102bbf - 0x102bb2);
CHECK_FIELD(pll_state, normal_state, 0x102bc0 - 0x102bb2);
CHECK_FIELD(pll_state, field_00f, 0x102bc1 - 0x102bb2);
CHECK_FIELD(pll_state, hold_causes, 0x102bc2 - 0x102bb2);
CHECK_FIELD(pll_state, holdover_state, 0x102bc4 - 0x102bb2);
CHECK_FIELD(pll_state, manual, 0x102bc5 - 0x102bb2);
CHECK_FIELD(pll_state, ti_limit_override, 0x102bc6 - 0x102bb2);
CHECK_FIELD(pll_state, field_015, 0x102bc7 - 0x102bb2);
CHECK_FIELD(pll_state, holdover_efc_offset, 0x102bc8 - 0x102bb2);
CHECK_FIELD(pll_state, holdover_efc_step, 0x102bcc - 0x102bb2);
CHECK_FIELD(pll_state, recovery_countdown, 0x102bd0 - 0x102bb2);
CHECK_FIELD(pll_state, recovery_state, 0x102bd1 - 0x102bb2);
CHECK_FIELD(pll_state, coarse_state, 0x102bd2 - 0x102bb2);
CHECK_FIELD(pll_state, fine_state, 0x102bd3 - 0x102bb2);
CHECK_FIELD(pll_state, frequency_adjusted, 0x102bd4 - 0x102bb2);
CHECK_FIELD(pll_state, fine_index, 0x102bd5 - 0x102bb2);
CHECK_FIELD(pll_state, fine_limit, 0x102bd6 - 0x102bb2);
CHECK_FIELD(pll_state, field_025, 0x102bd7 - 0x102bb2);
CHECK_FIELD(pll_state, fine_retries, 0x102bd8 - 0x102bb2);
CHECK_FIELD(pll_state, fine_d, 0x102bda - 0x102bb2);
CHECK_FIELD(pll_state, aging_state, 0x102bde - 0x102bb2);
CHECK_FIELD(pll_state, field_02d, 0x102bdf - 0x102bb2);
CHECK_FIELD(pll_state, gps_diag_state, 0x102be2 - 0x102bb2);
CHECK_FIELD(pll_state, gps_diag_substate, 0x102be3 - 0x102bb2);
CHECK_FIELD(pll_state, gps_init_state, 0x102be4 - 0x102bb2);
CHECK_FIELD(pll_state, field_033, 0x102be5 - 0x102bb2);
CHECK_FIELD(pll_state, leap_check_timer, 0x102be6 - 0x102bb2);
CHECK_FIELD(pll_state, leap_check_requested, 0x102be8 - 0x102bb2);
CHECK_FIELD(pll_state, leap_check_state, 0x102be9 - 0x102bb2);
CHECK_FIELD(pll_state, leap_ack_state, 0x102bea - 0x102bb2);
CHECK_FIELD(pll_state, leap_ack_requested, 0x102beb - 0x102bb2);
CHECK_FIELD(pll_state, leap_ack_done, 0x102bec - 0x102bb2);
CHECK_FIELD(pll_state, field_03b, 0x102bed - 0x102bb2);
CHECK_FIELD(pll_state, leap_ack_again, 0x102bee - 0x102bb2);
CHECK_FIELD(pll_state, field_03d, 0x102bef - 0x102bb2);
CHECK_FIELD(pll_state, efc, 0x102bf0 - 0x102bb2);
CHECK_FIELD(pll_state, tfom, 0x102bf4 - 0x102bb2);
CHECK_FIELD(pll_state, predicted_uncertainty, 0x102bf8 - 0x102bb2);
CHECK_FIELD(pll_state, measured_uncertainty, 0x102bfc - 0x102bb2);
CHECK_FIELD(pll_state, field_04e, 0x102c00 - 0x102bb2);
CHECK_FIELD(pll_state, frequency_tolerance, 0x102c08 - 0x102bb2);
CHECK_FIELD(pll_state, efc_history, 0x102c0c - 0x102bb2);
CHECK_FIELD(pll_state, aging_term, 0x10327a - 0x102bb2);
CHECK_FIELD(pll_state, loop_efc_step, 0x10327e - 0x102bb2);
CHECK_FIELD(pll_state, ti_filtered, 0x103282 - 0x102bb2);
CHECK_FIELD(pll_state, efc_base, 0x103286 - 0x102bb2);
CHECK_FIELD(pll_state, loop_time_constant, 0x10328a - 0x102bb2);
CHECK_FIELD(pll_state, field_6dc, 0x10328e - 0x102bb2);
CHECK_FIELD(pll_state, led_gps_lock, 0x10328f - 0x102bb2);
CHECK_FIELD(pll_state, led_holdover, 0x103290 - 0x102bb2);
CHECK_FIELD(pll_state, field_6df, 0x103291 - 0x102bb2);
CHECK_FIELD(pll_state, holdover_duration, 0x103292 - 0x102bb2);
CHECK_FIELD(pll_state, holdover_start_uptime, 0x103296 - 0x102bb2);
CHECK_FIELD(pll_state, reference_uptime, 0x10329a - 0x102bb2);
CHECK_FIELD(pll_state, reference_time, 0x10329e - 0x102bb2);
CHECK_FIELD(pll_state, efc_control_passes, 0x1032a2 - 0x102bb2);
CHECK_FIELD(pll_state, reference_seconds, 0x1032a6 - 0x102bb2);
CHECK_FIELD(pll_state, fpga_configuration, 0x1032a7 - 0x102bb2);
CHECK_FIELD(pll_state, fpga_loaded, 0x1032a8 - 0x102bb2);
CHECK_FIELD(pll_state, loop_running, 0x1032a9 - 0x102bb2);
CHECK_FIELD(pll_state, sync_requested, 0x1032aa - 0x102bb2);
CHECK_FIELD(pll_state, field_6f9, 0x1032ab - 0x102bb2);
CHECK_FIELD(pll_state, field_6fa, 0x1032ac - 0x102bb2);
CHECK_FIELD(pll_state, field_6fb, 0x1032ad - 0x102bb2);
CHECK_FIELD(pll_state, block_seconds, 0x1032ae - 0x102bb2);
CHECK_FIELD(pll_state, measured_uncertainty_valid, 0x1032af - 0x102bb2);
CHECK_SIZE(pll_hold_cause, 2);

/* the history 0x66e bytes, at 0x102c0c: efc_history_update's (0x46186)
 * and the fits' indexed arrays of 64, measured_uncertainty_update's fit
 * arrays */
CHECK_SIZE(efc_ring, 0x10327a - 0x102c0c);
CHECK_FIELD(efc_ring, stop_ptr, 0x102c0d - 0x102c0c);
CHECK_FIELD(efc_ring, loop_settled, 0x102c0e - 0x102c0c);
CHECK_FIELD(efc_ring, field_003, 0x102c0f - 0x102c0c);
CHECK_FIELD(efc_ring, field_004, 0x102c10 - 0x102c0c);
CHECK_FIELD(efc_ring, summed_count, 0x102c12 - 0x102c0c);
CHECK_FIELD(efc_ring, averages, 0x102c14 - 0x102c0c);
CHECK_FIELD(efc_ring, replaced_average, 0x102d14 - 0x102c0c);
CHECK_FIELD(efc_ring, weights, 0x102d18 - 0x102c0c);
CHECK_FIELD(efc_ring, replaced_weight, 0x102d58 - 0x102c0c);
CHECK_FIELD(efc_ring, field_14d, 0x102d59 - 0x102c0c);
CHECK_FIELD(efc_ring, times, 0x102d5a - 0x102c0c);
CHECK_FIELD(efc_ring, replaced_time, 0x102e5a - 0x102c0c);
CHECK_FIELD(efc_ring, logs, 0x102e5e - 0x102c0c);
CHECK_FIELD(efc_ring, fit_a_history, 0x102f5e - 0x102c0c);
CHECK_FIELD(efc_ring, fit_b_history, 0x10305e - 0x102c0c);
CHECK_FIELD(efc_ring, fit_c_history, 0x10315e - 0x102c0c);
CHECK_FIELD(efc_ring, next_entry_uptime, 0x10325e - 0x102c0c);
CHECK_FIELD(efc_ring, efc_sum, 0x103262 - 0x102c0c);
CHECK_FIELD(efc_ring, efc_first, 0x103266 - 0x102c0c);
CHECK_FIELD(efc_ring, field_65e, 0x10326a - 0x102c0c);
CHECK_FIELD(efc_ring, fit_a, 0x10326e - 0x102c0c);
CHECK_FIELD(efc_ring, fit_b, 0x103272 - 0x102c0c);
CHECK_FIELD(efc_ring, fit_c, 0x103276 - 0x102c0c);

/* Records the printf calls name; in the 58503A 3704 @@Cj's decoder's
 * table (0x52620) lays the receiver ID out as the Z3805A 3543B's: 34
 * bytes, then 9 lines of 27. */

/* The receiver ID's lines (0x5262c): 12 bytes each, the table ending
 * at 0x52698, its tenth; the model number's and the options' texts at
 * 0x1012e7 and 0x101353, in gps.receiver_id from 0x101259. */

CHECK_SIZE(gps_statistics, 0x40);
CHECK_FIELD(gps_statistics, field_0e, 0xe);
CHECK_FIELD(gps_statistics, responses_read, 0x22);
CHECK_FIELD(gps_statistics, e_retries, 0x26);
CHECK_FIELD(gps_statistics, t_retries, 0x28);
CHECK_FIELD(gps_statistics, raim_detected, 0x101954 - 0x10192a);
CHECK_FIELD(gps_statistics, raim_alarms, 0x101956 - 0x10192a);
CHECK_FIELD(gps_statistics, unknown_response, 0x101960 - 0x10192a);
CHECK_FIELD(gps_statistics, frame_errors, 0x101962 - 0x10192a);
CHECK_FIELD(gps_statistics, checksum_errors, 0x3a);
CHECK_FIELD(gps_statistics, command_monitor_failures, 0x101966 - 0x10192a);
CHECK_FIELD(gps_statistics, status_monitor_failures, 0x3e);

CHECK_SIZE(exception_record, 0x100f20 - 0x100f0e);
CHECK_FIELD(exception_record, name, 0x100f0f - 0x100f0e);
CHECK_FIELD(exception_record, sr, 0x100f14 - 0x100f0e);
CHECK_FIELD(exception_record, pc, 0x100f16 - 0x100f0e);
CHECK_FIELD(exception_record, vector, 0x100f1a - 0x100f0e);
CHECK_FIELD(exception_record, address, 0x100f1c - 0x100f0e);
CHECK_FIELD(exception_record, pending, 0x100f0e - 0x100f0e);

/* The pool: five entries from 0x10372a to 0x1037ac. */
CHECK_SIZE(klok_entry, (0x1037ac - 0x10372a) / 5);
CHECK_FIELD(klok_entry, function, 5 * 4);
CHECK_FIELD(klok_entry, event_class, 6 * 4);
/* log_read's page, log_page_buffer at 0x102452: entry n's fields at
 * 0x102456, 0x10245a, 0x10245e, ... + n * 0x14. */
CHECK_SIZE(log_page, 0x1027e8 - 0x1027a8);
CHECK_SIZE(log_entry, 0x14);
CHECK_FIELD(log_page, entries, 0x1027ac - 0x1027a8);
CHECK_FIELD(log_entry, format, 0x1027b4 - 0x1027ac);
CHECK_FIELD(log_entry, argument_1, 0x1027b8 - 0x1027ac);
CHECK_SIZE(fpga_bit, 4);
/* interpolator_calibrate reads and writes param[0] to param[8]. */
CHECK_SIZE(interpolator_calibration, 9);
CHECK_FIELD(interpolator_calibration, result_valid, 4);
CHECK_FIELD(interpolator_calibration, reading_high, 8);
CHECK_FIELD(interpolator_calibration, calibrated, 0x102a21 - 0x102a20);
CHECK_FIELD(interpolator_calibration, reading_low, 0x102a27 - 0x102a20);

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
CHECK_SIZE(nv_settings_fields, 0x1029a1 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, elevation_mask, 0x1028d8 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, antenna_delay, 0x1028dc - 0x1028d6);
CHECK_FIELD(nv_settings_fields, douglas_duration, 0x1028e0 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, douglas_duration_infinite, 0x1028e4 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, tracking_log, 0x1028e5 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, questionable_hysteresis, 0x1028e6 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, external_questionable_hysteresis,
            0x1028ea - 0x1028d6);
CHECK_FIELD(nv_settings_fields, holdover_status_threshold, 0x1028ee - 0x1028d6);
CHECK_FIELD(nv_settings_fields, limit_threshold, 0x1028f2 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, holdover_limit_threshold, 0x1028f6 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, holdover_limit_state, 0x1028fa - 0x1028d6);
CHECK_FIELD(nv_settings_fields, douglas_state, 0x1028fb - 0x1028d6);
CHECK_FIELD(nv_settings_fields, position, 0x1028fc - 0x1028d6);
CHECK_FIELD(nv_settings_fields, ignored_satellites, 0x102908 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, time_zone, 0x10290c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, prediction_duration, 0x102914 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, leap_date, 0x102918 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, leap_minute_length, 0x10291c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, leap_pending, 0x102920 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, raim_state, 0x102921 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, raim_threshold, 0x102924 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, holdover_measurement_threshold,
            0x10292c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, status_enable, 0x102930 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, status_positive_transition,
            0x10294c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, status_negative_transition,
            0x102960 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, log_wrap, 0x102974 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, questionable_user, 0x102975 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, serial_restricted, 0x102976 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, pulse_state, 0x102977 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, pulse_period, 0x102978 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, pulse_start_time, 0x10297c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, pulse_start_date, 0x102988 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, field_be, 0x102994 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, output_state, 0x102998 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, format_data, 0x102999 - 0x1028d6);
CHECK_FIELD(nv_settings_fields, memory_save, 0x10299a - 0x1028d6);
CHECK_FIELD(nv_settings_fields, antenna_current_alarm, 0x10299b - 0x1028d6);
CHECK_FIELD(nv_settings_fields, douglas_state_powerup, 0x10299c - 0x1028d6);
CHECK_FIELD(nv_settings_fields, tcode_format, 0x10299d - 0x1028d6);
CHECK_FIELD(nv_settings_fields, gps_utc, 0x10299e - 0x1028d6);
CHECK_FIELD(nv_settings_fields, pforth_at_startup, 0x10299f - 0x1028d6);
CHECK_FIELD(nv_settings_fields, debug_output, 0x1029a0 - 0x1028d6);

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

/* The time-interval counter's two records: the reading at the Z3815A
 * 4010's 0x10268c, its flag at 0x102694 and armed flag at 0x102698; the
 * configuration at the Z3817A 3752's 0x1024fc, ending where aging_curve
 * begins (0x102512), each field where counter_configuration_init
 * (0x459bc to 0x459d2), counter_arm (0x45786 to 0x457fa) and
 * counter_read (0x45896) reach it. */
CHECK_SIZE(interval_reading_fields, 0x1029fb - 0x1029ee);
CHECK_FIELD(interval_reading_fields, valid, 0x1029f6 - 0x1029ee);
CHECK_FIELD(interval_reading_fields, errors, 9);
CHECK_FIELD(interval_reading_fields, tag, 10);
CHECK_FIELD(interval_reading_fields, armed, 0x1029fa - 0x1029ee);
CHECK_FIELD(interval_reading_fields, value, 0x1029ee - 0x1029ee);

/* The counter's configuration, the 58503A 3704's: counter_configuration
 * (0x102a00), interpolator_calibration_state after it (0x102a20);
 * sync_request's stores (0x44a66) and counter_calibrate's (0x4568e). */
CHECK_SIZE(counter_configuration_fields, 0x102a20 - 0x102a00);
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

/* The status snapshots, 0x19e bytes each: the screen's first at
 * 0x101abc, the second 0x19e on (status_screen_print, 0x51a4c); each
 * field where status_snapshot (0x50ee0 to 0x5120c) stores it through
 * A2, the record (make record-access). */
CHECK_SIZE(status_snapshot_record, 0x19e);
CHECK_FIELD(status_snapshot_record, mode, 0x101abd - 0x101abc);
CHECK_FIELD(status_snapshot_record, powerup_suffix, 0x101abe - 0x101abc);
CHECK_FIELD(status_snapshot_record, recovery_suffix, 0x101abf - 0x101abc);
CHECK_FIELD(status_snapshot_record, tfom_code, 0x101ac0 - 0x101abc);
CHECK_FIELD(status_snapshot_record, ffom, 0x101ac2 - 0x101abc);
CHECK_FIELD(status_snapshot_record, ffom_valid, 0x101ac3 - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval, 0x101ac4 - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval_valid, 0x101acc - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval_errors, 0x101acd - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval_tag, 0x101ace - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval_mean, 0x101ad0 - 0x101abc);
CHECK_FIELD(status_snapshot_record, interval_mean_flag, 0x101ad4 - 0x101abc);
CHECK_FIELD(status_snapshot_record, holdover_waiting_reason,
            0x101ad6 - 0x101abc);
CHECK_FIELD(status_snapshot_record, holdover_limit_state, 0x101ad7 - 0x101abc);
CHECK_FIELD(status_snapshot_record, holdover_limit_threshold,
            0x101ad8 - 0x101abc);
CHECK_FIELD(status_snapshot_record, holdover_duration, 0x101adc - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_present, 0x101ae0 - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_present_valid, 0x101ae4 - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_present_resolution,
            0x101ae5 - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_predicted, 0x101ae6 - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_valid,
            0x101aea - 0x101abc);
CHECK_FIELD(status_snapshot_record, kenneth_predicted_resolution,
            0x101aeb - 0x101abc);
CHECK_FIELD(status_snapshot_record, channel_count, 0x101aec - 0x101abc);
CHECK_FIELD(status_snapshot_record, field_031, 0x101aed - 0x101abc);
CHECK_FIELD(status_snapshot_record, satellites, 0x101aee - 0x101abc);
CHECK_FIELD(status_snapshot_record, not_tracked, 0x101b18 - 0x101abc);
CHECK_FIELD(status_snapshot_record, tracked, 0x101bb8 - 0x101abc);
CHECK_FIELD(status_snapshot_record, status_received, 0x101c08 - 0x101abc);
CHECK_FIELD(status_snapshot_record, not_tracked_count, 0x101c09 - 0x101abc);
CHECK_FIELD(status_snapshot_record, tracked_count, 0x101c0a - 0x101abc);
CHECK_FIELD(status_snapshot_record, pll_field_6f9, 0x101c0b - 0x101abc);
CHECK_FIELD(status_snapshot_record, reference_valid, 0x101c0c - 0x101abc);
CHECK_FIELD(status_snapshot_record, pps_dead, 0x101c0d - 0x101abc);
CHECK_FIELD(status_snapshot_record, status_code, 0x101c0e - 0x101abc);
CHECK_FIELD(status_snapshot_record, field_153, 0x101c0f - 0x101abc);
CHECK_FIELD(status_snapshot_record, reference_elapsed, 0x101c10 - 0x101abc);
CHECK_FIELD(status_snapshot_record, external_valid, 0x101c14 - 0x101abc);
CHECK_FIELD(status_snapshot_record, external_pps_dead, 0x101c15 - 0x101abc);
CHECK_FIELD(status_snapshot_record, external_status_code, 0x101c16 - 0x101abc);
CHECK_FIELD(status_snapshot_record, field_15b, 0x101c17 - 0x101abc);
CHECK_FIELD(status_snapshot_record, external_elapsed, 0x101c18 - 0x101abc);
CHECK_FIELD(status_snapshot_record, gps_flags, 0x101c1c - 0x101abc);
CHECK_FIELD(status_snapshot_record, fix_mode, 0x101c1e - 0x101abc);
CHECK_FIELD(status_snapshot_record, receiver_status, 0x101c1f - 0x101abc);
CHECK_FIELD(status_snapshot_record, time, 0x101c20 - 0x101abc);
CHECK_FIELD(status_snapshot_record, date, 0x101c24 - 0x101abc);
CHECK_FIELD(status_snapshot_record, time_zone, 0x101c28 - 0x101abc);
CHECK_FIELD(status_snapshot_record, from_clock, 0x101c30 - 0x101abc);
CHECK_FIELD(status_snapshot_record, utc_time, 0x101c31 - 0x101abc);
CHECK_FIELD(status_snapshot_record, time_status, 0x101c32 - 0x101abc);
CHECK_FIELD(status_snapshot_record, leap_second_pending, 0x101c33 - 0x101abc);
CHECK_FIELD(status_snapshot_record, pps_cable_delay, 0x101c34 - 0x101abc);
CHECK_FIELD(status_snapshot_record, antenna_sense, 0x101c38 - 0x101abc);
CHECK_FIELD(status_snapshot_record, mask_angle, 0x101c39 - 0x101abc);
CHECK_FIELD(status_snapshot_record, position_hold, 0x101c3a - 0x101abc);
CHECK_FIELD(status_snapshot_record, douglas_duration_infinite,
            0x101c3b - 0x101abc);
CHECK_FIELD(status_snapshot_record, gps_field_5bf, 0x101c3c - 0x101abc);
CHECK_FIELD(status_snapshot_record, gps_field_5c0, 0x101c3d - 0x101abc);
CHECK_FIELD(status_snapshot_record, position_source, 0x101c3e - 0x101abc);
CHECK_FIELD(status_snapshot_record, position, 0x101c42 - 0x101abc);
CHECK_FIELD(status_snapshot_record, height_type, 0x101c4e - 0x101abc);
CHECK_FIELD(status_snapshot_record, field_193, 0x101c4f - 0x101abc);
CHECK_FIELD(status_snapshot_record, average_count, 0x101c50 - 0x101abc);
CHECK_FIELD(status_snapshot_record, douglas_duration, 0x101c54 - 0x101abc);
CHECK_FIELD(status_snapshot_record, hardware_condition, 0x101c58 - 0x101abc);
/* status_screen_card_lines, six entries of 6 bytes from 0x59716 */
CHECK_SIZE(status_screen_card_line[6], 0x5973a - 0x59716);
CHECK_FIELD(status_screen_card_line, text, 0x5971e - 0x5971c);
CHECK_SIZE(satellite_list_entry, 10);
CHECK_SIZE(satellite_sets, 0x60 - 0x36);

/* The preserved region at 0x100000, the 0xc04 bytes record_checksum
 * sums (system_init, 0x22dd0): gps's copy at 0x100004, pll's at
 * 0x10067c, adc_monitors' at 0x100a6a and retained_block's 0x19 bytes at
 * 0x100bea (0x22e06 to 0x22e48). */
/* preserved_region_fields, the 58503B's: record_checksum's length
 * (0x4ce4c) and pllp_task's copies (0x4cdf8 to 0x4ce3e). */
CHECK_SIZE(preserved_region_fields, 0xebe);
CHECK_FIELD(preserved_region_fields, gps, 4);
CHECK_FIELD(preserved_region_fields, pll, 0x622);
CHECK_FIELD(preserved_region_fields, adc_monitors, 0xd20);
CHECK_FIELD(preserved_region_fields, values, 0xea0);

/* the 0x180 bytes pllp_task and system_init copy from 0x101e78: the
 * ADC monitors, which monitor_average_add indexes by 0x30 (0x21d40) */

/* The clock record at 0x100c6e: the routines' offsets, at the address
 * the derivation from the 58503A's code gives (layout_addresses.py). */
CHECK_SIZE(clock_record, 0x100f5b - 0x100f28);
CHECK_FIELD(clock_record, uptime, 0x100f2a - 0x100f28);
CHECK_FIELD(clock_record, time, 0x100f2e - 0x100f28);
CHECK_FIELD(clock_record, date, 0x100f32 - 0x100f28);
CHECK_FIELD(clock_record, month_days, 0x100f36 - 0x100f28);
CHECK_FIELD(clock_record, gps_time, 0x100f42 - 0x100f28);
CHECK_FIELD(clock_record, gps_date, 0x100f46 - 0x100f28);
CHECK_FIELD(clock_record, gps_month_days, 0x100f4a - 0x100f28);
CHECK_FIELD(clock_record, minute_length, 0x100f56 - 0x100f28);
CHECK_FIELD(clock_record, set, 0x100f57 - 0x100f28);
CHECK_FIELD(clock_record, gps_utc_offset, 0x100f58 - 0x100f28);
CHECK_FIELD(clock_record, gps_utc_offset_valid, 0x100f5a - 0x100f28);

/* pps_message at 0x100e64, pps_exchange after the cells pps_init clears */
CHECK_SIZE(pps_exchange_message, 0x101134 - 0x10111c);
CHECK_FIELD(pps_exchange_message, gps_time, 0x101124 - 0x10111c);
CHECK_FIELD(pps_exchange_message, gps_date, 0x101128 - 0x10111c);

CHECK_SIZE(gps_process_state, 1);

/* the 58503A 3704's three status snapshots, 0x1016ae (status_screen_print,
 * 0x4b6ec), 0x10181e and 0x101988, with the two cells at 0x101818
 * between the first two; each field where status_snapshot (0x4ac56)
 * stores it */

/* status_screen_mode_lines' five entries, the last a terminator */
CHECK_SIZE(status_screen_mode_line[5], 0x4d050 - 0x4d032);
CHECK_FIELD(status_screen_mode_line, text, 0x02);

/* the event log's write page at 0x10240c, its number after it */
CHECK_SIZE(log_page, 0x1027a2 - 0x102762);

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
CHECK_SIZE(satellite_list_entry, 10);

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
