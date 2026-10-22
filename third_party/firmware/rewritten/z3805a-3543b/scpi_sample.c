/* The SCPI :SAMPle tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/sample`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* :SAMPle's action: prints which of three it was (0x43426). */
void sample_action_print(int32_t index, int32_t source) {
  switch ((int8_t)index) {
    case 1:
      task_debug_message("sample action 1\r\n");
      break;
    case 2:
      task_debug_message("sample action 2\r\n");
      break;
    case 3:
      task_debug_message("sample action 3\r\n");
      break;
    default:
      break;
  }
}
