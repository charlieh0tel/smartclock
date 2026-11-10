# CI calls the shared rust-ci.yml; "make ci" runs the same checks locally.
#
# Every target says --workspace.  This is a virtual workspace, so that
# is already the default; saying it keeps the targets honest if a
# default-members is ever added.  One was, briefly, and it narrowed
# "cargo test" to a single crate: zero tests, exit 0, CI green while
# testing nothing.

CARGO ?= cargo

# The package version is asked of the binary being packaged rather than
# derived a second time here.  The binary is stamped at compile time by
# the library's build script, so the package, the --version output, the
# daemon's journal line, its info reply and the writer recorded in the
# log all carry one string.  Two builds of different code then cannot
# share a version, which matters because dpkg treats reinstalling an
# identical version as a no-op: the binaries change or they do not, and
# nothing from the outside says which.

.PHONY: all build windows hooks ci fmt fmt-check clippy test test-hw web-deps test-web screenshots doc docs pages check-pages clean deb release release-notes install-service

all: build

build:
	$(CARGO) build --workspace --all-targets

# Everything, cross-built for Windows; needs the x86_64-pc-windows-gnu
# target and mingw-w64.
WINDOWS_TARGET := x86_64-pc-windows-gnu
windows:
	$(CARGO) build --workspace --all-targets --target $(WINDOWS_TARGET)

# What CI runs.  Keep this the whole of it.
# Use the repository's git hooks, once per clone: commit-msg refuses a
# message crediting an AI assistant (AGENTS.md).
hooks:
	git config core.hooksPath .githooks

ci: fmt-check clippy test

fmt:
	$(CARGO) fmt --all

fmt-check:
	$(CARGO) fmt --all -- --check

clippy:
	$(CARGO) clippy --workspace --all-targets --all-features -- -D warnings

# Everything that runs without a receiver.
test:
	$(CARGO) test --workspace --all-features

# Tests needing the attached receiver.  Stop smartclockd first: it holds
# the serial port open.  Never run in CI.
test-hw:
	$(CARGO) test --workspace --all-features -- --ignored

# The browser tests' own Playwright and Chromium, into the test
# directory's node_modules rather than the home directory.
WEB_TESTS := crates/smartclock-web/tests/browser

