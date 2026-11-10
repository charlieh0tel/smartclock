"""A page's full-screen view of its inline SVG drawings, to pan and zoom:
the style, the button under each drawing that opens it, the one dialog
they open in and the one script that runs it.  The board block diagram
(tools/block_diagram.py) opens its diagram this way on a narrow window,
and the FPGA detail page (tools/fpga/detail.py) any of its drawings at
any width.

Without JavaScript the page is unchanged: the buttons stay hidden and
the drawings stay where they are.  With it, a click on a drawing or its
button copies that drawing into the dialog over the page, which a
pointer pans, two pointers, the wheel or the buttons zoom, and the
Close button or Escape closes, giving focus back; the page does not
scroll while it is open.
"""

# The style: MEDIA is the media query the buttons and the zoom cursor
# show under, FIGURE the figures' class.
CSS = """\
button { font: inherit; font-size: 14px; color: var(--ink); background: var(--panel); border: 1px solid var(--line);
  border-radius: 4px; padding: 5px 12px; cursor: pointer; }
button:focus-visible { outline: 2px solid var(--read); outline-offset: 2px; }
.zoom-open { display: none; margin-top: 8px; }
@media __MEDIA__ {
  .js .zoom-open { display: inline-block; }
  .js .__FIGURE__ svg { cursor: zoom-in; }
}
.zoom { position: fixed; inset: 0; z-index: 10; display: flex; flex-direction: column; background: var(--bg); }
.zoom[hidden] { display: none; }
.zoom-bar { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 8px 16px;
  background: var(--panel); border-bottom: 1px solid var(--line); }
.zoom-bar span { flex: 1 1 8em; font-size: 13px; color: var(--muted); }
.zoom-view { position: relative; flex: 1; overflow: hidden; touch-action: none; cursor: grab; }
.zoom-view.dragging { cursor: grabbing; }
.zoom-stage { position: absolute; left: 0; top: 0; transform-origin: 0 0; }
.zoom-stage svg { display: block; width: 100%; height: auto; }
body.zoomed { overflow: hidden; }
"""

# The dialog's bar and its view onto the stage the drawing is copied to.
BAR = ('  <div class="zoom-bar"><span>Drag to pan, pinch to zoom.</span>'
       '<button type="button" data-zoom="out" aria-label="Zoom out">\u2212</button>'
       '<button type="button" data-zoom="in" aria-label="Zoom in">+</button>'
       '<button type="button" data-zoom="fit">Reset</button>'
       '<button type="button" data-zoom="close">Close</button></div>')
VIEW = '  <div class="zoom-view"><div class="zoom-stage"></div></div>'

OPENER = '<button class="zoom-open" type="button">Open full screen</button>'

