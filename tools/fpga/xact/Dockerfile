# XACT under headless DOSBox-X, and what checks its output: for
# rebuilding the test designs (build.sh) and the XAPP check (xapp.sh)
# where dosbox-x is not installed.  XACT and the XC3000 viewer are
# fetched when the image is built, into the image alone; nothing of
# Xilinx's is in the repository.
#
#     XACT_DOCKER=1 tools/fpga/xact/build.sh
#
# builds the image, with tools/fpga/decoder.mk's pin as DECODER_REPO and
# DECODER_COMMIT, and runs build.sh in it.
# The base image by digest; its packages are the archive's at build time.
FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        dosbox-x p7zip-full curl ca-certificates git nodejs python3 \
    && rm -rf /var/lib/apt/lists/*

# fetch.sh checks the archives against SHA256SUMS before unpacking them.
COPY fetch.sh SHA256SUMS /opt/xact-fetch/
ENV XACT_HOME=/opt/xact
RUN sh /opt/xact-fetch/fetch.sh && rm -rf "$XACT_HOME/download" "$XACT_HOME/base"

# The viewer netdump.js decodes with, at the commit the tools were
# checked against: tools/fpga/decoder.mk's pin, which build.sh passes.
ARG DECODER_REPO
ARG DECODER_COMMIT
ENV XC3000_VIEWER=/opt/xc3000
RUN test -n "$DECODER_REPO" && test -n "$DECODER_COMMIT" \
    && git clone -q "$DECODER_REPO" "$XC3000_VIEWER" \
    && git -C "$XC3000_VIEWER" checkout -q "$DECODER_COMMIT" \
    && test "$(git -C "$XC3000_VIEWER" rev-parse HEAD)" = "$DECODER_COMMIT"

# DOSBox-X writes its configuration under HOME, which a --user run lacks.
ENV HOME=/tmp
