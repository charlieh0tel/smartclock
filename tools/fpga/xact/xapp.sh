#!/bin/sh
# Build the XC3000-family application-note designs on the XACT CD with
# MakeBits and check netdump.js's decode of each against its LCA, as
# roundtrip.py does for ours.  The designs are Xilinx's, so they stay in
# the fetched tree and are never committed; this is a local check, not
# CI's.  Needs fetch.sh's tree, dosbox-x and node.
#
#     tools/fpga/xact/xapp.sh VIEWER_HTML_DIR [XAPP_DIR_GLOB]
#
# XAPP_DIR_GLOB, under the CD's XAPP/, picks the notes (default XAPP003*,
# the XC3042A counters).
set -e
if [ $# -lt 1 ]; then
    echo "usage: xapp.sh VIEWER_HTML_DIR [XAPP_DIR_GLOB]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
viewer=$(cd "$1" && pwd)
xapp=${XACT_HOME:-$root/target/xact}/iso_960628/XAPP
work=$root/target/xact/xapp
if [ ! -d "$xapp" ]; then
    echo "xapp.sh: no $xapp; tools/fpga/xact/fetch.sh fetches it" >&2
    exit 1
fi
rm -rf "$work"
mkdir -p "$work"

# The viewer's package names for XACT's.
package() {
    case $1 in
        *PC84) echo PLCC84 ;; *PC68) echo PLCC68 ;; *PC44) echo PLCC44 ;;
        *PG84) echo PGA84 ;; *PQ100) echo PQFP100 ;; *PG132) echo PGA132 ;;
        *PQ160) echo PQFP160 ;; *PG175) echo PGA175 ;; *PQ208) echo PQFP208 ;;
    esac
}

count=0
failed=0
for lca in "$xapp"/${2:-XAPP003*}/*/*.LCA "$xapp"/${2:-XAPP003*}/*.LCA; do
    [ -f "$lca" ] || continue
    part=$(sed -n 's/^Design \([^ \r]*\).*/\1/p' "$lca" | head -n 1)
    case $part in 30*|31*) ;; *) continue ;; esac
    note=$(basename "$(dirname "$lca")")
    dir=$work/$(basename "$(dirname "$(dirname "$lca")")")-$note-$(basename "$lca" .LCA)
    mkdir -p "$dir"
    cp "$lca" "$dir/DESIGN.LCA"
    "$here/run-xact.sh" "$dir" 'makebits DESIGN.LCA > MB.OUT'
    count=$((count + 1))
    if [ ! -s "$dir/DESIGN.BIT" ]; then
        echo "$note/$(basename "$lca"): MakeBits wrote no bitstream" >&2
        failed=$((failed + 1))
        continue
    fi
    node "$here/../netdump.js" "$viewer" "$dir/DESIGN.BIT" "$(package "$part")" > "$dir/DESIGN.json" 2> "$dir/netdump.err"
    if ! python3 -I "$here/../lcacheck.py" "$dir/DESIGN.LCA" "$dir/DESIGN.json" > "$dir/check.txt"; then
        failed=$((failed + 1))
        cat "$dir/check.txt"
    fi
    echo "$note/$(basename "$lca") ($part): $(tail -n 1 "$dir/check.txt" | sed 's/.*; //')"
done
echo "xapp.sh: $((count - failed)) of $count designs agree with XACT"
if [ "$count" -eq 0 ]; then
    echo "xapp.sh: no XC3000-family design under $xapp/${2:-XAPP003*}" >&2
    exit 1
fi
[ "$failed" -eq 0 ]
