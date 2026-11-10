# Reproduce an FPGA counter's decode, simulation and waveform figures.
#
# Run from the repository root:
#
#     make -f tools/fpga/Makefile z3805a       # decode -> Verilog -> sim -> VCD
#     make -f tools/fpga/Makefile figures      # render every waves/*/*.json
#     make -f tools/fpga/Makefile explainer    # the explainer page from them
#     make -f tools/fpga/Makefile detail       # the detailed diagrams page from the decodes
#     make -f tools/fpga/Makefile check        # rebuild all images, fail on drift
#     make -j16 -f tools/fpga/Makefile check-readings      # the long runs
#     make -f tools/fpga/Makefile check-decoder-upstream  # the decodes at upstream PR #3
#
# A receiver is an image with a bitstream at $(BITDIR)/<image>.bit and a
# testbench tools/fpga/<image>_tb.v (copy the nearest one; it needs the
# +vcd block): IMAGES is every testbench.  Figure specs go under
# tools/fpga/waves/<image>/, and the references `check` diffs against,
# tools/fpga/ref/<image>.stats and .readout, are copied from $(OUT) once
# reviewed; a long run is a READ_ line below and its reference.  The
# decode clones the XC3000 viewer and pins it to the validated commit
# (DECODER_COMMIT, decoder.mk) under $(OUT); see docs/firmware/outputs.md,
# "Netlists".  `make <image>` runs the fast path, +seconds=0 (VVPFLAGS);
# the long runs are check-readings-<image>.<run>.

PACKAGE ?= PLCC84
OUT     ?= target/fpga
BITDIR  := third_party/firmware/images/fpga
HERE    := tools/fpga

# The XC3000 viewer/decoder, cloned and pinned rather than read from a
# local clone, which on another commit can decode otherwise
# (decoder.mk).  The stamp names the commit, so a new pin checks out
# again and decodes again.
include $(HERE)/decoder.mk
VIEWER_CLONE   := $(OUT)/xc3000
VIEWER         := $(VIEWER_CLONE)/html
VIEWER_STAMP   := $(VIEWER_CLONE).commit-$(DECODER_COMMIT)
# A second clone, at upstream pull request #3, and its decodes
# (check-decoder-upstream).
UPSTREAM       := $(OUT)/upstream
UPSTREAM_CLONE := $(UPSTREAM)/xc3000
UPSTREAM_STAMP := $(UPSTREAM_CLONE).commit-$(DECODER_UPSTREAM_COMMIT)

# That the decode $(1) records commit $(2) with no local change, so that
# a decode made from another checkout cannot pass.
define decoded_at
	@python3 -I -c 'import json, sys; sys.exit(json.load(open(sys.argv[1]))["decoder"]["commit"] != sys.argv[2])' \
	  $(1) $(2) || { echo "$(1) was not decoded at $(2) with no local change" >&2; exit 1; }
endef

