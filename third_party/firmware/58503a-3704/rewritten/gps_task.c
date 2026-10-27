/* The GPS task, gpsm: its state's start, the link's reset, and the steps
 * of its own a request or script runs in place of a message to the
 * engine (docs/firmware/gps.md; docs/firmware/structures.md).  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/gps/task`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Fills gps_request_step's arguments for @@Bn, the Time RAIM setup:
 * each second, the algorithm on or off and its alarm limit as set for
 * position hold or not, and the 1PPS on all the time (VPCommands.pdf,
 * Time RAIM Setup and Status) (0x4cfbc). */
void raim_request_arguments(void) {
  static const int32_t EACH_SECOND = 1;
  static const int32_t PULSE_ALWAYS = 1;
  /* nv_settings' RAIM settings in position hold, and out of it. */
  static const uint8_t IN_HOLD = 0;
  static const uint8_t OUT_OF_HOLD = 1;
  uint8_t setting = gps.position_hold == 1 ? IN_HOLD : OUT_OF_HOLD;

  long_array_set(EACH_SECOND, gps_request_step.arguments, 0);
  long_array_set((int8_t)nv_settings.raim_state[setting],
                 gps_request_step.arguments, 1);
  long_array_set(nv_settings.raim_threshold[setting],
                 gps_request_step.arguments, 2);
  long_array_set(PULSE_ALWAYS, gps_request_step.arguments, 3);
}

/* Takes a status snapshot at each @@Ba into status_snapshot_latest, or
 * at 03:14:07 and 06:28:15, pi and two pi, into status_snapshot_flagged
 * with a copy of the command log; stops for good when one of those
 * comes twice running (0x4d362). */
void status_snapshot_update(uint32_t time) {
  if (status_snapshots_frozen != 0) {
    return;
  }
  if (time == clock_pack(6, 28, 15) || time == clock_pack(3, 14, 7)) {
    if (time == status_snapshot_last_time) {
      status_snapshots_frozen = 1;
      return;
    }
    status_snapshot(&status_snapshot_flagged);
    command_log_snapshot = command_log;
    command_log_snapshot.read =
        command_log_snapshot.entries + (command_log.read - command_log.entries);
    command_log_snapshot.write = command_log_snapshot.entries +
                                 (command_log.write - command_log.entries);
  } else {
    status_snapshot(&status_snapshot_latest);
  }
  status_snapshot_last_time = time;
}

/* Decodes a complete response and starts framing the next. */
static void gps_task_read_response(void) {
  gps_response_decode(gps_response_index, gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}
