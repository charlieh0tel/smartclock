# Firmware

This tree is split by kind, not by image (`PLAN.md`, "Firmware
layout").  `images/` is what we were given: the vendors' ROM images
(`IMAGE.bin`), the console log one was rebuilt from, the EEPROM dumps
in `eeprom/` and the FPGA bitstreams in `fpga/`.  `decompiled/IMAGE/`
is what Ghidra made from an image, exported by `tools/ghidra/`.
`rewritten/` is what we wrote: `common/`, the C several images share (1011
functions and 232 static helpers, `make check-common`),
and `IMAGE/`, an image's own C, its type header and layout check in
`types/`.  `analysis/IMAGE/` is what we learned about each ROM: its
names files, modules, SCPI handlers and trees' rows, result names and
allowlists.  `include/` holds the image-independent headers (the
MC68331's, the 68681's, the kernel's signals), `ghidra/` the Ghidra
archive the exports come from, and the `Makefile` the checks and
generated files (`make -C third_party/firmware help`).  What is the
vendors' and what is ours is in `third_party/NOTICE`; how the work is
done, in `docs/firmware/`.  Unlike the rest of `third_party/`,
`decompiled/`, `rewritten/` and `analysis/` are not vendor material but
derived from the vendor images.

| Image | `images/` | `decompiled/` | `rewritten/` | `analysis/` |
|---|---|---|---|---|
| Z3805A 3543B | `z3805a-3543b.bin` | `z3805a-3543b/` | `z3805a-3543b/` | `z3805a-3543b/` |
| 58503A 3704 | `58503a-3704.bin` | `58503a-3704/` | `58503a-3704/` | `58503a-3704/` |
| Z3801A 3543 | `z3801a-3543.bin` | `z3801a-3543/` | `z3801a-3543/` | `z3801a-3543/` |
| 58503A 3633 | `58503a-3633.bin` | | | |
| 58503B 1.01.04 | `58503b-1.01.04.bin`, `58503b-L85376-console.log` | | | |
| Z3815A 4010 | `z3815a-4010.bin` | | | |
| Z3816A 4001 | `z3816a-4001.bin` | | | |
