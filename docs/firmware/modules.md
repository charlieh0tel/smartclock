# Modules

The plan for regrouping the Z3805A 3543B's hand-written C
(`third_party/firmware/rewritten/z3805a-3543b/`, README.md, "Hand-written
C") by subject.  `tools/ghidra/names/modules.tsv` puts each of the 1301
functions the exported C (`third_party/firmware/decompiled/z3805a-3543b.c`)
gives as C in a module, 387 hand-written among them; the assembly
routines, the pForth kernel (`0x18ed6` to `0x1ad27`) and the installer's
copies are left out.  The evidence is the exported C's direct calls,
read from each function's body; a call through a pointer, a descriptor's
hook or a vector, is not among them, and those that matter are listed
under "Indirect calls".

## Layers

Four, lowest first.  A function calls its own layer or a lower one, but
for the calls under "Calls the wrong way".

| Layer | Functions | What it holds |
| ----- | --------: | ------------- |
| platform | 180 | start-up, the CPU, the kernel's calls and tasks, the debug output, the C library and arithmetic |
| devices | 131 | a module to a chip or a QSPI device |
| services | 194 | what the tasks share: the spooler, the status model, the EEPROM's records and logs, time, the 1PPS and the health monitor |
| applications | 796 | the tasks that make the receiver: SCPI, the loop, the GPS engine's protocol, the self-test, the status screen and the console |

Where the approved layering named a module and the code says otherwise
(these four names approved 2026-10-21: `platform/debug`,
`services/status`, `applications/main`, `applications/selftest`):

- **`platform/debug`, not `services/messages`.**  `fatal_error`
  (`0x1f176`), `task_debug_message` (`0x1eff6`), `task_dmessage`
  (`0x1f056`) and `debug_sprintf` (`0x1ef46`) are called by every driver
  (the DUART's, the SCI's, the QSPI's, the EEPROM's, the FPGA's and the
  interval counter's: 34 calls), the kernel reports and libc
  (`float_exception_report`, `program_end_report`).  As a service, every
  one would be a call the wrong way; below the drivers, three are (they
  reach channel B's polled output).  `exception_name` and
  `print_exception` go with it from `os.c`: they print through
  `printf_polled`.
- **`services/spool` keeps only the spooler.**  Its task and its
  deferred text (`deferred_vprintf`, `deferred_flush`); `spool.c`'s
  fatal and debug messages are `platform/debug`'s.
- **`services/status`, not `applications/scpi/status`.**  The status
  model and its error queue are raised from below the SCPI task:
  `status_happening` (`0x2f53c`) by the health monitor, the event log,
  the EEPROM, the loop and the GPS sequences, `status_report_from`
  (`0x2bf00`) by NV, the records and the loop's actions (45 callers
  outside the module).  IEEE 488.2's status byte and service request
  enable stay with the console port object, `applications/scpi/port`.
- **`applications/main`.**  `system_init` (`0x12bb4`) and `root_task`
  (`0x1304a`) initialize every layer and create every task, so they are
  the top, not `platform/startup`; the start-up keeps the reset path,
  the QSM, the GPT and the periodic interrupt.
- **`applications/selftest`, not a service.**  The self-tests reach the
  GPS engine (`selftest_gps_with_lamp` calls `gps_selftest_run`) and
  only applications call them: `selftest_run` from
  `gps_diag_sequence`, `selftest_print_failure` from `pllp_task`.
- **`devices/dac` beside `devices/adc`.**  The hex DAC and the EFC DAC
  are QSPI devices 2 and 7, the ADC device 1, the FPGA 6 and 9 to 12;
  0, 4 and 5 have no caller (structures.md, "The QSPI's devices").
- **`devices/eeprom` out of `records.c`.**  The chip, its lock and its
  page writes are a device; the records are what is written.
- **`devices/duartb` is DUART channel B, `devices/duarta` channel
  A**, named as the firmware's `drta_`/`duarta_` and `drtb_`/`duartb_`
  routines are.  The split is real: each has its own ring, signals and
  interrupt routine (`duarta_isr` `0x1e766`, `duartb_isr` `0x1e7f6`)
  and its own self-tests (`duart_test_tx_a`, `duart_loopback_test_b`,
  ...); `devices/duart` keeps what the two share, the output port,
  `duart_test` and the reset.
- **`services/pps`.**  The 1PPS capture's work (`pps_interrupt`,
  `0x12db6`) ticks the clock, builds the time code and posts the time
  to `pps_exchange`, which `klok_task` waits on: a service, not a timer.
- **`applications/pll/pll_scpi`.**  The pllc task's settings server is
  the firmware's own `pll_scpi` ("pll_scpi: finish_scpi_cmd() --
  unknown structure type", `0x4306a`); with it go the descriptors'
  validators, clamps and hooks it runs.
- **`applications/pll/gps_control`.**  `gps_init_sequence` (`0x46838`),
  `gps_leap_sequence`, `gps_diag_sequence` and their kin are
  `pllp_task`'s steps, among the loop's code (`0x46620` to `0x471fc`),
  that drive the GPS engine through `gpsx_exchange`; the GPS task's own
  code is `applications/gps`.
- **`applications/screen`.**  The status screen and its snapshot,
  printed by the GPS task and `:SYSTem:PRINt?`.

## Platform

| Module | Functions | Subject; entry points; data |
| ------ | --------: | --------------------------- |
| `boot` | 5 | the boot code, reset at `0x550` (`restart.md`), its own program: `boot_duart_early_init`, `boot_unpack_installer`; `primary_unpack_records`, the primary's copy |
| `startup` | 7 | the reset path: `primary_start` (`0x12d6e`), `qsm_init`, `gpt_init`, `periodic_interrupt_enable`, `enter_installer`, `shutdown`; `sim` |
| `cpu` | 2 | `set_interrupt_mask` (38 callers), `enter_supervisor_mode` |
| `os` | 79 | pSOS: `psos_config_init`, the service and I/O wrappers (`psos_pause`, `psos_wait_v`, `psos_send_x`, ...), mutexes, memory, exception hooks, and the `:DIAG:OS` reports (`os_process_report`, `os_memory_report`, `task_stack_report`); `psos_config`, `task_table` |
| `debug` | 13 | debug and fatal output: `task_dmessage`, `task_debug_message`, `debug_sprintf`, `debug_printf`, `printf_polled`, `fatal_error`, `print_exception`; `debug_enabled`, `crash_message` |
| `string` | 11 | `memcpy`, `strcat`, `strncpy`, ...; `string_upper`, `skip_spaces`, `string_trim_right` |
| `stdlib` | 15 | `abs`, `div`, `atoi`, `rand`, `qsort`, `setjmp`, `longjmp`, `program_exit`; `atexit_functions` |
| `stdio` | 10 | `vsprintf`, `sprintf`, `doprnt`, `double_to_digits`, `printf`, and `console_write`/`console_read`, pSOS device 0 |
| `math` | 10 | `fabs`, `frexp`, `modf`, `log`, `sqrt`, `pow`, `float_raise`; `errno` |
| `arithmetic` | 28 | the C parts of the arithmetic: 64-bit (`int64_add`, `int64_to_double`), rounding, powers of ten, bit counts, `random_dither`.  The soft float itself (`softfloat.md`) is assembly |

## Devices

| Module | Functions | Subject; entry points; data |
| ------ | --------: | --------------------------- |
| `duart` | 8 | what both channels share: `duart_early_init`, `duart_output_bits` (8 callers), `duart_test`, `duart_stop`; `duart`, `duart_shadow` |
| `duarta` | 8 | channel A, the link to the GPS engine: `drta_send_message`, `drta_get_byte`, `duarta_isr`, its self-tests; `duarta_ring` |
| `duartb` | 23 | channel B, the console and SCPI port (device 0): `drtb_putc`, `drtb_gets`, `duartb_reconfigure`, `console_write_polled`, `duartb_isr`, its self-tests and settings hooks; `duartb`, `duartb_ring` |
| `sci` | 15 | the SCI, device 3: `sci_putc`, `sci_gets`, `sci_reconfigure`, `sci_send_async`; `sci`, `sci_ring` |
| `qspi` | 9 | the QSPI and its owner queue: `qspi_transfer`, `qspi_send`, `qspi_write`, `qspi_read`, `qspi_isr`; `qspi` |
| `adc` | 9 | `adc_read` and the `read_adc_*` readings |
| `dac` | 6 | `hdac_set`, `hdac_write_all`, `hdac_update`, `efc_write`, `efc_write_dithered`; `hdac_shadow` |
| `fpga` | 17 | loading and verifying the configuration (`fpga_setup`, `fpga_program`, `fpga_verify`) and its registers (`fpga_write`, `fpga_read`, `fpga_set_bit`); `fpga_bit_table`, `fconf_regs` |
| `ti` | 28 | the time-interval counter and interpolator (`interval.md`): `counter_init`, `counter_arm`, `counter_read`, `counter_calibrate`, `interpolator_calibrate`, `sync_request`, `counter_selftest`; `counter_configuration`, `interval_reading` |
| `eeprom` | 7 | `eeprom_init`, `eeprom_lock`, `eeprom_write` (8 callers); `eepx_exchange` |
| `timers` | 2 | GPT pins: `pwma_force_level`, `gpt_input_capture_2_interrupt` |
| `leds` | 1 | `leds_update`, the lamp latch at `0x300000` |

## Services

| Module | Functions | Subject; entry points; data |
| ------ | --------: | --------------------------- |
| `spool` | 5 | the spoo task: `spoo_task_create`, `spoo_run`, `deferred_vprintf`, `deferred_flush`; `spoo_exchange`, `deferred_text` |
| `status` | 47 | the status model (structures.md's status registers): `status_init`, `status_happening`, `status_report_from`, the registers' get and set, the error queue, the early-happening buffers; `status_object`, `status_conditions` |
| `nv` | 9 | the NV register: `nv_restore`, `nv_save`, `nv_slot_valid`, `clear_nv`; `nv_settings`, `nv_records` |
| `records` | 14 | the parameter, identification, lifetime and calibration records: `parameter_record_read`, `identification_record_write`, `lifetime_update`, `save_calibration_block`; `lifetime`, `idn_model` |
| `log` | 27 | the event log (`log_init`, `log_write`, `log_event`, `log_read`) and the reset-cause log at `0x4001c0` (`log_reset_cause`, `reset_record_write`, `log_entry_text`); `log_entries`, `eeprom_reset_records` |
| `calendar` | 22 | date and time arithmetic: `datetime_add`, `datetime_compare`, `date_to_mjd`, `utc_to_local`, `gps_seconds`, `oncore_date_pack` |
| `clock` | 15 | the clock record: `clock_init`, `clock_tick`, `clock_set`, `get_date_time` (12 callers), `get_uptime`, `time_valid`; `clock`.  Its checksum and advance routines are assembly (`0x134a8` to `0x138e4`) |
| `leap` | 14 | the leap second and offset step: `leap_event_create`, `leap_event_schedule`, the minute arms, `leap_second_near`, `time_next_second` |
| `klok` | 10 | the timed-event list and its task: `klok_task_create`, `clk_do`, `klok_cancel`, `klok_run_due`; `klok_entries`, `klok_mutex` |
| `pps` | 4 | `pps_init`, `pps_interrupt`, `pps_wait_time`; `pps_exchange` |
| `timecode` | 9 | the T1/T2 time code and the ES message: `timecode_init`, `tcode_request`, `timecode_update`, `es_build`, and their output compares (`timecode_schedule`, `es_schedule`); `tcode_message`, `es_message` |
| `monitors` | 18 | the hmon task: `hmon_task`, the supply, EFC and 1PPS checks, `adc_average`, `oven_warmed_up`, `efc_clamp` (the loop's stages call it), `watchdog_report`; `adc_monitors`, `gps_pps_dead` |

## Applications

| Module | Functions | Subject; entry points; data |
| ------ | --------: | --------------------------- |
| `main` | 2 | `system_init`, `root_task`; the task ids and exchanges |
| `selftest` | 52 | the self-test table at `0x18458` and its checks: `selftest_run`, `selftest_all`, `selftest_print_failure`, the `*_with_lamp` checks, `lamp_test`, `supplies_test`; `selftest` |
| `scpi/engine` | 23 | the SCPI task and interpreter: `scpi_task_start`, `scpi_task`, `scpi_interpret`, `scpi_parse_message_unit`, `scpi_call_handler`, the arena, and the handlers' helpers `return_zero` (117 calls) and `scpi_query_failed` (68) |
| `scpi/port` | 17 | the console port object, method table `0x29dd4`: `scpi_console_port`, `console_port_read`, `scpi_prompt`, the status byte and SRE methods, `status_prompt_refresh`; `status_byte` |
| `scpi/lexer` | 9 | `scpi_scan_token`, numbers, units and suffixes (`scpi_apply_suffix`) |
| `scpi/tree` | 9 | keyword lookup and header paths: `scpi_node_find`, `scpi_header_resolve` |
| `scpi/parameters` | 19 | the `scpi_parse_*` parsers and the numeric get and set (`scpi_numeric_set`, 15 callers) |
| `scpi/validators` | 17 | the numeric descriptors' validators and step routines, which pllc's `validate_num` calls through the descriptors: the clamps, `round_float_to_step`, `load_long`, `load_double`; `report_clamped` |
| `scpi/convert` | 8 | numbers to and from text: `scpi_real_to_text`, `scpi_number_to_*` (with the BCD routines in assembly, `0x24d42` to `0x254fe`) |
| `scpi/format` | 21 | the response formatters, `scpi_format_response` |
| `scpi/requests` | 7 | the SCPI task's side of `pllc_exchange`: `pllc_request_a` to `_e` |
| `scpi/handlers/*` | 289 | a module to a root keyword: `common` (`*` commands, 13), `status` (20), `system` (22), `source_gpsystem` (32), `source_ptime` (19), `source_pulse` (10), `source_roscillator` (23), `output` (8), `led` (11), `format` (2), `sense` (2), `diagnostic` (48), `sample` (23, the test tree), `douglas` (7), `kenneth` (4), `robin` (18, with its parser), `stubs` (24, the four-letter entries that fail, `scpi/undocumented.md`), `misc` (3, `:DANA?`, which reads the error queue, and `:RTCM?`) |
| `pll/pllp` | 18 | the pllp task and its state: `pll_init`, `pllp_task`, `pll_check_reference`, `pll_stage_status`, `pll_mode`, `get_ffom`; `pll` |
| `pll/reading` | 3 | `interval_next_reading` (9 callers), the loop's one-second reading |
| `pll/stages` | 15 | `loop.md`'s stages: `powerup`, `coarse`, `fine`, `startup_pll`, `pll_normal`, `holdover`, `holdover_recovery` |
| `pll/aging` | 17 | the curv task's aging fit: `efc_history_update`, `curv_task`, `aging_fit` |
| `pll/pll_scpi` | 55 | the pllc task: `pll_scpi_receive`, `pll_scpi_command`, `validate_or_doaction`, `finish_scpi_cmd`, the clamps and the descriptors' hooks |
| `pll/gps_control` | 8 | `gpsx_send`, `gps_init_sequence`, `gps_leap_sequence`, `gps_diag_sequence`, `gps_selftest_run` |
| `gps/task` | 7 | the gpsm task: `gps_init`, `gpsm_task`, `gps_console_request`, `status_snapshot_update` |
| `gps/requests` | 16 | the request ring and the requests' text (`gps.md`, "Requests"): `gps_request_queue`, `gps_request_build`, `gps_message_supported` |
| `gps/oncore` | 31 | framing and decoding the engine's messages: `gps_receive`, `gps_response_decode`, the `oncore_*_message` decoders |
| `gps/receiver` | 20 | the receiver's state: status codes, command and RAIM monitors, the satellite sets, the position and survey |
| `gps/reports` | 20 | the GPS command log and the console's GPS reports (`gps_print_*`) |
| `screen` | 33 | the status screen: `status_snapshot`, `status_screen`, `status_screen_print` and their formatters |
| `console` | 97 | the pForth console (`console.md`): `console_init`, `diag_words_define` and the 86 `diag_*` words, the `ps`, `mem_rep` and `s_rep` words |

## Calls the wrong way

Eleven direct calls go from a lower layer to a higher one.

1. **The EEPROM raises a status condition.**  `eeprom_write`
   (`0x3124a`) and `eeprom_write_byte` (`0x31128`) call
   `status_happening` on a failed write (happening `I`).  A device
   reporting to a service; returning the failure, as `eeprom_write`
   also does, would leave the report to `services/records`, `nv` and
   `log`, its callers.
2. **Debug output polls channel B.**  `printf_polled` (`0x1df48`),
   `debug_write` (`0x1ef74`) and `debug_printf_polled` (`0x1ef90`) call
   `console_write_polled` (`0x1df12`), which writes through
   `duartb_putc_polled` (`0x1d680`).  The output a crash or an
   exception needs, with the kernel unusable, ends at the chip; a hook
   the serial port sets at start-up would invert it.
3. **The reset path starts the system.**  `primary_start` (`0x12d6e`)
   calls `system_init` (`0x12bb4`): the start-up hands over to the
   composition root, as it must.
4. **The start-up stops the DUART.**  `shutdown` (`0x120aa`) calls
   `duart_stop` (`0x1ebbc`).
5. **The status model prompts the SCPI port.**  `error_queue_push`
   (`0x285d4`) and `error_queue_pop` (`0x2869e`) call
   `status_prompt_refresh`, a method of the console port's status
   output object, and `status_init` (`0x2f480`) takes that object from
   `scpi_console_port`.  The status model and the port are wired
   together; the object's methods (table `0x29dd4`) would serve as
   the callback were `status_init` handed it.
6. **The time code asks the loop.**  `timecode_update` (`0x13c50`), run
   at each 1PPS, calls `get_ffom` (`0x42a18`), which derives the FFOM
   from `pll.stage`, and passes `pll.tfom` to `tfom_code`.  The time
   code reads the loop's state from below it.

Data used the wrong way, without a call:

- `pll`, the loop's state, is read below it: `pll.efc` by
  `efc_write_dithered` (`0x2421c`), the EFC the hmon task writes;
  `pll.fpga_configuration` and `pll.fpga_loaded` by `fpga_setup`
  (`0x22d70`); `pll.led_gps_lock` and `pll.led_holdover` by
  `leds_update` (`0x464de`); `pll.efc` and `pll.stage` by
  `lifetime_event_run` and `lifetime_update` (`0x30b30`, `0x30b66`);
  `pll.stage` by `es_build` (`0x13a6a`) and `pll.tfom` by
  `timecode_update`.  Each wants its value handed down, or kept in
  its own module and written by the loop.
- `counter_wait_ready` (`0x4091a`, `devices/ti`) reads
  `gps_pps_dead` and `internal_pps_dead` (`0x101b6e`, `0x101b6f`),
  which the health monitor sets.
- `selftest`, the self-test's results, is written by the DUART's, the
  interval counter's and the monitors' own tests and by `nv_restore`.

## Indirect calls

Not in the direct calls above, and not counted:

- `exception_hook_get` (`0x118fe`, `platform/os`) returns the hook
  `console_init`'s task stores, `console_exception_hook`; the
  exception path calls up into the console through it.
- `spoo_task` (`services/spool`) runs what `spoo_run` posts:
  `scpi_task_create`, from `scpi_task_start` and `console_halt_hook`.
- `klok_run_due` (`services/klok`) runs the entries `clk_do` queues:
  `lifetime_event_run` and the leap arms, services both.
- `finish_scpi_cmd` and `validate_or_doaction` (`pll/pll_scpi`) run the
  descriptors' hooks: their own, the loop's and `services/status`'s
  (`status_preset_done`, `questionable_user_done`), none above them.

## The hand-written files

The hand-written C (`third_party/firmware/rewritten/z3805a-3543b/`)
follows the modules: one file to a module, named by it (`calendar.c`,
`devices/dac`'s `dac.c`, `platform/string`'s `string.c`), an
application's submodules prefixed (`scpi_lexer.c`, `pll_reading.c`),
and the SCPI commands by their tree or subtree, a file to a handler
group (`scpi_source_ptime.c` for `:SOURce:PTIMe`, `scpi_diagnostic.c`,
the root's unsupported four-letter entries in `scpi_unsupported.c`),
each with the hooks and actions pllc runs for its settings, which
`pllc.c` keeps only when no one command owns them; a module's
first function written by hand starts its file (`boot.c`, `cpu.c`,
`leds.c`, `pps.c`, `timers.c`).  What a group of
them shares, from the file they were split from, is in a header named
for it (`clock.h`, `duart.h`, `libc.h`, `monitors.h`, `records.h`,
`scpi.h`), keeping that file's prefix.

The installer's copies of the primary's functions (`installer::` in
the names file, by their twins' names prefixed `installer_`) are
written in a file named for their twin's module, prefixed, in the
folder `installer/`: `installer/installer_os.c`,
`installer/installer_duartb.c`, `installer/installer_status.c`, which the
exporter includes with the rest.  A copy
that reads nothing of the installer's own, and calls nothing but
other such bodies, shares its twin's body: the body moves to the
group's header (`libc.h`, `scpi.h`, `scpi_lexer.h`, `scpi_tree.h`,
`scpi_parameters.h`, `scpi_engine.h`, `status.h`, `startup.h`, `os.h`,
`duart.h`) as a static function both call, given its image's own
tables where it reads one (the lexer's class table, the negative
conditions).  The others are written out
again on the installer's globals and callees, and some are older than
the primary's (`installer_print_exception`,
`installer_identification_record_read`).

## Open

- `efc_clamp` both clamps the EFC to the DAC's range and reports
  events B and C; it could be the DAC's, were the report left to its
  callers.
- `mas_to_dms` and `dms_to_mas` serve the SCPI position handlers and the
  screen; they are `gps/receiver`'s for now.
- A descriptor's hook or action goes with its command's tree, from the
  record that points at it, or, when it keeps a module's own state,
  with that module, as its prefix says: `duartb_parity_restrict` and
  `duartb_eight_bits_restrict` keep channel B's settings and sit with
  `duartb_settings_done` in `devices/duartb`; `pll/pll_scpi` keeps
  what no one command owns.
