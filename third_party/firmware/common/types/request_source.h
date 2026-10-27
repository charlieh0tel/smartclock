/* Who asked for a change or an EEPROM write, request_source, as the images'
 * smartclock.h declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C". Where a comment cites code, it is the
 * Z3805A 3543B's, which the 58503A's copy of the comment came from. */

/* request_source: who asked for a change or an EEPROM write, and so
 * whether a failure goes to the status system.  Handed down through
 * pllc_request.source and pll_scpi_source to the descriptors' validate,
 * changed, done and action routines, and to nv_save,
 * save_parameter_record, save_calibration_block, log_erase and
 * douglas_finish_nv_argument, ending in status_report_from.  Its jump
 * table at status_report_from+0x16 reports for 0 and 2 alone; any value above 4
 * is silent too (status_report_from+0x6).  Nothing in the image passes 1, 3 or
 * 5: they are named by what the code does with them.  Indexing
 * scpi_response_texts[2] and its siblings by source suggests 1 was a
 * second SCPI port, as the 59551A has (097-59551-02, *CLS, "Context
 * Dependencies": each I/O port has its own error queue); unproved. */
typedef enum __attribute__((packed)) request_source {
  /* A SCPI command: return_zero(call->input), the only port.
   * Reports; pllc signals task_scpi when done (get_setup_vars+0x14) and sends
   * what the GPS must hear (pll_scpi_command+0x3c); status_preset_register sets
   * registers for it alone (status_preset_register+0xe); status_preset_done's
   * restores (status_preset_done+0x3e, status_preset_done+0x72,
   * status_preset_done+0xbe, status_preset_done+0xe4). */
  REQUEST_SOURCE_SCPI = 0,
  /* Not reported, not sent to the GPS; the second slot of the per-source
   * response arrays. */
  REQUEST_SOURCE_SILENT_1 = 1,
  /* Startup: nv_restore's save of the defaults and its configuration
   * lost (nv_restore+0x92, nv_restore+0x9c).  Reports. */
  REQUEST_SOURCE_STARTUP = 2,
  /* Not reported: status_report_from's own case, returning 0
   * (status_report_from+0x2c). */
  REQUEST_SOURCE_SILENT_3 = 3,
  /* The firmware on its own: root_task's log erase after a failed
   * EEPROM self-test (root_task+0x74), the leap second's and the loop's NV
   * saves (leap_event_create+0x20, leap_event_create+0x78, pllp_task+0x44a),
   * douglas_finish_nv_argument's when a survey ends (gps_init_sequence+0x53c,
   * pllp_task+0x8e).  Not reported. */
  REQUEST_SOURCE_INTERNAL = 4,
  /* Not reported, but pllc sends its requests to the GPS as for
   * REQUEST_SOURCE_SCPI (pll_scpi_command+0x42). */
  REQUEST_SOURCE_SILENT_TO_GPS = 5,
} request_source;
