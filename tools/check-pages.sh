#!/bin/sh
# Load each HTML page under the directories given (make check-pages
# gives the published pages') in headless Chrome at desktop widths and
# fail if the page is wider than the window or any box on it scrolls
# sideways; the landing page, index.html, at a phone's width too.  A
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

# Reports, after load, the page's overflow and each element allowed to
# scroll sideways whose content is wider than it.
probe='<script>addEventListener("load",()=>setTimeout(()=>{const bad=[];
if(document.documentElement.scrollWidth>innerWidth+1)bad.push("page "+document.documentElement.scrollWidth+">"+innerWidth);
for(const e of document.querySelectorAll("*")){const o=getComputedStyle(e).overflowX;
if((o==="auto"||o==="scroll")&&e.scrollWidth>e.clientWidth+1)bad.push((e.id||e.className||e.tagName)+" "+e.scrollWidth+">"+e.clientWidth);}
const p=document.createElement("pre");p.id="check-pages";p.textContent=bad.length?bad.join("; "):"ok";document.body.appendChild(p);},300));</script>'

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
    widths=$WIDTHS
    if [ "$(basename "$page")" = index.html ]; then
        widths="$WIDTHS $PHONE_WIDTH"
    fi
    for width in $widths; do
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
