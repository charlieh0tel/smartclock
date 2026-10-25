# The firmware's checks and generated files, run from here alone or
# delegated by name from the top Makefile:
#
#     make -C third_party/firmware TARGET [IMAGE=z3805a-3543b]
#
# A target of an image's runs for IMAGE, or without it for every image
# folder present (report-decompiled for the Z3805A's alone, below):
# IMAGE/, beside IMAGE.bin, which holds that image's
# work (PLAN.md, "Decided 2026-10-22, the firmware layout").  Needs an
# m68k gcc and binutils, clang-format and uv; none is part of `make ci`,
# and .github/workflows/firmware.yml runs the Z3805A's checks.

# The repository's root, from which every path is written.
ROOT ?= ../..
FIRMWARE = $(ROOT)/third_party/firmware

# bash, so that a pipeline fails when any command in it does: the
# function check's report through tee, a generated tree through
# clang-format.
SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -c

CARGO ?= cargo
CLANG ?= clang
CLANG_FORMAT ?= clang-format
M68K_CC ?= m68k-linux-gnu-gcc

# The image folders: each one holding a names file.
IMAGES = $(patsubst $(FIRMWARE)/%/names.tsv,%,$(wildcard $(FIRMWARE)/*/names.tsv))
# The image docs/firmware/modules.md and docs/firmware/README.md's
# figures describe.
DOCUMENTED_IMAGE = z3805a-3543b

# The image folders with an export, decompiled/IMAGE.c, and rewritten C:
# the ones the export's and the SCPI tree's targets can run for.  A
# folder only seeded so far (names, types) has neither.
EXPORTED_IMAGES = $(foreach image,$(IMAGES),$(if $(wildcard $(FIRMWARE)/$(image)/decompiled/$(image).c),$(image)))

# The targets run for each image: those that need only its names and
# types, and those that need its export too; and the function check,
# which without IMAGE runs for DOCUMENTED_IMAGE alone, the image the
# documents' figures describe (another's: make
# report-decompiled IMAGE=58503a-3704).
NAMED_TARGETS = check-types handlers scpi-tree
EXPORTED_TARGETS = check-decompiled check-scpi-tree check-magic-numbers magic-numbers
IMAGE_TARGETS = $(NAMED_TARGETS) $(EXPORTED_TARGETS) report-decompiled
# The rest, run once.
TARGETS = format-c check-format-c check-module-figures module-figures check-pforth-words \
	pforth-words check-report-figures report-figures check-unicorn \
	check-chip-selects chip-selects

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

# Fail if docs/firmware/chip-selects.md, or a map docs/firmware/restart.md
# or docs/firmware/README.md gives between its markers, is not what the
# images' reset code writes to the SIM's chip-select registers;
# chip-selects rewrites them, keeping the maps' Device column.
check-chip-selects:
	python3 -I $(ROOT)/tools/firmware/chip_selects.py

chip-selects:
	python3 -I $(ROOT)/tools/firmware/chip_selects.py --write

# The decompiled C's flags.  -fno-defer-pop has gcc pop a call's
# arguments after the call, so that from a task in user mode an argument
# is not pushed on one stack and popped from the other across
# enter_supervisor or a move back of the status register.
# -mstrict-align, which -mcpu=cpu32 implies, is named, since the CPU32
# reads and writes words and longs at even addresses alone (MC68331UM,
# 4.4.4).
DECOMPILED_CFLAGS = -std=gnu17 -mcpu=cpu32 -mstrict-align -O1 -fwrapv -fno-toplevel-reorder -fno-builtin \
	-ffunction-sections -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -fno-defer-pop -w \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types

# The libgcc the rebuilt image links, the CPU32's with soft float,
# built from GCC's source vendored in third_party/gcc/
# (tools/firmware/cpu32_libgcc.py): the toolchain's own is a 68020's
# with a 68881, whose float conversions and 64-bit division the CPU32
# traps on.
GCC_VENDORED = $(shell find $(ROOT)/third_party/gcc -type f)
CPU32_LIBGCC = $(ROOT)/target/firmware/libgcc-cpu32/libgcc.a
LINK_ENV = DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" CPU32_LIBGCC="$(CPU32_LIBGCC)"

$(CPU32_LIBGCC): $(ROOT)/tools/firmware/cpu32_libgcc.py $(GCC_VENDORED)
	M68K_CC="$(M68K_CC)" python3 -I $< $@

# How many of the decompiled C functions, as gcc compiles them, agree
# with the image's own in the emulator; fails if any function fails.
# The report is saved in the image's work folder, and check-report-figures
# reads DOCUMENTED_IMAGE's, which report-decompiled checks right after
# writing it.
REPORT = report-decompiled.txt
REPORT_DECOMPILED = $(call work,$(DOCUMENTED_IMAGE))/$(REPORT)
CHECK_REPORT_FIGURES = python3 -I $(ROOT)/tools/firmware/report_figures.py $(REPORT_DECOMPILED)

# Fail if the documents' figures from the function check --
# docs/firmware/README.md's functions run in user mode alone and in both
# modes and console sessions agreeing, docs/firmware/rewrite.md's and
# README.md's counts of functions agreeing, known, skipped and failed
# and the agreeing ones' cases -- are not what the last make
# report-decompiled saved, or if any function failed; report-figures
# rewrites them.
check-report-figures:
	$(CHECK_REPORT_FIGURES)

report-figures:
	python3 -I $(ROOT)/tools/firmware/report_figures.py $(REPORT_DECOMPILED) --write

ifeq ($(IMAGE),)

$(NAMED_TARGETS):
	@for image in $(IMAGES); do $(MAKE) --no-print-directory $@ IMAGE=$$image || exit 1; done

$(EXPORTED_TARGETS):
	@for image in $(EXPORTED_IMAGES); do $(MAKE) --no-print-directory $@ IMAGE=$$image || exit 1; done

report-decompiled:
	@$(MAKE) --no-print-directory $@ IMAGE=$(DOCUMENTED_IMAGE)

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
check-decompiled: check-module-figures check-unicorn $(CPU32_LIBGCC)
	mkdir -p $(WORK)
	$(M68K_CC) $(DECOMPILED_CFLAGS) -c -o $(WORK)/decompiled.o $(DECOMPILED)
	python3 $(ROOT)/tools/firmware/decompiled_check.py $(DECOMPILED) $(IMAGE_BIN) $(WORK)/decompiled.o
	$(LINK_ENV) uv run -q $(ROOT)/tools/firmware/rebuild_link.py $(DECOMPILED) $(IMAGE_BIN)
	$(LINK_ENV) uv run -q $(ROOT)/tools/firmware/bridge_check.py $(DECOMPILED) $(IMAGE_BIN)

report-decompiled: $(CPU32_LIBGCC)
	mkdir -p $(WORK)
	$(LINK_ENV) uv run -q $(ROOT)/tools/firmware/function_check.py \
		$(DECOMPILED) $(IMAGE_BIN) | tee $(WORK)/$(REPORT)
	$(if $(filter $(DOCUMENTED_IMAGE),$(IMAGE)),$(CHECK_REPORT_FIGURES))

# The image's SCPI tree as C data and as names-file rows, from the image
# (docs/scpi/README.md, "As C"); cargo test fails while they are stale.
# An image with no rewritten/ yet gets the rows alone.
SCPI_TREE_C = $(IMAGE_FOLDER)/rewritten/scpi_tree_data.c
SCPI_TREE_NAMES = $(IMAGE_FOLDER)/scpi-tree.tsv
INSTALLER_SCPI_TREE_C = $(IMAGE_FOLDER)/rewritten/installer/installer_scpi_tree_data.c
INSTALLER_SCPI_TREE_NAMES = $(IMAGE_FOLDER)/installer-scpi-tree.tsv
SCPI_TREE_IMAGE = --names-file $(NAMES) $(IMAGE_BIN)
scpi-tree:
ifneq ($(wildcard $(IMAGE_FOLDER)/rewritten),)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --c $(SCPI_TREE_IMAGE) \
		| $(CLANG_FORMAT) --assume-filename=$(SCPI_TREE_C) > $(SCPI_TREE_C)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --c --installer $(SCPI_TREE_IMAGE) \
		| $(CLANG_FORMAT) --assume-filename=$(INSTALLER_SCPI_TREE_C) > $(INSTALLER_SCPI_TREE_C)
endif
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --names $(SCPI_TREE_IMAGE) > $(SCPI_TREE_NAMES)
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

# The magic numbers in the image's hand-written C, as clang parses it
# within the export (tools/firmware/magic_check.py): a literal where an
# enumerator, a register header's bit or a named constant is, and a
# constant defined twice.  check-magic-numbers fails on a finding of a
# checked kind that the image's magic-allow.tsv does not justify, or on
# a row there no finding matches; magic-numbers prints every finding,
# the unchecked kinds too.
MAGIC_NUMBERS = CLANG="$(CLANG)" DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" \
	python3 -I $(ROOT)/tools/firmware/magic_check.py --image $(IMAGE)
check-magic-numbers:
	$(MAGIC_NUMBERS) --check

magic-numbers:
	$(MAGIC_NUMBERS)

# Each SCPI path of the image with its query handler's and setter's
# addresses, for SmartClockSetup.java's scpi= (docs/firmware/README.md,
# "Decompiling with Ghidra"); cargo test fails while it is stale.
handlers:
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --handlers $(IMAGE_BIN) > $(IMAGE_FOLDER)/handlers.txt

endif
