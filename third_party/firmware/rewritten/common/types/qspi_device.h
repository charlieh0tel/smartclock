/* The QSPI's devices from 1, the same in every image: the members of
 * each image's qspi_device after its own device 0, whose smartclock.h
 * includes this header inside that enum (docs/firmware/rewrite.md,
 * "Shared C"). */
/* clang-format off */
  QSPI_ADC = 1,
  QSPI_HDAC = 2,
  QSPI_UNUSED_WORD = 4,
  QSPI_UNUSED_TWO_WORDS = 5,
  QSPI_FPGA_BLOCK = 6,
  QSPI_EFC = 7,
  QSPI_FPGA_TAIL = 9,
  QSPI_FPGA_WORD = 0xa,
  QSPI_FPGA_BYTE = 0xb,
  QSPI_FPGA_READBACK = 0xc,
 /* clang-format on */
