#!/bin/sh
# Run netdump.js: with node if there is one, else in headless Chrome
# through netdump.html ($CHROME, google-chrome by default).
#
#     tools/fpga/netdump.sh target/fpga/xc3000/html third_party/firmware/images/fpga/z3805a.bit > z3805a.json
#
# Paths must not need URL escaping.
set -e
if [ $# -lt 2 ]; then
    echo "usage: netdump.sh VIEWER_HTML_DIR FILE.bit [PACKAGE]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
viewer=$(cd "$1" && pwd)
file=$(realpath "$2")
package=${3:-PLCC84}
if command -v node >/dev/null 2>&1; then
    exec node "$here/netdump.js" "$viewer" "$file" "$package"
fi
profile=$(mktemp -d)
trap 'rm -rf "$profile"' EXIT
"${CHROME:-google-chrome}" --headless=new --disable-gpu --allow-file-access-from-files \
    --user-data-dir="$profile" --virtual-time-budget=60000 \
    --dump-dom "file://$here/netdump.html?viewer=file://$viewer&bit=file://$file&package=$package" \
    2>/dev/null |
python3 -I -c '
import html, json, re, subprocess, sys
match = re.search(r"<pre id=\"out\">(.*)</pre>", sys.stdin.read(), re.S)
if not match or not match.group(1):
    sys.exit("netdump.sh: the page wrote nothing")
result = json.loads(html.unescape(match.group(1)))
def git(*args):
    return subprocess.run(["git", "-C", sys.argv[1], *args], capture_output=True, text=True, check=True).stdout.strip()
try:
    dirty = "-dirty" if git("status", "--porcelain") else ""
    result["decoder"] = {"url": git("remote", "get-url", "origin"), "commit": git("rev-parse", "HEAD") + dirty}
except (OSError, subprocess.CalledProcessError):
    result["decoder"] = {"url": "unknown", "commit": "unknown"}
json.dump(result, sys.stdout, indent=1)
print()
if "error" in result:
    sys.exit("netdump.sh: " + result["error"])
' "$viewer"
