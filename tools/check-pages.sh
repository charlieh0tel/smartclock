#!/bin/sh
# Load each HTML page under the directories given (make check-pages
# gives the published pages') in headless Chrome at desktop widths and
# fail if the page is wider than the window or any box on it scrolls
# sideways; at a phone's width too, where a box marked data-scroll="x"
# may scroll and anything wider than the window must be inside one
# (tools/check-pages-probe.js).  A
# page forwarding to another, which docs/ keeps at a published page's
# old path, is skipped.  Headless screenshots hide scrollbars, so this
# measures instead of looking.  Needs google-chrome or chromium; not run in CI.
set -eu

WIDTHS="1920 1280 1000"
PHONE_WIDTH=390

chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)
if [ -z "$chrome" ]; then
    echo "check-pages: no google-chrome or chromium found" >&2
    exit 2
fi

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

probe="<script>$(cat "$(dirname "$0")/check-pages-probe.js")</script>"

# Headless Chrome's window is never narrower than 500 pixels, so a
# phone's width is a frame of that width the page is loaded in, its
# probe's report copied out to the frame's parent.
frame='<!doctype html><iframe id="page" style="border:0;width:__WIDTH__px;height:1000px" src="__PAGE__"></iframe>
<script>addEventListener("load",()=>setTimeout(()=>{const p=document.createElement("pre");p.id="check-pages";
const r=document.getElementById("page").contentDocument?.getElementById("check-pages");
p.textContent=r?r.textContent:"no result from the frame";document.body.appendChild(p);},600));</script>'

status=0
for page in $(find "$@" -name "*.html" | sort); do
    if grep -q 'http-equiv="refresh"' "$page"; then
        continue
    fi
    copy="$scratch/$(basename "$page")"
    # By position, not sub(): the probe holds "&", which sub() expands.
    PROBE="$probe" awk '{ i = index($0, "</body>"); if (i) $0 = substr($0, 1, i - 1) ENVIRON["PROBE"] substr($0, i); print }' "$page" > "$copy"
    for width in $WIDTHS $PHONE_WIDTH; do
        load=$copy
        if [ "$width" = "$PHONE_WIDTH" ]; then
            load="$scratch/phone-$(basename "$page")"
            printf '%s\n' "$frame" | sed "s|__WIDTH__|$width|; s|__PAGE__|$(basename "$copy")|" > "$load"
        fi
        result=$("$chrome" --headless=new --disable-gpu --window-size="$width,1000" \
            --allow-file-access-from-files \
            --virtual-time-budget=4000 --dump-dom "file://$load" 2>/dev/null \
            | sed -n 's/.*<pre id="check-pages">\(.*\)<\/pre>.*/\1/p' | sed 's/&gt;/>/g; s/&lt;/</g; s/&amp;/\&/g')
        if [ "$result" = "ok" ]; then
            echo "ok    $page at $width"
        else
            echo "FAIL  $page at $width: ${result:-no result}"
            status=1
        fi
    done
done
exit $status