# The images this tree knows about: one per testbench.  58503a-z3801a is
# one shared bitstream.
IMAGES := $(patsubst $(HERE)/%_tb.v,%,$(wildcard $(HERE)/*_tb.v))

# Default simulation: the fast path (preset and every address read, no wait
# on the divider tap).  Override per image with VVPFLAGS_<image>.
VVPFLAGS ?= +seconds=0 +registers +interpolator

# The Verilog module name.  Hyphens are illegal in identifiers, and an
# identifier may not start with a digit, so the images whose names do (the
# 585xx parts) are prefixed rather than suffixed; override per image here.
# (A tools/fpga/<image>_stimulus.v, where present, is a separate standalone
# harness with its own $finish, not an include for the main testbench.)
MODULE_58503a-z3801a := fpga_58503a_z3801a
MODULE_58503b        := fpga_58503b
module = $(or $(MODULE_$(1)),$(subst -,_,$(1))_fpga)

# What every decode and every simulation is made with, so that a change
# to one makes them again.
DECODE_TOOLS := $(HERE)/netdump.js $(HERE)/netdump.sh $(HERE)/netdump.html $(HERE)/viewer.js \
  $(HERE)/decoder.mk $(VIEWER_STAMP)
MODELS       := $(HERE)/xc3000.v $(HERE)/probe.v

.PHONY: all figures check check-viewer check-pins check-tiles check-readings \
  check-blocks check-explainer check-detail check-decoder-upstream explainer detail clean FORCE \
  $(IMAGES)
.PRECIOUS: $(OUT)/%.json $(UPSTREAM)/%.json $(OUT)/%.v $(OUT)/%_tb $(OUT)/%.vcd $(OUT)/%.readout $(OUT)/%.structure \
  $(OUT)/readings/%.readings $(OUT)/runs/%.vcd \
  $(OUT)/%.flags $(OUT)/readings/%.flags
.DELETE_ON_ERROR:
.SECONDEXPANSION:

all: $(IMAGES)

# Per-image convenience goal: everything up to the VCD.
$(IMAGES): %: $(OUT)/%.vcd

$(VIEWER_STAMP):
	$(call decoder_checkout,$(VIEWER_CLONE),$(DECODER_COMMIT))

$(OUT)/%.json: $(BITDIR)/%.bit $(DECODE_TOOLS)
	@mkdir -p $(OUT)
	$(call decoder_verify,$(VIEWER_CLONE),$(DECODER_COMMIT))
	$(HERE)/netdump.sh $(VIEWER) $< $(PACKAGE) > $@

$(OUT)/%.v: $(OUT)/%.json $(HERE)/netlist.py
	python3 $(HERE)/netlist.py $< --module $(call module,$*) > $@

$(OUT)/%_tb: $(OUT)/%.v $(MODELS) $(HERE)/%_tb.v
	iverilog -g2005 -Wall -o $@ $(MODELS) $< $(HERE)/$*_tb.v

# A run's flags, rewritten only when they change, so that a run whose
# flags change runs again.
$(OUT)/%.flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(or $(VVPFLAGS_$*),$(VVPFLAGS))' | cmp -s - $@ \
	  || echo '$(or $(VVPFLAGS_$*),$(VVPFLAGS))' > $@

$(OUT)/%.vcd: $(OUT)/%_tb $(OUT)/%.flags
	vvp -n $< $(or $(VVPFLAGS_$*),$(VVPFLAGS)) +vcd=$@

# Waveform figures: tools/fpga/waves/<dir>/<name>.json -> $(OUT)/<dir>/<name>.svg.
# The spec's directory names its VCD: an image, its fast path's, or
# <image>.<run>, the VCD of that check-readings run's flags
# (READ_<image>.<run>), made under $(OUT)/runs.
WAVES := $(wildcard $(HERE)/waves/*/*.json)
FIGS  := $(patsubst $(HERE)/waves/%.json,$(OUT)/%.svg,$(WAVES))
figure_vcd = $(OUT)/$(if $(findstring .,$(1)),runs/)$(1).vcd

figures: $(FIGS)

$(OUT)/runs/%.vcd: $(OUT)/$$(call reading_image,$$*)_tb $(OUT)/readings/%.flags
	@mkdir -p $(dir $@)
	vvp -n $< $(READ_$*) +vcd=$@ > /dev/null

$(OUT)/%.svg: $(HERE)/waves/%.json $(HERE)/vcd_wave.py $$(call figure_vcd,$$(firstword $$(subst /, ,$$*)))
	@mkdir -p $(dir $@)
	python3 $(HERE)/vcd_wave.py $(call figure_vcd,$(firstword $(subst /, ,$*))) $< > $@

# The explainer page (explainer.py), written to EXPLAINER_PAGE: its
# figures from the checked references, its waveforms the figures it
# names.  It is not committed; the top Makefile's `fpga` writes it for
# GitHub Pages.  check-explainer renders it under $(OUT) and fails if
# it cannot be.
EXPLAINER_PAGE ?= $(OUT)/fpga.html
EXPLAINER_FIGS = $(addprefix $(OUT)/,$(shell python3 -I $(HERE)/explainer.py --figures-used))

check-explainer: $$(EXPLAINER_FIGS) $(HERE)/explainer.py $(HERE)/page.py
	@python3 -I $(HERE)/explainer.py --figures $(OUT) --output $(OUT)/fpga.html \
	  && echo "check explainer: the page renders"

explainer: $$(EXPLAINER_FIGS) $(HERE)/explainer.py $(HERE)/page.py
	python3 -I $(HERE)/explainer.py --figures $(OUT) --output $(EXPLAINER_PAGE)

# The detailed diagrams page (detail.py), written to DETAIL_PAGE: one
# diagram a design, each drawn from the design's decode and the element
# tables in detail_designs.py, its figures from the checked references.
# It is not committed; the top Makefile's `fpga-detail` writes it for
# GitHub Pages.  check-detail renders it under $(OUT) and fails if it
# cannot be: an element naming a block the decode does not use, a pin
# in use with no role, an output no element drives, or a figure whose
# reference no longer matches; and first its own test, that each of those
# refusals happens.
DETAIL_PAGE ?= $(OUT)/fpga-detail.html
DETAIL_DEPS = $(addprefix $(OUT)/,$(addsuffix .json,$(IMAGES))) \
  $(addprefix $(HERE)/,detail.py detail_bind.py detail_designs.py page.py)

check-detail: $(DETAIL_DEPS)
	@python3 -I $(HERE)/detail.py --netlists $(OUT) --self-test
	@python3 -I $(HERE)/detail.py --netlists $(OUT) --output $(OUT)/fpga-detail.html \
	  && echo "check detail: the page renders"

detail: $(DETAIL_DEPS)
	python3 -I $(HERE)/detail.py --netlists $(OUT) --output $(DETAIL_PAGE)

# A fresh decode must still compile and run, and the netlist must not drift
# from what is committed as a reference (tools/fpga/ref/<image>.stats).
# The read port the firmware sees, every address with the preset on: what
# the read multiplexer and the bus's output registers give, which
# `netstats` cannot see (an output register clocked on the other edge
# leaves the net counts unchanged).
$(OUT)/%.readout: $(OUT)/%_tb
	vvp -n $< +seconds=0 +registers +interpolator 2>/dev/null | grep -E '^register ' > $@

# Each netlist's structure: Icarus Verilog's warnings, which should be
# none, as it prints them, and Yosys's `check`, its warnings with its
# combinational loops by the nets they pass through, against
# tools/fpga/ref/<image>.structure.  Either tool missing or failing is an
# error, not an empty report.
$(OUT)/%.structure: $(OUT)/%.v $(MODELS) $(HERE)/%_tb.v
	@command -v iverilog > /dev/null && command -v yosys > /dev/null \
	  || { echo "$@: the structure check needs Icarus Verilog and Yosys" >&2; exit 1; }
	@iverilog -g2005 -Wall -o /dev/null $(MODELS) $< $(HERE)/$*_tb.v > $@.iverilog 2>&1 \
	  || { cat $@.iverilog >&2; rm -f $@.iverilog; exit 1; }
	@yosys -q -p 'read_verilog $(HERE)/xc3000.v $<; hierarchy -top $(call module,$*); proc; flatten; check' \
	  > $@.yosys 2>&1 || { cat $@.yosys >&2; rm -f $@.iverilog $@.yosys; exit 1; }
	@{ cat $@.iverilog; grep -v 'limited support for tri-state' $@.yosys \
	   | sed -n 's/^Warning: //p; s/^ *wire \(\S*\).*/  through \1/p'; } > $@
	@rm -f $@.iverilog $@.yosys

check: check-viewer $(addprefix check-,$(IMAGES)) check-pins check-tiles check-blocks check-explainer check-detail

# The viewer's clone at the pin with no local change, on every check
# (decoder.mk), not only when its stamp is made.
check-viewer: $(VIEWER_STAMP)
	$(call decoder_verify,$(VIEWER_CLONE),$(DECODER_COMMIT))

# The viewer's XC3042 pad tables against the data sheet (pincheck.py).
check-pins: $(VIEWER_STAMP) $(HERE)/pincheck.py
	@python3 $(HERE)/pincheck.py $(VIEWER) > $(OUT)/pins.txt
	@diff -u $(HERE)/ref/pins.txt $(OUT)/pins.txt && echo "check pins: pad tables match reference"

# The 58503A/Z3801A's logic blocks against the Z3805A's (tilediff.py),
# and tilediff.py's own test: each of its listed changes to a block,
# output selection among them, is reported.
check-tiles: $(OUT)/z3805a.json $(OUT)/58503a-z3801a.json
	@python3 -I $(HERE)/tilediff.py --self-test $(OUT)/z3805a.json
	@python3 -I $(HERE)/tilediff.py $^ > $(OUT)/58503a-z3801a.tilediff
	@diff -u $(HERE)/ref/58503a-z3801a.tilediff $(OUT)/58503a-z3801a.tilediff \
	  && echo "check tiles: 58503a-z3801a against z3805a matches reference"

# The blocks a trace says are another design's, block for block
# (blockmatch.py): tools/fpga/<name>.match pairs them, and MATCH_<name>
# names its two designs, the first's blocks the second's.  blockmatch.py's
# own test first: each of its listed changes to a block is reported.
MATCH_58503b-z3805a        := 58503b z3805a
MATCH_58503b-58503a-z3801a := 58503b 58503a-z3801a
MATCHES := $(patsubst MATCH_%,%,$(filter MATCH_%,$(.VARIABLES)))
match_designs = $(addprefix $(OUT)/,$(addsuffix .json,$(MATCH_$(1))))

check-blocks: $(OUT)/z3805a.json $(HERE)/blockmatch.py $(addprefix check-blocks-,$(MATCHES))
	@python3 -I $(HERE)/blockmatch.py --self-test $<

check-blocks-%: $(HERE)/%.match $(HERE)/blockmatch.py $$(call match_designs,$$*)
	@python3 -I $(HERE)/blockmatch.py $< $(call match_designs,$*) > $(OUT)/$*.blockmatch \
	  || { cat $(OUT)/$*.blockmatch >&2; exit 1; }
	@echo "check blocks: $(word 1,$(MATCH_$*))'s blocks in $< are $(word 2,$(MATCH_$*))'s"

check-%: $(OUT)/%.vcd $(OUT)/%.readout $(OUT)/%.structure
	$(call decoded_at,$(OUT)/$*.json,$(DECODER_COMMIT))
	@python3 $(HERE)/netstats.py $(OUT)/$*.json > $(OUT)/$*.stats
	@for kind in stats readout structure; do \
	  if [ ! -f $(HERE)/ref/$*.$$kind ]; then \
	    echo "check $*: no reference $(HERE)/ref/$*.$$kind; review $(OUT)/$*.$$kind and copy it there" >&2; \
	    exit 1; \
	  fi; \
	  diff -u $(HERE)/ref/$*.$$kind $(OUT)/$*.$$kind && echo "check $*: $$kind matches reference" || exit 1; \
	done

# Every design decoded at upstream pull request #3 too
# (DECODER_UPSTREAM_COMMIT, decoder.mk), into a clone and decodes of its
# own under $(UPSTREAM), and each decode compared with the pin's, the
# JSON's decoder header aside: the same netlist at either commit.  Each
# run verifies both clones at their commits and that each decode records
# its commit, so a changed checkout cannot pass on stale decodes; and
# check-decoder-upstream-xact runs check-fpga-decoder's comparison with
# XACT's test designs (tools/fpga/xact/roundtrip.py) at the pull
# request, which covers the input flip-flops' clock polarity that no
# SmartClock design exercises.  Not part of `check` or CI: it clones and
# decodes again, and needs the network; run it by hand when netdump.js
# or the pin changes.
$(UPSTREAM_STAMP):
	@mkdir -p $(UPSTREAM)
	$(call decoder_checkout,$(UPSTREAM_CLONE),$(DECODER_UPSTREAM_COMMIT))

$(UPSTREAM)/%.json: $(BITDIR)/%.bit $(DECODE_TOOLS) $(UPSTREAM_STAMP)
	$(call decoder_verify,$(UPSTREAM_CLONE),$(DECODER_UPSTREAM_COMMIT))
	$(HERE)/netdump.sh $(UPSTREAM_CLONE)/html $< $(PACKAGE) > $@

# A decode's JSON without its decoder header, which names the commit.
decode_body = python3 -I -c 'import json, sys; d = json.load(open(sys.argv[1])); d.pop("decoder"); \
  json.dump(d, sys.stdout, indent=1)' $(1) > $(2)

check-decoder-upstream: $(addprefix check-decoder-upstream-,$(IMAGES) xact)

check-decoder-upstream-xact: $(UPSTREAM_STAMP) $(HERE)/netdump.js
	$(call decoder_verify,$(UPSTREAM_CLONE),$(DECODER_UPSTREAM_COMMIT))
	python3 -I $(HERE)/xact/roundtrip.py $(UPSTREAM_CLONE)/html

check-decoder-upstream-%: $(OUT)/%.json $(UPSTREAM)/%.json
	$(call decoder_verify,$(VIEWER_CLONE),$(DECODER_COMMIT))
	$(call decoder_verify,$(UPSTREAM_CLONE),$(DECODER_UPSTREAM_COMMIT))
	$(call decoded_at,$(OUT)/$*.json,$(DECODER_COMMIT))
	$(call decoded_at,$(UPSTREAM)/$*.json,$(DECODER_UPSTREAM_COMMIT))
	@$(call decode_body,$(OUT)/$*.json,$(UPSTREAM)/$*.pin)
	@$(call decode_body,$(UPSTREAM)/$*.json,$(UPSTREAM)/$*.upstream)
	@diff -u $(UPSTREAM)/$*.pin $(UPSTREAM)/$*.upstream \
	  && echo "check-decoder-upstream $*: decodes the same at $(DECODER_UPSTREAM_COMMIT) as at the pin"

# The long runs: every figure the FPGA docs quote from a simulation that
# `check` does not run, each run's output diffed against
# tools/fpga/ref/<image>.<run>.readings.  Not part of `check` or CI: each
# run waits on the divider, at minutes a simulated second, so run it with
# -j, when a change could move a figure (`time` it for the duration).  A
# run is <image>.<name>, its testbench's flags READ_<run>, always with
# +probe (probe.v).
READ_z3805a.fast        := +seconds=0 +registers +interpolator
# The read map with an interpolator byte whose nibbles are not each
# other's complement, as 5a's are, so that 0x10 and 0x11 can only read
# it complemented, low nibble first (also z3815a, z3816a, 58503a-z3801a
# and 58503b's byte_1c).
READ_z3805a.byte_1c     := +seconds=0 +registers +byte=1c
READ_z3805a.three       := +seconds=3 +interpolator
READ_z3805a.late        := +seconds=1 +gps=700000037
READ_z3805a.sync1       := +seconds=2 +sync=1
READ_z3805a.sync2       := +seconds=2 +sync=2
READ_z3805a.sync3       := +seconds=2 +sync=3
READ_z3805a.low         := +seconds=1 +low
READ_z3805a.wide        := +seconds=1 +width=300000
READ_z3805a.later       := +seconds=1 +gps=250000087
READ_z3805a.nogps       := +seconds=1 +gps=20000000000
READ_z3815a.fast        := +seconds=0 +registers +interpolator
READ_z3815a.byte_1c     := +seconds=0 +registers +byte=1c
READ_z3815a.three       := +seconds=3 +interpolator
READ_z3815a.freerun     := +freerun=2000000 +edges
# The P38/P40 comparison with P7 a clock of its own (10 MHz unless set):
# P7 slower (130 ns) and faster (97 ns), P53 faster (48 ns), and P53 at
# the 19.6608 MHz the divides imply, 625,000 / 12,288 ns, P7 at 10 MHz
# (pll); each clock edge falls within half a picosecond of its time
# (the testbenches).
READ_z3815a.p7_130      := +freerun=5000000 +edges +p7=130
READ_z3815a.p7_97       := +freerun=5000000 +edges +p7=97
READ_z3815a.osc_48      := +freerun=5000000 +edges +p7=100 +osc=48
READ_z3815a.pll         := +freerun=5000000 +edges +p7=100 +osc=50.862630208333
# Four seconds free, for the 1 PPS pins that pulse on alternate seconds,
# and every P38 and P40 rise across their edges.
READ_z3815a.freerun_4s  := +freerun=4000000000 +edges
# Register 3's sync field, 0 then the kind on the first arm.
READ_z3815a.sync1       := +seconds=2 +sync=1
READ_z3815a.sync2       := +seconds=2 +sync=2
READ_z3815a.sync3       := +seconds=2 +sync=3
# P35 a clock of its own, two seconds free: the P35 counter, reset at
# each internal 1 PPS, and its pins.  P84 a clock of its own, a period no
# bus time is a multiple of, under the read map: its counter at the odd
# addresses.
READ_z3815a.p35         := +freerun=2000000000 +p35=130
READ_z3815a.p84         := +seconds=0 +registers +p84=2917
READ_z3816a.fast        := +seconds=0 +registers
READ_z3816a.byte_1c     := +seconds=0 +registers +byte=1c
READ_z3816a.three       := +seconds=3
READ_z3816a.freerun     := +freerun=2000000 +edges
READ_z3816a.p7_130      := +freerun=5000000 +edges +p7=130
READ_z3816a.p7_97       := +freerun=5000000 +edges +p7=97
READ_z3816a.osc_48      := +freerun=5000000 +edges +osc=48
READ_z3816a.pll         := +freerun=5000000 +edges +osc=50.862630208333
READ_z3816a.freerun_4s  := +freerun=4000000000 +edges
READ_z3816a.sync1       := +seconds=2 +sync=1
READ_z3816a.sync2       := +seconds=2 +sync=2
READ_z3816a.sync3       := +seconds=2 +sync=3
READ_58503b.fast        := +seconds=0 +registers
# The read map with the interpolator byte 1c (z3805a.byte_1c).
READ_58503b.byte_1c     := +seconds=0 +registers +byte=1c
READ_58503b.three       := +seconds=3 +interpolator
READ_58503b.freerun_4s  := +freerun=4000000000
# Register 3's sync field, 0 then the kind on the first arm.
READ_58503b.sync1       := +seconds=2 +sync=1
READ_58503b.sync2       := +seconds=2 +sync=2
READ_58503b.sync3       := +seconds=2 +sync=3
# The controls the other runs leave at 0, each in a free run: address
# 6's two polarity bits, DATA0 and DATA1 alone, which the firmware never
# writes, and register 2's bit 0, pulsed at 1.5 s as the firmware pulses
# it, which resets FD, the phase of P14's alternate seconds.
READ_58503b.polarity_1  := +freerun=2000000000 +polarity=1
READ_58503b.polarity_2  := +freerun=2000000000 +polarity=2
READ_58503b.phase       := +freerun=3500000000 +phase=1500000000
READ_58503a-z3801a.fast := +seconds=0 +registers
READ_58503a-z3801a.byte_1c := +seconds=0 +registers +byte=1c
READ_58503a-z3801a.three := +seconds=3 +interpolator
READ_58503a-z3801a.sync1 := +seconds=2 +sync=1
READ_58503a-z3801a.sync2 := +seconds=2 +sync=2
READ_58503a-z3801a.sync3 := +seconds=2 +sync=3
READINGS := $(patsubst READ_%,%,$(filter READ_%,$(.VARIABLES)))
# The runs on a stimulus file, tools/fpga/<run>.stim, instead of a
# testbench's flags.
STIMULUS_READINGS := z3805a.sync_3_late
reading_image = $(firstword $(subst ., ,$(1)))

check-readings: $(addprefix check-readings-,$(READINGS) $(STIMULUS_READINGS))

$(OUT)/readings/%.flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(READ_$*)' | cmp -s - $@ || echo '$(READ_$*)' > $@

$(OUT)/readings/%.readings: $(OUT)/$$(call reading_image,$$*)_tb $(OUT)/readings/%.flags
	@mkdir -p $(dir $@)
	vvp -n $< $(READ_$*) +probe | grep -v '\$$finish called' > $@

check-readings-%: $(OUT)/readings/%.readings
	@diff -u $(HERE)/ref/$*.readings $< && echo "check-readings $*: matches reference"

# The Z3805A's netlist under a stimulus file (z3805a_stimulus.v).
$(OUT)/z3805a_stimulus: $(OUT)/z3805a.v $(MODELS) $(HERE)/z3805a_stimulus.v
	iverilog -g2005 -Wall -o $@ $(MODELS) $< $(HERE)/z3805a_stimulus.v

# The full sync the internal 1 PPS comes before, with the divider probed.
$(OUT)/readings/z3805a.sync_3_late.readings: $(OUT)/z3805a_stimulus $(HERE)/z3805a.sync_3_late.stim
	@mkdir -p $(dir $@)
	vvp -n $< +stimulus=$(HERE)/z3805a.sync_3_late.stim +probe \
	  | grep -v '\$$finish called' > $@

FORCE:

clean:
	rm -rf $(OUT)
