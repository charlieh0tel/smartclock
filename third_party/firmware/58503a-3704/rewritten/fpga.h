/* What the FPGA's loader shares with the system's start and the QSPI
 * (fpga.c, main.c, qspi.c): the FPGA's PROGRAM line, which system_init
 * pulses after a bad preserved region, and the bitstream's tail.  The exported
 * C includes the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The FPGA's PROGRAM line, port E's bit 6 (docs/firmware/outputs.md,
 * "The FPGA bitstreams"). */
static const uint8_t FPGA_PROGRAM = 0x40;

/* The words a bitstream's last bytes go out in, through the QSPI's FPGA
 * tail (fpga_program, qspi_fpga_tail_write). */
static const uint16_t FPGA_TAIL_WORDS = 2;
