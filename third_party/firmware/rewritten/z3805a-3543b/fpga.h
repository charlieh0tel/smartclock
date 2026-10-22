/* What the FPGA's loader shares with the system's start (fpga.c,
 * main.c): the FPGA's PROGRAM line, which system_init pulses after a
 * bad preserved region.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* The FPGA's PROGRAM line, port E's bit 6 (docs/firmware/outputs.md,
 * "The FPGA bitstreams"). */
static const uint8_t FPGA_PROGRAM = 0x40;
