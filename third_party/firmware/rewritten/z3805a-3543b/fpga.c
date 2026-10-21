/* The FPGA's loading and verifying, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): fpga_setup,
 * which loads its first configuration, fpga_verify and fpga_load_test.
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  A configuration is smartclock.h's
 * fpga_configuration_index, into the ROM table fpga_configurations;
 * pll's FPGA fields are docs/firmware/structures.md's. */

/* Loads the FPGA's first configuration, under the kernel, and marks it
 * loaded if it took (0x22d70). */
void fpga_setup(void)
{
	task_dmessage("setup fpga\r\n");
	if (fpga_program(FPGA_CONFIGURATION_COUNTER, 1)) {
		pll.fpga_configuration = FPGA_CONFIGURATION_COUNTER;
		pll.fpga_loaded = 1;
	}
}

/* Verifies a configuration against the FPGA's readback, the first chip;
 * false for one past fpga_configurations' last (0x23980). */
uint8_t fpga_verify(fpga_configuration_index configuration, char with_task_name)
{
	if (configuration > fpga_configurations.last)
		return 0;
	return fpga_verify_chip(configuration, with_task_name, FPGA_CHIP_FIRST);
}

/* The power-up FPGA test: loads the configuration fpga_setup loaded,
 * or the first if that is not below fpga_configurations' last, and
 * verifies it (0x18918).  The ROM's last is 1, so it is always the
 * first. */
uint8_t fpga_load_test(void)
{
	const fpga_configuration_index configuration =
		pll.fpga_configuration < fpga_configurations.last
			? pll.fpga_configuration
			: FPGA_CONFIGURATION_COUNTER;

	if (!fpga_program(configuration, 0))
		return 0;
	return fpga_verify(configuration, 0);
}
