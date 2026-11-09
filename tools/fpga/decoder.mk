# The XC3000 viewer whose decoder the FPGA tools read, pinned to the
# commit `make check-fpga-decoder` validates against XACT
# (docs/firmware/outputs.md, "Netlists").  The top Makefile,
# tools/fpga/Makefile and tools/fpga/xact/build.sh (the Dockerfile's
# build arguments) read the pin here, so it is in one place.
DECODER_REPO   ?= https://github.com/Arisotura/xc3000
DECODER_COMMIT ?= 8563397909b54c6f5efdcd78eb8a1a3c6fd5b3d8
# Upstream pull request #3, which changes the viewer's decoder:
# tools/fpga/Makefile's check-decoder-upstream decodes every design at it
# too and compares the two, and checks XACT's test designs there.
DECODER_UPSTREAM_COMMIT ?= 02c1a5657dbb7d2d78530e934aaaa282c7c05bef

# That the clone at $(1) is at commit $(2) with no local change: run on
# every decode and every `check`, not only when the stamp is made, so
# that a later checkout or an edit in the clone fails.
define decoder_verify
	@test "$$(git -C $(1) rev-parse HEAD)" = $(2) \
	  || { echo "decoder.mk: $(1) is not at $(2)" >&2; exit 1; }
	@test -z "$$(git -C $(1) status --porcelain)" \
	  || { echo "decoder.mk: $(1) has local changes" >&2; exit 1; }
endef

# The clone at $(1), checked out at commit $(2) and verified, then the
# stamp $(1).commit-$(2), which what reads the clone depends on: a new
# pin checks out again.  The commit is fetched by its hash, which
# reaches one only a pull request's ref holds.
define decoder_checkout
	@rm -f $(1).commit-*
	@if [ ! -d $(1)/.git ]; then rm -rf $(1) && git clone -q $(DECODER_REPO) $(1); fi
	@git -C $(1) cat-file -e $(2)^{commit} 2>/dev/null || git -C $(1) fetch -q origin $(2)
	git -C $(1) checkout -q $(2)
	$(call decoder_verify,$(1),$(2))
	@touch $(1).commit-$(2)
endef
