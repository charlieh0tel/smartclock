#!/bin/sh
# Run DOS commands under headless DOSBox-X, with XACT on C: (read only)
# and a work directory on D:.  Each command's output goes where the
# command itself redirects it (DOS redirection, to a file in WORKDIR).
#
#     tools/fpga/xact/run-xact.sh WORKDIR 'makebits -b TEST.LCA > MB.OUT' ...
#
# XACT_TREE is the XACT tree, the 6.0.1 CD's (fetch.sh unpacks it), whose
# apr and makebits need no key.  XACT_TIMEOUT, in seconds (300), stops a
# tool that waits for input, as xdelay with no arguments does.  Nothing
# from the archive runs outside DOSBox-X.
set -e
if [ $# -lt 2 ]; then
    echo "usage: run-xact.sh WORKDIR 'COMMAND' ..." >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
work=$(realpath "$1")
shift
tree=${XACT_TREE:-${XACT_HOME:-$root/target/xact}/iso_960628/XACT}
if [ ! -d "$tree" ]; then
    echo "run-xact.sh: no XACT tree at $tree; run tools/fpga/xact/fetch.sh" >&2
    exit 1
fi
conf=$(mktemp --suffix=.conf)
trap 'rm -f "$conf"' EXIT
{
    cat "$here/dosbox-x.conf"
    echo "[autoexec]"
    echo "mount c \"$tree\" -ro"
    echo "mount d \"$work\""
    echo "set XACT=C:\\"
    echo "set PATH=C:\\;Z:\\"
    echo "d:"
    for command in "$@"; do echo "$command"; done
    echo "exit"
} > "$conf"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "${XACT_TIMEOUT:-300}" dosbox-x -conf "$conf" -nogui -nomenu -silent -exit -fastlaunch \
    > "$work/dosbox.log" 2>&1
