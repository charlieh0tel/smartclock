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

.PHONY: all build windows hooks ci fmt fmt-check clippy test test-hw web-deps test-web screenshots doc docs check-pages check-types format-c check-format-c check-decompiled report-decompiled scpi-tree check-scpi-tree clean deb release release-notes install-service

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

# The project's own C, the Ghidra type headers and the hand-written
# firmware, in .clang-format's style; needs clang-format, not part of ci.
CLANG_FORMAT ?= clang-format
C_SOURCES = $(wildcard tools/ghidra/types/*.[ch] third_party/firmware/rewritten/*/*.[ch] third_party/firmware/rewritten/*/*/*.[ch])
format-c:
	$(CLANG_FORMAT) -i $(C_SOURCES)

check-format-c:
	$(CLANG_FORMAT) --dry-run -Werror $(C_SOURCES)

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
ONCORE_IMAGES := $(filter-out %/z3815a-4010.bin,$(wildcard third_party/firmware/*.bin))
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
# were taken with:
#   sg smartclockd -c 'make screenshots SHOTS_FROM="--log-dir /var/lib/smartclockd \
#     --log-dir /var/lib/smartclockd/archive --receiver 3710A01056 --span 86400 \
#     --stability-span 604800 --against efc:3625A01487" \
#     SHOTS_TUI="ssh -t apu-0 smartclockmon --daemon /run/smartclockd/ttyUSB0/socket"'
#   node tools/screenshots.mjs --url http://apu-0:9980 --receiver 3710A01056 \
#     --span 21600 --only web-correlation.png --only web-correlation-page.png
screenshots:
	node tools/screenshots.mjs --sim $(SHOTS_FROM) $(if $(SHOTS_TUI),--tui '$(SHOTS_TUI)')

doc:
	$(CARGO) doc --workspace --no-deps --all-features

# Regenerate the documentation that is derived from the command table.
# A test fails if docs/commands.md and the table disagree.
docs:
	$(CARGO) run -q -p smartclock-cli -- commands > docs/commands.md
	for image in third_party/firmware/*.bin; do \
		$(CARGO) run -q -p smartclock-cli -- dump-scpi $$image \
			> docs/scpi/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --models third_party/firmware/*.bin \
		> docs/scpi/models.md
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --models --html third_party/firmware/*.bin \
		> docs/scpi/models.html
	for image in third_party/firmware/*.bin; do \
		$(CARGO) run -q -p smartclock-cli -- dump-pforth $$image \
			> docs/firmware/pforth/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-pforth --models third_party/firmware/*.bin \
		> docs/firmware/pforth/models.md
	for image in $(ONCORE_IMAGES); do \
		$(CARGO) run -q -p smartclock-cli -- dump-oncore $$image \
			> docs/firmware/oncore/$$(basename $$image .bin).txt || exit 1; \
	done
	$(CARGO) run -q -p smartclock-cli -- dump-oncore --models $(ONCORE_IMAGES) \
		> docs/firmware/oncore/models.md

# Fail if an HTML page in docs/ scrolls sideways at desktop width.
# Needs Chrome; not part of ci.
check-pages:
	./tools/check-pages.sh

# Fail if a structure tools/ghidra/types/smartclock.h declares for
# Ghidra does not have the size and offsets the image shows.  Needs an
# m68k compiler, so that int, long and pointers are the target's; not
# part of ci.
M68K_CC ?= m68k-linux-gnu-gcc
check-types:
	$(M68K_CC) -mcpu=cpu32 -fsyntax-only tools/ghidra/types/check-layout.c

# Fail if the decompiled C does not compile, its assembly is not the
# image's own bytes, alone or linked, or a bridge disagrees with its
# routine.  Needs an m68k
# gcc and binutils, and uv; not part of ci.
DECOMPILED_CFLAGS = -std=gnu17 -mcpu=cpu32 -O1 -fwrapv -fno-toplevel-reorder -fno-builtin -ffunction-sections \
	-fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -w \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types
check-decompiled:
	$(M68K_CC) $(DECOMPILED_CFLAGS) -c -o target/decompiled.o third_party/firmware/decompiled/z3805a-3543b.c
	python3 tools/firmware/decompiled_check.py third_party/firmware/decompiled/z3805a-3543b.c \
		third_party/firmware/z3805a-3543b.bin target/decompiled.o
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q tools/firmware/rebuild_link.py \
		third_party/firmware/decompiled/z3805a-3543b.c third_party/firmware/z3805a-3543b.bin
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q tools/firmware/bridge_check.py \
		third_party/firmware/decompiled/z3805a-3543b.c third_party/firmware/z3805a-3543b.bin

# The Z3805A 3543B's SCPI tree as C data and as names-file rows, from
# the image (docs/scpi/README.md, "As C"); cargo test fails while they
# are stale.  Needs clang-format.
SCPI_TREE_C = third_party/firmware/rewritten/z3805a-3543b/scpi_tree_data.c
SCPI_TREE_NAMES = tools/ghidra/names/z3805a-3543b.scpi-tree.tsv
scpi-tree:
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --c \
		--names-file tools/ghidra/names/z3805a-3543b.tsv third_party/firmware/z3805a-3543b.bin \
		| $(CLANG_FORMAT) --assume-filename=$(SCPI_TREE_C) > $(SCPI_TREE_C)
	$(CARGO) run -q -p smartclock-cli -- dump-scpi --names \
		--names-file tools/ghidra/names/z3805a-3543b.tsv third_party/firmware/z3805a-3543b.bin \
		> $(SCPI_TREE_NAMES)

# Fail if a record of the generated SCPI tree, as m68k gcc lays it out,
# is not the image's bytes, its pointers compared by what they point at.
# Needs an m68k gcc and binutils; not part of ci.
check-scpi-tree:
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" python3 tools/firmware/scpi_tree_check.py \
		$(SCPI_TREE_C) $(SCPI_TREE_NAMES) tools/ghidra/names/z3805a-3543b.tsv \
		third_party/firmware/decompiled/z3805a-3543b.conventions.jsonl \
		third_party/firmware/z3805a-3543b.bin

# How many of the decompiled C functions, as gcc compiles them, agree with
# the image's own in the emulator; a measure, never a failure.
report-decompiled:
	DECOMPILED_CFLAGS="$(DECOMPILED_CFLAGS)" uv run -q tools/firmware/function_check.py \
		third_party/firmware/decompiled/z3805a-3543b.c third_party/firmware/z3805a-3543b.bin

clean:
	$(CARGO) clean

# Requires cargo-deb: cargo install cargo-deb
# The deb carries the tools, so build the whole workspace first
# and package without rebuilding.  cargo-deb warns that the asset paths
# are not under target/release/ and so will not be built; that is the
# point, --no-build means they are already there.
deb:
	$(CARGO) build --release --workspace
# -q keeps the output to the artifact path.
# A snapshot overrides the version with its own stamp, so two builds of
# different code cannot share one.  A build standing on a release tag
# does not: cargo-deb then takes the version from the changelog, which
# is what CI does too, so the package built here and the package built
# from the tag are the same version rather than 0.1.0 against 0.1.0-1.
	@v=$$(./target/release/smartclockd --version | awk '{print $$2}'); \
	case "$$v" in \
	  *~git*) $(CARGO) deb -p smartclockd --no-build -q --deb-version "$$v" ;; \
	  *)      $(CARGO) deb -p smartclockd --no-build -q ;; \
	esac

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
	sed -i '0,/^version = ".*"/s//version = "$(NEXT_VERSION)"/' Cargo.toml
	$(CARGO) build --workspace
	git add Cargo.toml Cargo.lock
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
