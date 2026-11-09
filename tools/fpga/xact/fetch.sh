#!/bin/sh
# Download Xilinx XACT 5.2/6.0.x from archive.org, check it against
# SHA256SUMS, and unpack the 6.0.1 CD's XACT and XAPP trees into
# XACT_HOME (target/xact by default, which git ignores).  Idempotent:
# what is already there and checked is kept.  Nothing from the archives
# is run here; XACT runs only in DOSBox-X (run-xact.sh).
#
#     tools/fpga/xact/fetch.sh
#
# The item: https://archive.org/details/1995-1996-xilinx-xact-520-600-601.
# Its second archive, the patches, is fetched and checked as the item's
# own record but not unpacked: the CD's apr and makebits need no key.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
home=${XACT_HOME:-$root/target/xact}
base=https://archive.org/download/1995-1996-xilinx-xact-520-600-601
cd_image=cd03__XACT_STEP_960628.iso

mkdir -p "$home/download"
cd "$home/download"
while read -r _ file; do
    if ! grep " $file\$" "$here/SHA256SUMS" | sha256sum -c --status 2>/dev/null; then
        curl -fSL --retry 3 -o "$file" "$base/$file"
    fi
done < "$here/SHA256SUMS"
sha256sum -c "$here/SHA256SUMS"

cd "$home"
if [ ! -f "base/$cd_image" ]; then
    7z x -y -obase download/1995_1996_xilinx_xact_520_600_601.7z "$cd_image" > /dev/null
fi
if [ ! -d iso_960628/XACT ] || [ ! -d iso_960628/XAPP ]; then
    rm -rf iso_960628
    7z x -y -oiso_960628 "base/$cd_image" XACT XAPP > /dev/null
fi
echo "fetch.sh: XACT in $home/iso_960628"