# The images for an Oncore engine: all but the Z3815A's, whose engine
# is a Furuno.
ONCORE_IMAGES := $(filter-out %/z3815a-4010.bin,$(wildcard third_party/firmware/images/*.bin))
web-deps:
	cd $(WEB_TESTS) && npm ci --no-fund --no-audit && npm run install-browser

# The web pages in a browser, against a faked API: switching receivers,
# a daemon going and coming back, and the other edges every page must
# handle alike.  Needs `make web-deps` once.
test-web:
	$(CARGO) build -p smartclock-web
	cd $(WEB_TESTS) && npm test

# The README's screenshots, into docs/images.  Give a running web view
# or a directory of logs, and a command running the terminal monitor
# for its screenshot; the status page is shot from the simulator.  See
# tools/screenshots.mjs.  Needs `make web-deps` once.  The README's
# were taken from read-only copies of the logs
# (`sqlite3 -readonly LOG "VACUUM INTO 'COPY'"`): the compare and
# correlation pages from one bench's three receivers and its sensor log,
# the live and stability pages from one receiver's current log, and the
# terminal monitor against a simulator's daemon:
#   make screenshots SHOTS_FROM="--log-dir BENCH --receiver 3710A01056 \
#     --span 86400 --stability-span 604800 --against sensor:temperature:ambient \
#     --only web-compare.png --only web-compare-stability.png \
#     --only web-correlation.png --only web-correlation-page.png" \
#     SHOTS_TUI="smartclockmon --daemon SIM_SOCKET"
#   node tools/screenshots.mjs --log-dir CURRENT --receiver 3710A01056 \
#     --span 86400 --stability-span 604800 --only web-live.png --only web-stability.png
screenshots:
	node tools/screenshots.mjs --sim $(SHOTS_FROM) $(if $(SHOTS_TUI),--tui '$(SHOTS_TUI)')

doc:
	$(CARGO) doc --workspace --no-deps --all-features

# Regenerate the documentation that is derived from the command table.
# A test fails if docs/commands.md and the table disagree.
docs:
	$(CARGO) run -q -p smartclock-cli -- commands > docs/commands.md
	for image in third_party/firmware/images/*.bin; do \
		$(CARGO) run -q -p smartclock-cli -- dump-scpi $$image \
			> docs/scpi/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --models third_party/firmware/images/*.bin \
		> docs/scpi/models.md
	for image in third_party/firmware/images/*.bin; do \
		$(CARGO) run -q -p smartclock-cli -- dump-pforth $$image \
			> docs/firmware/pforth/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-pforth --models third_party/firmware/images/*.bin \
		> docs/firmware/pforth/models.md
	for image in $(ONCORE_IMAGES); do \
		$(CARGO) run -q -p smartclock-cli -- dump-oncore $$image \
			> docs/firmware/oncore/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-oncore --models $(ONCORE_IMAGES) \
		> docs/firmware/oncore/models.md

# The published pages, into PAGES_DIR, each failing on an error in its
# source; .github/workflows/pages.yml publishes them on GitHub Pages, at
# https://charlieh0tel.github.io/smartclock/<page>.html.  The generated
# ones are not committed; the hand-written ones' sources are in
# docs/pages/, HAND_PAGES, copied with the files they name.  A page
# that once was at a path in docs/ left there a page forwarding to its
# new one.  Each page is built by the target of its own name, writing
# $(PAGES_DIR)/<page>.html with a <title>, a description meta tag and
# a smartclock-group meta tag naming its group in tools/pages_index.py;
# a page joins by such a target, named in PAGES.  The landing page,
# index.html, lists them by group, in that order within one, after
# NEW_PAGE, a page just launched, under "New", and FEATURED_PAGE under
# "Featured", each one of PAGES or empty for none, each set apart above
# them in a box of its own.  It also puts a breadcrumb back to
# index.html at the top of each page's content, the first element in its
# <body>, which must be a main, div, header, article or section; the
# sources in docs/pages/ are left without one.  It copies the site's
# icon, docs/pages/favicon.svg, beside index.html and links it from each
# page's <head> and the index's, unless a page declares an icon of its own.
# Its hero is a drawing of the 58503A's front panel, or HERO_IMAGE, a
# path under docs/, described by HERO_ALT.
PAGES_DIR ?= target/pages
HAND_PAGES := loop map55-4003 bench-sky-2026-10-03
PAGES := fpga board-atlas scpi-models $(HAND_PAGES)
NEW_PAGE := fpga
FEATURED_PAGE := loop
HERO_IMAGE :=
HERO_ALT :=
.PHONY: $(PAGES)
pages: $(PAGES)
	python3 -I tools/pages_index.py \
		$(if $(NEW_PAGE),--new $(NEW_PAGE).html) $(if $(FEATURED_PAGE),--featured $(FEATURED_PAGE).html) \
		$(if $(HERO_IMAGE),--hero $(HERO_IMAGE) --hero-alt "$(HERO_ALT)") \
		$(PAGES_DIR) $(PAGES:%=%.html)

# The board atlas, from docs/board-atlas.json, its photos copied beside
# it; fails if the data cites a file or section that does not exist or
# names a part it lacks.
# Needs only python3; .github/workflows/firmware.yml runs it too.
board-atlas:
	mkdir -p $(PAGES_DIR)
	python3 -I tools/board_atlas.py $(PAGES_DIR)/board-atlas.html

# The SCPI trees side by side, as a page that filters them.
scpi-models:
	mkdir -p $(PAGES_DIR)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --models --html third_party/firmware/images/*.bin \
		> $(PAGES_DIR)/scpi-models.html

$(HAND_PAGES):
	mkdir -p $(PAGES_DIR)
	python3 -I tools/pages_copy.py docs/pages/$@.html $(PAGES_DIR)

# The FPGA decoder's checks against XACT (tools/fpga/xact/README.md).
# check-fpga-decoder decodes each test design's committed bitstream with
# netdump.js and checks it against XACT's routed LCA
# (tools/fpga/xact/designs); it needs node, git and the XC3000 viewer
# netdump.js reads, cloned at the commit the tools were checked against
# into target/ (never vendored: it has no license).  fpga-designs
# rebuilds the designs with XACT under DOSBox-X, after
# tools/fpga/xact/fetch.sh, or in Docker with XACT_DOCKER=1;
# check-fpga-xapp checks XACT's XAPP003 designs the same way, locally.
include tools/fpga/decoder.mk
XC3000_VIEWER = target/fpga/xc3000
.PHONY: check-fpga-decoder fpga-designs check-fpga-xapp
$(XC3000_VIEWER).commit-$(DECODER_COMMIT):
	$(call decoder_checkout,$(XC3000_VIEWER),$(DECODER_COMMIT))

check-fpga-decoder: $(XC3000_VIEWER).commit-$(DECODER_COMMIT)
	$(call decoder_verify,$(XC3000_VIEWER),$(DECODER_COMMIT))
	python3 -I tools/fpga/xact/roundtrip.py $(XC3000_VIEWER)/html

fpga-designs:
	tools/fpga/xact/build.sh

# The FPGA explainer, $(PAGES_DIR)/fpga.html, for GitHub Pages
# (https://charlieh0tel.github.io/smartclock/fpga.html), not committed:
# rendered from the decodes, the references and the waveform figures,
# which it makes first (tools/fpga/Makefile, explainer).  Needs git,
# node and Icarus Verilog, not Yosys or pdftotext.
.PHONY: fpga
fpga:
	mkdir -p $(PAGES_DIR)
	$(MAKE) -f tools/fpga/Makefile explainer EXPLAINER_PAGE=$(abspath $(PAGES_DIR))/fpga.html

check-fpga-xapp: $(XC3000_VIEWER).commit-$(DECODER_COMMIT)
	$(call decoder_verify,$(XC3000_VIEWER),$(DECODER_COMMIT))
	tools/fpga/xact/xapp.sh $(XC3000_VIEWER)/html

# Fail if a published page scrolls sideways at desktop width, or the
# landing page at a phone's.  Needs
# Chrome; not part of ci.
check-pages: pages
	./tools/check-pages.sh $(PAGES_DIR)

# The firmware's checks and generated files, which
# third_party/firmware/Makefile runs, alone or from here by name, for
# IMAGE=... or every image: the C's format, the layouts, the
# decompiled C, each image's link from the hand-written C and assembly
# alone, the SCPI trees, the module, pForth and report figures,
# the 58503A's and Z3801A's diffs against the Z3805A and the names
# seeded from them, the addresses their C cites from the Z3805A's,
# Unicorn's recorded bugs, every image's chip-select tables, the
# hand-written C's magic numbers, the loop's traces, the images' shared
# C, rewritten/common/, the functions' names, none an image's, and each
# hand-written file compiled alone after its export's declarations; and,
# for one IMAGE, record-access, the accesses its code makes into a
# record, and report-link, what its link check finds missing.  They need
# an m68k gcc and binutils, clang, clang-format and uv; none is part of
# ci, and .github/workflows/firmware.yml runs the images' checks.
FIRMWARE_TARGETS = check-types format-c check-format-c check-decompiled check-alone check-link report-decompiled \
	scpi-tree check-scpi-tree handlers check-module-figures module-figures check-image-diff image-diff \
	check-seed-names seed-names check-comment-addresses check-common check-plain-names \
	check-pforth-words pforth-words check-report-figures report-figures check-unicorn \
	check-chip-selects chip-selects check-magic-numbers magic-numbers check-loop-trace loop-trace \
	record-access report-link
.PHONY: $(FIRMWARE_TARGETS)
$(FIRMWARE_TARGETS):
	$(MAKE) -C third_party/firmware $@

clean:
	$(CARGO) clean

# The crates carrying [package.metadata.deb]: smartclock-cli makes
# smartclock-utils, the tools, and smartclockd makes smartclockmon, the
# monitoring stack, which requires smartclock-utils of its own version.
DEB_CRATES := smartclock-cli smartclockd
DEB_MANIFESTS := $(DEB_CRATES:%=crates/%/Cargo.toml)

# Requires cargo-deb: cargo install cargo-deb
# Each package carries binaries of several crates, so build the whole
# workspace first and package without rebuilding.  cargo-deb warns that
# the asset paths are not under target/release/ and so will not be
# built; that is the point, --no-build means they are already there.
deb:
	$(CARGO) build --release --workspace
# -q keeps the output to the artifact paths.
# A snapshot overrides the version with its own stamp, so two builds of
# different code cannot share one, and its relations to its sibling are
# rewritten from the release version Cargo.toml names to that stamp.  A
# build standing on a release tag does neither: cargo-deb then takes
# Cargo.toml's version with revision 1, which is what CI does too, so
# the package built here and the package built from the tag are the
# same version rather than 0.1.0 against 0.1.0-1.
	@v=$$(./target/release/smartclockd --version | awk '{print $$2}'); \
	for crate in $(DEB_CRATES); do \
	  case "$$v" in \
	    *~git*) deb=$$($(CARGO) deb -p $$crate --no-build -q --deb-version "$$v") && \
	            ./packaging/retarget-deb.sh "$$deb" "$(CARGO_VERSION)-1" "$$v" && \
	            echo "$$deb" ;; \
	    *)      $(CARGO) deb -p $$crate --no-build -q ;; \
	  esac || exit 1; \
	done

# Cut a release: one version, in both places that must agree, tagged.
#
# The package version and the changelog are what apt compares, and the
# build stamp reads the tag, so all three have to move together.  Doing
# it by hand means one of them is eventually forgotten and an upgrade
# silently is not one.  Refuses a dirty tree, since the tag would name a
# commit that does not contain what was built.
#
#     make release
#
# The version comes from Cargo.toml, which the previous release already
# set to the one being prepared -- that is what "Open x.y.z for
# development" does.  Naming it again on the command line was a second
# source of truth for a number that is already written down, and
# nothing checked the two agreed.  `make release VERSION=x.y.z` still
# works, for releasing something other than the version in hand, and is
# refused if it disagrees with Cargo.toml.
CARGO_VERSION = $(shell sed -n '0,/^version = "\(.*\)"/s//\1/p' Cargo.toml)
VERSION ?= $(CARGO_VERSION)
# The version development continues on once VERSION is released: the
# next patch, so a snapshot sorts after the release it follows.
NEXT_VERSION = $(shell echo "$(VERSION)" | awk -F. '{printf "%d.%d.%d", $$1, $$2, $$3 + 1}')

release:
	@test -n "$(VERSION)" || { echo "cannot read a version from Cargo.toml" >&2; exit 2; }
	@echo "$(VERSION)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$' || \
	    { echo "VERSION must be x.y.z, not $(VERSION)" >&2; exit 2; }
	@test "$(VERSION)" = "$(CARGO_VERSION)" || \
	    { echo "VERSION $(VERSION) is not the $(CARGO_VERSION) in Cargo.toml" >&2; exit 2; }
	@echo "releasing $(VERSION), the version Cargo.toml already names"
	@test -z "$$(git status --porcelain)" || { echo "the tree is dirty" >&2; exit 2; }
	@git rev-parse -q --verify "refs/tags/v$(VERSION)" >/dev/null && \
	    { echo "v$(VERSION) already exists" >&2; exit 2; } || true
	sed -i '0,/^version = ".*"/s//version = "$(VERSION)"/' Cargo.toml
	printf '%s\n\n  * \n\n -- %s  %s\n\n%s' \
	    'smartclockmon ($(VERSION)-1) unstable; urgency=low' \
	    'Christopher Hoover <ch@murgatroid.com>' \
	    "$$(date -R)" \
	    "$$(cat packaging/debian/changelog)" > packaging/debian/changelog.new
	mv packaging/debian/changelog.new packaging/debian/changelog
	$$EDITOR packaging/debian/changelog
	$(CARGO) build --workspace
	git add Cargo.toml Cargo.lock packaging/debian/changelog
	git commit -m "Release $(VERSION)"
	git tag -a "v$(VERSION)" -m "Release $(VERSION)"
# Open the next patch version straight away.  A snapshot takes its
# version from Cargo.toml, so while that still names the version just
# released, every commit after the tag builds as 0.1.1-0~git..., which
# sorts BELOW 0.1.1-1: apt would offer to replace a newer dev build with
# the older release.  Moving to the next version means those snapshots
# read 0.1.2-0~git... and sit where they belong, after the release they
# follow and before the one they precede.  The changelog is not touched;
# there is nothing to say about a version nobody has released yet.
# The packages' relations to each other name the version too, as the
# release cargo-deb would make of it (crates/smartclockd/Cargo.toml).
	sed -i '0,/^version = ".*"/s//version = "$(NEXT_VERSION)"/' Cargo.toml
	sed -i 's/(\(=\|<<\) $(subst .,\.,$(VERSION))-1)/(\1 $(NEXT_VERSION)-1)/' $(DEB_MANIFESTS)
	$(CARGO) build --workspace
	git add Cargo.toml Cargo.lock $(DEB_MANIFESTS)
	git commit -m "Open $(NEXT_VERSION) for development"
	@echo
	@echo "Tagged v$(VERSION), and Cargo.toml now reads $(NEXT_VERSION)."
	@echo "Push the tag to build and publish:"
	@echo "    git push origin main && git push origin v$(VERSION)"

# The GitHub release's notes, as the release workflow sets them from the
# newest changelog entry; for checking before a tag is pushed.
release-notes:
	@./packaging/release-notes.sh

install-service:
	install -m 0644 packaging/systemd/smartclockd@.service /etc/systemd/system/
	install -m 0644 -b packaging/systemd/smartclockd.default /etc/default/smartclockd
	systemctl daemon-reload
