/* The root's four-letter SCPI entries the firmware does not support
 * (:CALA? to :RWHO?, docs/scpi/README.md), each failing through
 * scpi_query_failed, and :RTCM?'s answer, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/stubs`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* 0: RTCM corrections are not supported (0x29b08). */
uint8_t rtcm_supported(void) { return 0; }
