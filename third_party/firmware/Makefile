# The firmware's checks and generated files, run from here alone or
# delegated by name from the top Makefile:
#
#     make -C third_party/firmware TARGET [IMAGE=z3805a-3543b]
#
# A target of an image's runs for IMAGE, or without it for every image
# folder present: IMAGE/, beside IMAGE.bin, which holds that image's
# work (PLAN.md, "Decided 2026-10-22, the firmware layout").  Needs an
# m68k gcc and binutils, clang-format and uv; none is part of `make ci`,
# and .github/workflows/firmware.yml runs the Z3805A's checks.

# The repository's root, from which every path is written.
ROOT ?= ../..
FIRMWARE = $(ROOT)/third_party/firmware

CARGO ?= cargo
CLANG_FORMAT ?= clang-format
M68K_CC ?= m68k-linux-gnu-gcc

# The image folders: each one holding a names file.
IMAGES = $(patsubst $(FIRMWARE)/%/names.tsv,%,$(wildcard $(FIRMWARE)/*/names.tsv))
# The image docs/firmware/modules.md and docs/firmware/README.md's
# figures describe.
DOCUMENTED_IMAGE = z3805a-3543b

# The targets run for each image.
IMAGE_TARGETS = check-types check-decompiled report-decompiled scpi-tree check-scpi-tree handlers
# The rest, run once.
TARGETS = format-c check-format-c check-module-figures module-figures check-pforth-words \
	pforth-words check-report-figures report-figures check-unicorn

# Where an image's checks keep what they make.
work = $(ROOT)/target/firmware/$(1)

.PHONY: help $(IMAGE_TARGETS) $(TARGETS)

# Without a target, the targets; none is run by default, since format-c
# rewrites files and the checks take minutes.
help:
	@echo "make -C third_party/firmware TARGET [IMAGE=...], images: $(IMAGES)"
	@echo "each image's: $(IMAGE_TARGETS)"
	@echo "once: $(TARGETS)"

# The project's own C, in .clang-format's style: include/'s headers,
# each image's type header and layout check, and its hand-written
# firmware, rewritten/ and the folders under it.
C_SOURCES = $(wildcard $(FIRMWARE)/*/*.[ch] $(FIRMWARE)/*/rewritten/*.[ch] \
	$(FIRMWARE)/*/rewritten/*/*.[ch])
format-c:
	$(CLANG_FORMAT) -i $(C_SOURCES)

check-format-c:
	$(CLANG_FORMAT) --dry-run -Werror $(C_SOURCES)

# Fail if a figure in docs/firmware/modules.md -- a module's or layer's
# functions, a function's callers or calls, the calls the wrong way --
# is not what the image's modules.tsv and the exported C's direct calls
# give; module-figures rewrites them.
check-module-figures:
	python3 -I $(ROOT)/tools/firmware/module_figures.py --image $(DOCUMENTED_IMAGE)

module-figures:
	python3 -I $(ROOT)/tools/firmware/module_figures.py --image $(DOCUMENTED_IMAGE) --write

# Fail if a table in docs/firmware/pforth/IMAGE-words.md -- each image's
# pForth words, their headers, flags and stack effects -- is not what
# the image gives; pforth-words rewrites them.
check-pforth-words:
	uv run -q $(ROOT)/tools/firmware/pforth_words.py

pforth-words:
	uv run -q $(ROOT)/tools/firmware/pforth_words.py --write

# Fail if Unicorn's two m68k bugs the harness works around, its status
# register read and a code hook's write of PC losing the condition
# codes, no longer show as recorded, or if the harness's workarounds do
# not give the right codes (PLAN.md, "Done 2026-10-22, the harness's
# status register").
check-unicorn:
	uv run -q $(ROOT)/tools/firmware/unicorn_check.py

# The decompiled C's flags.  -fno-defer-pop has gcc pop a call's
# arguments after the call, so that from a task in user mode an argument
# is not pushed on one stack and popped from the other across
# enter_supervisor or a move back of the status register.
DECOMPILED_CFLAGS = -std=gnu17 -mcpu=cpu32 -O1 -fwrapv -fno-toplevel-reorder -fno-builtin -ffunction-sections \
	-fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -fno-defer-pop -w \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types

# How many of the decompiled C functions, as gcc compiles them, agree
# with the image's own in the emulator; a measure, never a failure.
# The report is saved in the image's work folder, and check-report-figures
# reads DOCUMENTED_IMAGE's, which report-decompiled checks right after
# writing it.
REPORT = report-decompiled.txt
REPORT_DECOMPILED = $(call work,$(DOCUMENTED_IMAGE))/$(REPORT)
CHECK_REPORT_FIGURES = python3 -I $(ROOT)/tools/firmware/report_figures.py $(REPORT_DECOMPILED)

# Fail if docs/firmware/README.md's figures from the function check --
# the functions run in user mode alone and in both modes, and the
# console sessions agreeing -- are not what the last make
# report-decompiled saved; report-figures rewrites them.
check-report-figures:
	$(CHECK_REPORT_FIGURES)

report-figures:
	python3 -I $(ROOT)/tools/firmware/report_figures.py $(REPORT_DECOMPILED) --write

ifeq ($(IMAGE),)

$(IMAGE_TARGETS):
	@for image in $(IMAGES); do $(MAKE) --no-print-directory $@ IMAGE=$$image || exit 1; done

else

# The image's folder, its image, its exported C and where its checks
# keep what they make.
IMAGE_FOLDER = $(FIRMWARE)/$(IMAGE)
IMAGE_BIN = $(FIRMWARE)/$(IMAGE).bin
DECOMPILED = $(IMAGE_FOLDER)/decompiled/$(IMAGE).c
CONVENTIONS = $(IMAGE_FOLDER)/decompiled/$(IMAGE).conventions.jsonl
NAMES = $(IMAGE_FOLDER)/names.tsv
WORK = $(call work,$(IMAGE))

# Fail if a structure the image's smartclock.h declares for Ghidra does
# not have the size and offsets the image shows.  Needs an m68k
# compiler, so that int, long and pointers are the target's.
check-types:
	$(M68K_CC) -mcpu=cpu32 -fsyntax-only $(IMAGE_FOLDER)/check-layout.c

# Fail if the decompiled C does not compile, its assembly is not the
# image's own bytes, alone or linked, or a binding disagrees with its
# routine; and first if the figures or Unicorn's recorded bugs drift.
check-decompiled: check-module-figures check-unicorn
	mkdir -p $(WORK)
	$(M68K_CC) $(DECOMPILED_CFLAGS) -c -o $(WORK)/decompiled.o $(DECOMPILED)
	python3 $(ROOT)/tools/firmware/decompiled_check.py $(DECOMPILED) $(IMAGE_BIN) $(WORK)/decompiled.o
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q $(ROOT)/tools/firmware/rebuild_link.py \
		$(DECOMPILED) $(IMAGE_BIN)
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q $(ROOT)/tools/firmware/bridge_check.py \
		$(DECOMPILED) $(IMAGE_BIN)

report-decompiled:
	mkdir -p $(WORK)
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q $(ROOT)/tools/firmware/function_check.py \
		$(DECOMPILED) $(IMAGE_BIN) | tee $(WORK)/$(REPORT)
	$(if $(filter $(DOCUMENTED_IMAGE),$(IMAGE)),$(CHECK_REPORT_FIGURES))

# The image's SCPI tree as C data and as names-file rows, from the image
# (docs/scpi/README.md, "As C"); cargo test fails while they are stale.
SCPI_TREE_C = $(IMAGE_FOLDER)/rewritten/scpi_tree_data.c
SCPI_TREE_NAMES = $(IMAGE_FOLDER)/scpi-tree.tsv
INSTALLER_SCPI_TREE_C = $(IMAGE_FOLDER)/rewritten/installer/installer_scpi_tree_data.c
INSTALLER_SCPI_TREE_NAMES = $(IMAGE_FOLDER)/installer-scpi-tree.tsv
SCPI_TREE_IMAGE = --names-file $(NAMES) $(IMAGE_BIN)
scpi-tree:
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --c $(SCPI_TREE_IMAGE) \
		| $(CLANG_FORMAT) --assume-filename=$(SCPI_TREE_C) > $(SCPI_TREE_C)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --names $(SCPI_TREE_IMAGE) > $(SCPI_TREE_NAMES)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --c --installer $(SCPI_TREE_IMAGE) \
		| $(CLANG_FORMAT) --assume-filename=$(INSTALLER_SCPI_TREE_C) > $(INSTALLER_SCPI_TREE_C)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --names --installer $(SCPI_TREE_IMAGE) \
		> $(INSTALLER_SCPI_TREE_NAMES)

# Fail if a record of a generated SCPI tree, the primary's or the
# installer's, as m68k gcc lays it out, is not the image's bytes, its
# pointers compared by what they point at.
check-scpi-tree:
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" python3 $(ROOT)/tools/firmware/scpi_tree_check.py \
		$(SCPI_TREE_C) $(SCPI_TREE_NAMES) $(NAMES) $(CONVENTIONS) $(IMAGE_BIN)
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" python3 $(ROOT)/tools/firmware/scpi_tree_check.py \
		$(INSTALLER_SCPI_TREE_C) $(INSTALLER_SCPI_TREE_NAMES) $(NAMES) $(CONVENTIONS) $(IMAGE_BIN)

# Each SCPI path of the image with its query handler's and setter's
# addresses, for SmartClockSetup.java's scpi= (docs/firmware/README.md,
# "Decompiling with Ghidra"); cargo test fails while it is stale.
handlers:
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --handlers $(IMAGE_BIN) > $(IMAGE_FOLDER)/handlers.txt

endif
