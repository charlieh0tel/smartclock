/* The FPGA's loading and verifying, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): fpga_setup,
 * which loads its first configuration, and fpga_verify.  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  pll's FPGA flags are docs/firmware/structures.md's. */

/* Loads the FPGA's first configuration, under the kernel, and marks it
 * loaded if it took (0x22d70). */
void fpga_setup(void)
{
	task_dmessage("setup fpga\r\n");
	if (fpga_program(0, 1)) {
		pll.fpga_load_pending = 0;
		pll.fpga_loaded = 1;
	}
}

/* The FPGA's configurations, which fpga_program and fpga_verify take
 * by number, and the chip fpga_verify checks. */
enum {
	FPGA_CONFIGURATIONS = 2,
	FPGA_VERIFY_CHIP = 1,
};

/* Verifies a configuration, 0 or 1, against the FPGA's readback; false
 * for another (0x23980). */
uint8_t fpga_verify(uint8_t configuration, char with_task_name)
{
	if (configuration >= FPGA_CONFIGURATIONS)
		return 0;
	return fpga_verify_chip(configuration, with_task_name, FPGA_VERIFY_CHIP);
}
