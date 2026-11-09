#!/bin/sh
# Rebuild the test designs' routed LCAs and bitstreams with XACT, in
# headless DOSBox-X: each designs/NAME.lca through APR ("apr -l -p",
# keep the hand placement, route every net) and MakeBits, into
# designs/NAME.routed.lca and designs/NAME.bit.  A design with no nets,
# or one that routes its own (NProgram), skips APR: APR swaps a block's
# pins and rewrites a mode-F block of four inputs as mode FG, which a
# design testing those cannot have.  Needs fetch.sh's XACT tree and dosbox-x; XACT_DOCKER=1 runs
# it all in the image the Dockerfile builds instead (README.md).
#
#     tools/fpga/xact/build.sh [NAME ...]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
designs=$here/designs

if [ -n "$XACT_DOCKER" ]; then
    image=smartclock-xact
    pin() { sed -n "s/^$1 *?= *//p" "$here/../decoder.mk"; }
    docker build -q -t "$image" -f "$here/Dockerfile" \
        --build-arg DECODER_REPO="$(pin DECODER_REPO)" --build-arg DECODER_COMMIT="$(pin DECODER_COMMIT)" \
        "$here" > /dev/null
    exec docker run --rm --user "$(id -u):$(id -g)" -v "$root:/src" -w /src "$image" \
        tools/fpga/xact/build.sh "$@"
fi

if [ $# -eq 0 ]; then
    set -- $(for source in "$designs"/*.lca; do
        case $source in *.routed.lca) ;; *) basename "$source" .lca ;; esac
    done)
fi

work=$root/target/xact/build
rm -rf "$work"
mkdir -p "$work"
# DOS names: 8.3, upper case; NAME's routed design is NAMER.LCA.
commands=
for name in "$@"; do
    dos=$(echo "$name" | tr a-z A-Z)
    if [ ${#dos} -gt 7 ]; then
        echo "build.sh: $name: a design name has at most 7 characters" >&2
        exit 2
    fi
    sed 's/$/\r/' "$designs/$name.lca" > "$work/$dos.LCA"
    # A design with nets is routed, unless it routes them itself.
    if grep -q '^Addnet' "$designs/$name.lca" && ! grep -q '^NProgram' "$designs/$name.lca"; then
        commands="$commands|apr -l -p -w -o ${dos}A.OUT $dos.LCA ${dos}R.LCA"
    else
        cp "$work/$dos.LCA" "$work/${dos}R.LCA"
    fi
    commands="$commands|makebits ${dos}R.LCA > $dos.MB"
done
# One DOSBox-X session runs them all.
(IFS='|'; set -f; set -- ${commands#|}; "$here/run-xact.sh" "$work" "$@")

for name in "$@"; do
    dos=$(echo "$name" | tr a-z A-Z)
    if [ ! -s "$work/${dos}R.BIT" ]; then
        echo "build.sh: $name: no bitstream; see $work/${dos}A.OUT and $work/$dos.MB" >&2
        exit 1
    fi
    if [ -f "$work/${dos}A.OUT" ] && ! grep -q "There are 0 unrouted load pins" "$work/${dos}A.OUT"; then
        echo "build.sh: $name: APR left pins unrouted; see $work/${dos}A.OUT" >&2
        exit 1
    fi
    cp "$work/${dos}R.LCA" "$designs/$name.routed.lca"
    cp "$work/${dos}R.BIT" "$designs/$name.bit"
    echo "build.sh: $name"
done