# The zoom's script: it copies the drawing clicked into the full-screen
# view, pans it with one pointer, zooms it with two, the wheel or the
# buttons, and lets a tap, a pointer that barely moved, through to a link.
SCRIPT = """\
(() => {
  "use strict";
  // The widths the zoom opens on, as the style's media query; the
  // largest and smallest zoom, over the drawing fitted to the window's
  // width; how far a pointer may move and still tap; a button's step.
  const NARROW = matchMedia("__MEDIA__");
  const MAX_ZOOM = 8, MIN_ZOOM = 0.5, TAP = 8, STEP = 1.5;
  document.documentElement.classList.add("js");
  const zoom = document.querySelector(".zoom"), view = zoom.querySelector(".zoom-view");
  const stage = zoom.querySelector(".zoom-stage");
  let copy = null, x = 0, y = 0, z = 1, returnTo = null, moved = 0;
  const pointers = new Map();
  let last = null;

  function apply() {
    stage.style.transform = `translate(${x}px, ${y}px) scale(${z})`;
  }
  function home() {
    // The drawing at the window's width, then zoomed to __HOME_WHAT__.
    stage.style.width = view.clientWidth + "px";
    z = __HOME_ZOOM__;
    x = 0;
    y = 0;
    apply();
  }
  function zoomAt(factor, cx, cy) {
    const next = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z * factor));
    x = cx - (cx - x) * next / z;
    y = cy - (cy - y) * next / z;
    z = next;
    apply();
  }
  function open(event, source) {
    if (!NARROW.matches) return;
    if (event) event.preventDefault();
    returnTo = document.activeElement;
    copy = source.cloneNode(true);
    stage.replaceChildren(copy);
    zoom.hidden = false;
    document.body.classList.add("zoomed");
    home();
    zoom.querySelector("[data-zoom=close]").focus();
  }
  function close() {
    zoom.hidden = true;
    document.body.classList.remove("zoomed");
    pointers.clear();
    if (returnTo && returnTo.focus) returnTo.focus();
  }
  for (const figure of document.querySelectorAll("__FIGURES__")) {
    const source = figure.querySelector("svg"), opener = figure.querySelector(".zoom-open");
    source.addEventListener("click", (event) => open(event, source));
    opener.addEventListener("click", (event) => open(event, source));
  }
  zoom.querySelector(".zoom-bar").addEventListener("click", (event) => {
    const button = event.target.closest("button");
    if (!button) return;
    const cx = view.clientWidth / 2, cy = view.clientHeight / 2;
    if (button.dataset.zoom === "close") close();
    else if (button.dataset.zoom === "in") zoomAt(STEP, cx, cy);
    else if (button.dataset.zoom === "out") zoomAt(1 / STEP, cx, cy);
    else if (button.dataset.zoom === "fit") home();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !zoom.hidden) close();
  });
  view.addEventListener("pointerdown", (event) => {
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (pointers.size === 1) moved = 0;
    last = null;
    view.classList.add("dragging");
  });
  view.addEventListener("pointermove", (event) => {
    const p = pointers.get(event.pointerId);
    if (!p) return;
    const dx = event.clientX - p.x, dy = event.clientY - p.y;
    p.x = event.clientX;
    p.y = event.clientY;
    moved += Math.abs(dx) + Math.abs(dy);
    if (moved > TAP && !view.hasPointerCapture(event.pointerId)) view.setPointerCapture(event.pointerId);
    if (pointers.size === 1) {
      x += dx;
      y += dy;
      apply();
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      const rect = view.getBoundingClientRect();
      const spread = Math.hypot(a.x - b.x, a.y - b.y);
      const cx = (a.x + b.x) / 2 - rect.left, cy = (a.y + b.y) / 2 - rect.top;
      if (last) zoomAt(spread / last, cx, cy);
      last = spread;
    }
  });
  function release(event) {
    pointers.delete(event.pointerId);
    last = null;
    if (!pointers.size) view.classList.remove("dragging");
  }
  view.addEventListener("pointerup", release);
  view.addEventListener("pointercancel", release);
  view.addEventListener("wheel", (event) => {
    event.preventDefault();
    const rect = view.getBoundingClientRect();
    zoomAt(event.deltaY < 0 ? 1.1 : 1 / 1.1, event.clientX - rect.left, event.clientY - rect.top);
  }, { passive: false });
  stage.addEventListener("click", (event) => {
    // A drag is not a tap; a tap on a link in the drawing closes the
    // view and follows it.
    if (moved > TAP) {
      event.preventDefault();
      return;
    }
    if (event.target.closest("a")) close();
  });
  addEventListener("resize", () => {
    if (!zoom.hidden && !NARROW.matches) close();
  });
})();
"""


def css(media, figure_class):
    """The style, the buttons and cursor shown under media."""
    return CSS.replace("__MEDIA__", media).replace("__FIGURE__", figure_class)


def dialog(label):
    """The page's one dialog's lines, labeled for a screen reader."""
    return [f'<div class="zoom" role="dialog" aria-modal="true" aria-label="{label}" hidden>', BAR, VIEW, "</div>"]


def script(media, figures, home_what="twice that", home_zoom="2"):
    """The page's one script, for every figure figures selects: it opens
    under media, at the window's width zoomed by home_zoom, a JavaScript
    expression over the drawing's copy, copy, which home_what says in
    words."""
    return (SCRIPT.replace("__MEDIA__", media).replace("__FIGURES__", figures)
            .replace("__HOME_WHAT__", home_what).replace("__HOME_ZOOM__", home_zoom))
