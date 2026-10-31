"""Renders docs/board-atlas.html, the first-family SmartClock main
board's atlas, from docs/board-atlas.json, and checks the page against
it, or with --write rewrites the page.

    python3 -I tools/board_atlas.py [--write]

The page is whole without JavaScript: the three photos stacked with
every part ringed, the parts table, the nets and the notes.  Its one
inline script, which htmlpreview.github.io runs too, only adds to that:
view tabs, zoom and pan, a card for the selected part, filters, a
search and a #<part> deep link.  The script reads every part from its
table row, so the data reaches the page once.  It states no counts.

The data, one JSON object:

    title, heading     the page's <title> and <h1>
    intro, tags        the header's paragraphs and tags
    confidence         the confidence marks, {id, text}, in legend order
    comparison         the Z3801A and 58503A columns' marks, likewise
    views              the photos, {id, title, short (the parts table's
                       name for it), image (relative to docs/), width,
                       height (pixels), alt, caption}
    groups             the parts table's sections, {id, title}, in order
    parts              each part: ref, group, marking, type, role,
                       firmware, note; id_confidence (the designator and
                       marking) and role_confidence, each a confidence
                       id; z3801a and 58503a, each {status, note};
                       at, {view id: {x, y}}, the ring's center in
                       percent of the photo's width and height
    nets_intro, nets   the measured nets, {net, path, note}
    design_note        paragraphs after the nets
    observations       paragraphs: across the three boards
    limits             paragraphs: what the photos cannot show
    open               the open items

Prose takes three marks: `code`; [[path]] or [[path|section]], a
citation of a file in the repository, linked on GitHub without a
fragment (htmlpreview points a fragment at the page itself), whose path
must exist and whose section must be a heading of that Markdown file,
or "item N" for hardware-investigations.md's numbered items; and
{{ref}}, a link to a part's row.
"""

import argparse
import difflib
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "docs" / "board-atlas.json"
PAGE = ROOT / "docs" / "board-atlas.html"
BLOB = "https://github.com/charlieh0tel/smartclock/blob/main/"
RAW = "https://raw.githubusercontent.com/charlieh0tel/smartclock/main/docs/"
CITE = re.compile(r"\[\[([^|\]]+)(?:\|([^\]]+))?\]\]")
CODE = re.compile(r"`([^`]+)`")
REF = re.compile(r"\{\{([^}]+)\}\}")
ITEM = re.compile(r"item (\d+)$")
COMPARED = ("z3801a", "58503a")
COMPARED_TITLES = {"z3801a": "Z3801A", "58503a": "58503A"}

CSS = """\
/* Layout: the photos, each with its ringed parts, then the measured
   nets, the parts table grouped by subsystem and the notes.  Tables sit
   in their own scroll boxes so the page never widens. */
:root {
  --bg: #eef0ee; --panel: #ffffff; --ink: #1b2320; --muted: #5a6661; --line: #d3d9d6; --hi: #fff4c2; --well: #d5dbd8;
  --board: #1f7a6c; --read: #2f5fd0; --inferred: #a26a00; --open: #c8401f;
  --same: #1f7a6c; --fitted: #2f5fd0; --differs: #a26a00;
  color-scheme: light;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418; --well: #0b0f0d;
    --board: #4cc3ae; --read: #7d9ff0; --inferred: #e6ac3a; --open: #ff7650;
    --same: #4cc3ae; --fitted: #7d9ff0; --differs: #e6ac3a;
    color-scheme: dark;
  }
}
:root[data-theme="dark"] {
  --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418; --well: #0b0f0d;
  --board: #4cc3ae; --read: #7d9ff0; --inferred: #e6ac3a; --open: #ff7650;
  --same: #4cc3ae; --fitted: #7d9ff0; --differs: #e6ac3a;
  color-scheme: dark;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.5 "IBM Plex Sans", system-ui, sans-serif; }
.wrap { max-width: 1240px; margin: 0 auto; padding-inline: 16px; padding-block: 24px 48px; display: grid; gap: 24px; }
header h1 { font-size: 1.6rem; margin: 0; text-wrap: balance; font-weight: 600; }
header p { margin: 6px 0 0; color: var(--muted); max-width: 80ch; }
h2 { font: 600 12px/1.4 "IBM Plex Mono", monospace; letter-spacing: .08em; text-transform: uppercase; margin: 0 0 8px; color: var(--muted); }
a { color: inherit; }
code { font: 13px "IBM Plex Mono", monospace; }
.tags { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
.tag { font: 600 11px/1 "IBM Plex Mono", monospace; letter-spacing: .06em; text-transform: uppercase; padding: 6px 8px; border: 1px solid var(--line); border-radius: 4px; background: var(--panel); }
.legend { display: flex; flex-wrap: wrap; gap: 6px 16px; font-size: 13px; color: var(--muted); margin: 0; padding: 0; list-style: none; }
.legend li { display: flex; align-items: center; gap: 6px; }
.views { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 440px), 1fr)); gap: 20px; align-items: start; }
.views figure.wide { grid-column: 1 / -1; }
figure { margin: 0; display: grid; gap: 8px; min-width: 0; }
figcaption { font-size: 14px; color: var(--muted); }
.photo { position: relative; width: 100%; border-radius: 6px; overflow: hidden; border: 1px solid var(--line);
  background-size: 100% 100%; background-repeat: no-repeat; }
.pin { position: absolute; width: 0; height: 0; text-decoration: none; }
.ring { position: absolute; left: -8px; top: -8px; width: 16px; height: 16px; border-radius: 50%;
  border: 3px solid var(--c); box-shadow: 0 0 0 1px rgba(0, 0, 0, .7), inset 0 0 0 1px rgba(0, 0, 0, .7); }
.lbl { position: absolute; left: 10px; top: 0; transform: translateY(-50%); white-space: nowrap;
  font: 600 11px/1 "IBM Plex Mono", monospace; color: #fff; background: rgba(0, 0, 0, .62); padding: 2px 3px; border-radius: 3px; }
.pin:hover .lbl, .pin:focus-visible .lbl { background: #000; outline: 2px solid var(--c); }
@media (max-width: 600px) { .lbl { font-size: 9px; padding: 1px 2px; } .ring { left: -6px; top: -6px; width: 12px; height: 12px; border-width: 2px; } .lbl { left: 7px; } }
.dot { display: inline-block; width: 12px; height: 12px; border-radius: 50%; border: 3px solid var(--c); flex: none; }
.c-board { --c: var(--board); } .c-read { --c: var(--read); } .c-inferred { --c: var(--inferred); } .c-open { --c: var(--open); }
.c-same { --c: var(--same); } .c-fitted { --c: var(--fitted); } .c-differs { --c: var(--differs); }
.mark { font: 600 11px/1.6 "IBM Plex Mono", monospace; color: var(--c); white-space: nowrap; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; min-width: 0; }
.card p { margin: 0 0 8px; max-width: 90ch; }
.card p:last-child { margin: 0; }
.card ul { margin: 0; padding-left: 20px; display: grid; gap: 6px; max-width: 90ch; }
.notes { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 340px), 1fr)); gap: 16px; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; font-size: 14px; width: 100%; }
th, td { text-align: left; vertical-align: top; padding: 5px 12px 5px 0; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: 500; }
tbody th.group { font: 600 12px/1.4 "IBM Plex Mono", monospace; letter-spacing: .08em; text-transform: uppercase; color: var(--ink); padding-top: 18px; }
td.ref { white-space: nowrap; font: 600 13px "IBM Plex Mono", monospace; }
td.ref small { display: block; font: 400 12px "IBM Plex Sans", system-ui, sans-serif; color: var(--muted); white-space: normal; }
td.marking { font: 12.5px "IBM Plex Mono", monospace; }
/* Wide enough to read on a phone, inside its scroll box; narrow enough
   for a 1000 px window. */
.parts { font-size: 13px; min-width: 880px; }
.parts th, .parts td { padding-right: 8px; }
.parts code { overflow-wrap: anywhere; }
td small, .note { color: var(--muted); font-size: 13px; }
tr[id] { scroll-margin-top: 12px; }
tr:target { background: var(--hi); }
footer { color: var(--muted); font-size: 13px; }
/* With the script: the toolbar, one photo at a time beside the selected
   part's card, the card below the photo on a narrow window.  The photo
   is a fixed window onto a stage, the image and its rings, that the
   script zooms and pans; each ring scales back by the zoom about its
   own center, the part's point, so it keeps its size and its place. */
.js [hidden] { display: none !important; }
.js .atlas { display: grid; grid-template-columns: minmax(0, 1fr) minmax(260px, 340px); gap: 16px 20px; align-items: start; }
.js .tools { grid-column: 1 / -1; display: flex; flex-wrap: wrap; gap: 10px 16px; align-items: end; }
.js .tools label { display: grid; gap: 2px; font-size: 12px; color: var(--muted); flex: 1 1 150px; min-width: 0; }
.js .tools label.search { flex-basis: 220px; }
.js .tools p { margin: 0; flex: 1 1 100%; font-size: 13px; color: var(--muted); }
.js .tools p.none { color: var(--open); }
.tabs, .zoom { display: flex; flex-wrap: wrap; gap: 4px; }
.js button, .js select, .js input { font: inherit; font-size: 14px; color: var(--ink); background: var(--panel); border: 1px solid var(--line); border-radius: 4px; padding: 5px 10px; min-width: 0; }
.js button { cursor: pointer; }
.js .tabs button[aria-selected="true"] { background: var(--ink); color: var(--bg); border-color: var(--ink); }
.js :is(button, select, input, a, .photo):focus-visible { outline: 2px solid var(--read); outline-offset: 2px; }
.js .views { display: block; }
.js .views figure > h2 { display: none; }
.js .photo { overflow: clip; max-height: 78vh; background-color: var(--well); touch-action: pan-y; }
.js .photo.zoomed { touch-action: none; cursor: grab; }
.js .photo.dragging { cursor: grabbing; }
.stage { position: absolute; left: 0; top: 0; transform-origin: 0 0; background-size: 100% 100%; background-repeat: no-repeat; }
.js .pin { transform: scale(calc(1 / var(--z, 1))); }
.js .stage.glide, .js .stage.glide .pin { transition: transform .3s ease; }
.js .pin.out { display: none; }
.js .pin:focus-visible { outline: none; }
.js .pin:focus-visible .ring { outline: 2px solid #fff; outline-offset: 2px; }
.pin.sel { z-index: 2; }
.pin.sel .ring { box-shadow: 0 0 0 2px #fff, 0 0 0 3px #000; }
.pin.sel .lbl { background: #000; outline: 2px solid var(--c); }
.pin.flash .ring { animation: flash .8s ease-out 3; }
@keyframes flash {
  from { box-shadow: 0 0 0 2px #fff, 0 0 0 3px #000, 0 0 0 3px var(--c); }
  to { box-shadow: 0 0 0 2px #fff, 0 0 0 3px #000, 0 0 0 20px transparent; }
}
.js .parts tr[data-part] { cursor: pointer; }
.js .parts tr.sel { background: var(--hi); }
.js button.refbtn { font: inherit; padding: 0; border: 0; background: none; color: inherit; text-decoration: underline dotted; text-underline-offset: 3px; }
.js .detail { position: sticky; top: 12px; max-height: calc(100vh - 24px); overflow: hidden auto; overflow-wrap: anywhere; }
.detail h3 { font: 600 20px/1.2 "IBM Plex Mono", monospace; margin: 0 0 8px; }
.detail dl { margin: 0 0 10px; }
.detail dt { font: 600 11px/1.4 "IBM Plex Mono", monospace; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
.detail dd { margin: 0 0 8px; font-size: 14px; }
.detail .on { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.detail .on button[aria-pressed="true"] { border-color: var(--ink); }
.detail .actions { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 10px; }
@media (max-width: 860px) {
  .js .atlas { grid-template-columns: minmax(0, 1fr); }
  .js .detail { position: static; max-height: none; overflow: visible; }
}
@media (prefers-reduced-motion: reduce) {
  .pin.flash .ring { animation: none; }
  .js .stage.glide, .js .stage.glide .pin { transition: none; }
}
"""

# The page's behavior, all of it optional: without it the page is the
# stacked photos and the table.  It reads each part from its table row
# and each ring's point from its style, so it holds no data of its own.
SCRIPT = """\
(function () {
  "use strict";
  // Zoom on selecting a part, if less; a step of the buttons and keys;
  // the most, past the photo's own pixels; a wheel notch's rate; an
  // arrow key's pan, of the window; a drag's least movement, in pixels.
  const FOCUS_ZOOM = 2.5, ZOOM_STEP = 1.5, OVERZOOM = 1.5, MIN_MAX_ZOOM = 4;
  const WHEEL_RATE = 0.0015, LINE_PIXELS = 16, PAN_STEP = 0.1, DRAG_SLOP = 4;
  const SLUG = /^[A-Za-z0-9-]+$/, ROW_PREFIX = "part-";
  const atlas = document.querySelector(".atlas");
  if (!atlas || !window.ResizeObserver) return;
  document.documentElement.classList.add("js");
  const tools = atlas.querySelector(".tools"), detail = atlas.querySelector(".detail");
  const body = detail.querySelector(".detail-body"), hint = body.innerHTML;
  const none = tools.querySelector(".none");
  const byGroup = tools.querySelector("[name=group]"), byConfidence = tools.querySelector("[name=confidence]");
  const search = tools.querySelector("[name=search]");
  const headers = Array.from(document.querySelectorAll(".parts thead th"), (th) => th.textContent);
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const behavior = () => (reduced.matches ? "auto" : "smooth");
  const here = () => location.href.split("#")[0];
  tools.hidden = false;
  detail.hidden = false;

  // Each part, by its slug: its row, the words a search looks in, and
  // its rings, {view, pin, x, y}, x and y in percent, in view order.
  const parts = new Map();
  for (const row of document.querySelectorAll(".parts tr[data-part]")) {
    const cell = row.cells[0], ref = cell.firstChild.textContent;
    const words = [ref].concat(Array.from(row.querySelectorAll(".marking, .type, .role"), (td) => td.textContent));
    const button = document.createElement("button");
    button.type = "button";
    button.className = "refbtn";
    button.textContent = ref;
    cell.firstChild.replaceWith(button);
    parts.set(row.dataset.part, { slug: row.dataset.part, row, ref, text: words.join(" ").toLowerCase(), pins: [] });
  }
  const bySlug = new Map(Array.from(parts.keys(), (slug) => [slug.toLowerCase(), slug]));

  // Each photo's state: the window's size, the stage's size at zoom 1,
  // the zoom and the stage's offset in the window, in pixels.
  const views = new Map(), byPhoto = new Map();
  for (const figure of atlas.querySelectorAll(".views figure")) {
    const photo = figure.querySelector(".photo"), stage = document.createElement("div");
    stage.className = "stage";
    stage.style.backgroundImage = photo.style.backgroundImage;
    photo.style.backgroundImage = "";
    const view = { id: figure.dataset.view, figure, photo, stage, w: +photo.dataset.width, h: +photo.dataset.height,
      vw: 0, vh: 0, sw: 0, sh: 0, z: 1, tx: 0, ty: 0, max: MIN_MAX_ZOOM };
    for (const pin of photo.querySelectorAll(".pin")) {
      const part = parts.get(pin.dataset.part);
      if (part) part.pins.push({ view, pin, x: parseFloat(pin.style.left), y: parseFloat(pin.style.top) });
      stage.append(pin);
    }
    photo.append(stage);
    photo.tabIndex = 0;
    photo.setAttribute("role", "group");
    photo.setAttribute("aria-label", photo.getAttribute("aria-label") + ". " + tools.querySelector(".keys").textContent);
    views.set(view.id, view);
    byPhoto.set(photo, view);
    wire(view);
  }
  const tabs = Array.from(tools.querySelectorAll(".tabs button"));
  let active = views.values().next().value, current = null;

  function clamp(view) {
    const axis = (t, size, window) => (size <= window ? (window - size) / 2 : Math.min(0, Math.max(window - size, t)));
    view.tx = axis(view.tx, view.sw * view.z, view.vw);
    view.ty = axis(view.ty, view.sh * view.z, view.vh);
  }
  function apply(view, glide) {
    clamp(view);
    view.stage.classList.toggle("glide", !!glide && !reduced.matches);
    view.stage.style.transform = `translate(${view.tx}px, ${view.ty}px) scale(${view.z})`;
    view.stage.style.setProperty("--z", view.z);
    view.photo.classList.toggle("zoomed", view.z > 1);
  }
  // Fits the stage to the window, keeping the point at its center.
  function layout(view) {
    const vw = view.photo.clientWidth, vh = view.photo.clientHeight;
    if (!vw || !vh) return;
    const cx = view.sw ? (view.vw / 2 - view.tx) / (view.sw * view.z) : 0.5;
    const cy = view.sh ? (view.vh / 2 - view.ty) / (view.sh * view.z) : 0.5;
    const fit = Math.min(vw / view.w, vh / view.h);
    Object.assign(view, { vw, vh, sw: view.w * fit, sh: view.h * fit, max: Math.max(MIN_MAX_ZOOM, OVERZOOM / fit) });
    view.z = Math.min(view.z, view.max);
    view.stage.style.width = view.sw + "px";
    view.stage.style.height = view.sh + "px";
    view.tx = vw / 2 - cx * view.sw * view.z;
    view.ty = vh / 2 - cy * view.sh * view.z;
    apply(view);
  }
  // Zooms to z about (cx, cy), a point of the window.
  function zoomAt(view, z, cx, cy, glide) {
    const next = Math.min(view.max, Math.max(1, z));
    const lx = (cx - view.tx) / view.z, ly = (cy - view.ty) / view.z;
    Object.assign(view, { z: next, tx: cx - lx * next, ty: cy - ly * next });
    apply(view, glide);
  }
  function zoomBy(view, factor) {
    zoomAt(view, view.z * factor, view.vw / 2, view.vh / 2, true);
  }
  function centerOn(view, ring, zoom) {
    view.z = Math.max(view.z, Math.min(zoom, view.max));
    view.tx = view.vw / 2 - (view.sw * ring.x / 100) * view.z;
    view.ty = view.vh / 2 - (view.sh * ring.y / 100) * view.z;
    apply(view, true);
  }
  function show(id) {
    active = views.get(id);
    for (const view of views.values()) view.figure.hidden = view !== active;
    for (const tab of tabs) {
      const on = tab.dataset.view === id;
      tab.setAttribute("aria-selected", on);
      tab.tabIndex = on ? 0 : -1;
    }
    layout(active);
  }

  // Wheel and pinch zoom, drag pan, and the keys, on one photo.
  function wire(view) {
    const photo = view.photo, points = new Map();
    let start = null, moved = false;
    const at = (e) => {
      const box = photo.getBoundingClientRect();
      return { x: e.clientX - box.left, y: e.clientY - box.top };
    };
    const begin = () => {
      const [a, b] = Array.from(points.values());
      start = { a, b, z: view.z, tx: view.tx, ty: view.ty,
        d: b ? Math.hypot(b.x - a.x, b.y - a.y) : 0, m: b ? { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 } : a };
    };
    photo.addEventListener("wheel", (e) => {
      const delta = e.deltaY * (e.deltaMode === 1 ? LINE_PIXELS : 1);
      if (delta > 0 && view.z <= 1) return;
      e.preventDefault();
      const p = at(e);
      zoomAt(view, view.z * Math.exp(-delta * WHEEL_RATE), p.x, p.y);
    }, { passive: false });
    photo.addEventListener("pointerdown", (e) => {
      if (e.pointerType === "mouse" && e.button !== 0) return;
      if (points.size === 0) moved = false;
      points.set(e.pointerId, at(e));
      begin();
    });
    photo.addEventListener("pointermove", (e) => {
      if (!points.has(e.pointerId)) return;
      points.set(e.pointerId, at(e));
      const [a, b] = Array.from(points.values());
      if (b && start.b) {
        const m = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
        const z = Math.min(view.max, Math.max(1, start.z * Math.hypot(b.x - a.x, b.y - a.y) / start.d));
        const lx = (start.m.x - start.tx) / start.z, ly = (start.m.y - start.ty) / start.z;
        Object.assign(view, { z, tx: m.x - lx * z, ty: m.y - ly * z });
        moved = true;
      } else {
        const dx = a.x - start.a.x, dy = a.y - start.a.y;
        if (!moved && Math.hypot(dx, dy) < DRAG_SLOP) return;
        if (!moved) photo.setPointerCapture(e.pointerId);
        moved = true;
        photo.classList.add("dragging");
        Object.assign(view, { tx: start.tx + dx, ty: start.ty + dy });
      }
      apply(view);
    });
    const end = (e) => {
      if (!points.delete(e.pointerId)) return;
      photo.classList.remove("dragging");
      if (points.size) begin();
    };
    photo.addEventListener("pointerup", end);
    photo.addEventListener("pointercancel", end);
    // A drag that ends on a ring is not a click on it.
    photo.addEventListener("click", (e) => {
      if (!moved) return;
      moved = false;
      e.preventDefault();
      e.stopPropagation();
    }, true);
    photo.addEventListener("keydown", (e) => {
      const pan = { ArrowLeft: [1, 0], ArrowRight: [-1, 0], ArrowUp: [0, 1], ArrowDown: [0, -1] }[e.key];
      if (pan && view.z > 1) {
        view.tx += pan[0] * view.vw * PAN_STEP;
        view.ty += pan[1] * view.vh * PAN_STEP;
        apply(view, true);
      } else if (e.key === "+" || e.key === "=") zoomBy(view, ZOOM_STEP);
      else if (e.key === "-" || e.key === "_") zoomBy(view, 1 / ZOOM_STEP);
      else if (e.key === "0") zoomAt(view, 1, 0, 0, true);
      else return;
      e.preventDefault();
    });
    // A ring reached by Tab is brought into the window.
    photo.addEventListener("focusin", (e) => {
      const part = e.target.closest(".pin") && parts.get(e.target.dataset.part);
      const ring = part && part.pins.find((p) => p.pin === e.target);
      if (!ring) return;
      const x = view.tx + view.sw * ring.x / 100 * view.z, y = view.ty + view.sh * ring.y / 100 * view.z;
      if (x < 0 || y < 0 || x > view.vw || y > view.vh) centerOn(view, ring, 1);
    });
  }

  function matches(part) {
    if (byGroup.value && part.row.dataset.group !== byGroup.value) return false;
    if (byConfidence.value) {
      const [kind, id] = byConfidence.value.split(":");
      if (part.row.dataset[kind + "Confidence"] !== id) return false;
    }
    return search.value.toLowerCase().split(/\\s+/).every((word) => part.text.includes(word));
  }
  function filter() {
    let any = false;
    for (const part of parts.values()) {
      const on = matches(part);
      any = any || on;
      part.row.hidden = !on;
      for (const ring of part.pins) ring.pin.classList.toggle("out", !on);
    }
    for (const group of document.querySelectorAll(".parts tbody")) group.hidden = !group.querySelector("tr[data-part]:not([hidden])");
    none.hidden = any;
  }
  function unfilter() {
    byGroup.value = byConfidence.value = search.value = "";
    filter();
  }

  function setHash(slug) {
    try {
      history.replaceState(history.state, "", here() + (slug ? "#" + slug : ""));
    } catch (e) {
      // A page whose address cannot be rewritten keeps its old hash.
    }
  }
  function flash(pin) {
    pin.classList.remove("flash");
    void pin.offsetWidth;
    pin.classList.add("flash");
  }
  function mark(on) {
    if (!current) return;
    current.row.classList.toggle("sel", on);
    for (const ring of current.pins) ring.pin.classList.toggle("sel", on);
  }
  function button(text, onclick) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = text;
    b.addEventListener("click", onclick);
    return b;
  }
  // The card: the part's row, cell by cell under the table's headers,
  // and a button for each photo it is ringed on.
  function card(part, shown) {
    const title = document.createElement("h3"), list = document.createElement("dl");
    title.textContent = part.ref;
    const on = document.createElement("p");
    on.className = "on";
    if (part.pins.length) {
      on.append("On ");
      for (const ring of part.pins) {
        const tab = tabs.find((t) => t.dataset.view === ring.view.id);
        const b = button(tab.textContent, () => reveal(part, ring));
        b.setAttribute("aria-pressed", ring === shown);
        on.append(b);
      }
    } else on.textContent = part.row.cells[0].querySelector("small").textContent;
    Array.from(part.row.cells).slice(1).forEach((cell, i) => {
      if (!cell.textContent.trim()) return;
      const dt = document.createElement("dt"), dd = document.createElement("dd");
      dt.textContent = headers[i + 1];
      dd.append(...Array.from(cell.childNodes, (n) => n.cloneNode(true)));
      list.append(dt, dd);
    });
    const actions = document.createElement("p");
    actions.className = "actions";
    actions.append(
      button("Show in the table", () => {
        part.row.scrollIntoView({ block: "center", behavior: behavior() });
        part.row.querySelector(".refbtn").focus({ preventScroll: true });
      }),
      button("Close", clear));
    body.replaceChildren(title, on, list, actions);
  }
  function reveal(part, ring) {
    if (!ring) {
      detail.scrollIntoView({ block: "nearest", behavior: behavior() });
      return;
    }
    show(ring.view.id);
    centerOn(ring.view, ring, FOCUS_ZOOM);
    flash(ring.pin);
    card(part, ring);
    ring.view.photo.scrollIntoView({ block: "nearest", behavior: behavior() });
  }
  // Selects a part: from a ring, which stays where it is, or else from
  // the table, a link or the address, which shows it on the first photo
  // it is ringed on.
  function select(slug, pin) {
    const part = parts.get(slug);
    if (!part) return;
    if (!matches(part)) unfilter();
    mark(false);
    current = part;
    mark(true);
    setHash(slug);
    const ring = pin ? part.pins.find((p) => p.pin === pin) : part.pins[0];
    card(part, ring);
    if (pin) {
      flash(pin);
      detail.scrollIntoView({ block: "nearest", behavior: behavior() });
    } else reveal(part, ring);
  }
  function clear() {
    const focused = detail.contains(document.activeElement);
    mark(false);
    if (focused && current && current.pins.length) current.pins[0].pin.focus({ preventScroll: true });
    current = null;
    body.innerHTML = hint;
    setHash("");
  }
  function fromHash() {
    let name = "";
    try {
      name = decodeURIComponent(location.hash.slice(1));
    } catch (e) {
      return;
    }
    if (!SLUG.test(name)) return;
    const key = name.toLowerCase();
    const slug = bySlug.get(key) || (key.startsWith(ROW_PREFIX) && bySlug.get(key.slice(ROW_PREFIX.length)));
    if (slug && (!current || current.slug !== slug)) select(slug);
  }

  document.addEventListener("click", (e) => {
    const link = e.target.closest("a");
    if (link) {
      if (link.classList.contains("pin")) {
        e.preventDefault();
        select(link.dataset.part, link);
      } else if (link.hash.startsWith("#" + ROW_PREFIX) && link.href.split("#")[0] === here()) {
        const slug = link.hash.slice(1 + ROW_PREFIX.length);
        if (parts.has(slug)) {
          e.preventDefault();
          select(slug);
        }
      }
      return;
    }
    const row = e.target.closest(".parts tr[data-part]");
    if (!row || (!e.target.closest(".refbtn") && String(getSelection()))) return;
    select(row.dataset.part);
  });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && current && !e.target.closest("input, select")) clear();
  });
  tools.querySelector(".tabs").addEventListener("keydown", (e) => {
    const step = { ArrowLeft: -1, ArrowRight: 1 }[e.key];
    if (!step) return;
    const next = tabs[(tabs.indexOf(document.activeElement) + step + tabs.length) % tabs.length];
    show(next.dataset.view);
    next.focus();
  });
  for (const tab of tabs) tab.addEventListener("click", () => show(tab.dataset.view));
  for (const b of tools.querySelectorAll("[data-zoom]")) {
    b.addEventListener("click", () => {
      const how = b.dataset.zoom;
      if (how === "fit") zoomAt(active, 1, 0, 0, true);
      else zoomBy(active, how === "in" ? ZOOM_STEP : 1 / ZOOM_STEP);
    });
  }
  for (const control of [byGroup, byConfidence]) control.addEventListener("change", filter);
  search.addEventListener("input", filter);
  addEventListener("hashchange", fromHash);
  const resized = new ResizeObserver((entries) => {
    for (const entry of entries) layout(byPhoto.get(entry.target));
  });
  for (const view of views.values()) resized.observe(view.photo);
  show(active.id);
  filter();
  fromHash();
})();
"""


def die(message):
    sys.exit(f"board_atlas: {message}")


def headings(path):
    """The heading texts of a Markdown file, and its numbered items."""
    found = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"#+ (.*)", line)
        if match:
            text = match.group(1).strip()
            found.add(text)
            item = re.match(r"(\d+)\. ", text)
            if item:
                found.add(f"item {item.group(1)}")
    return found


class Renderer:
    """Renders the data's prose marks, checking each citation and part
    reference as it goes."""

    def __init__(self, refs):
        self.refs = refs
        self.headings = {}

    def code(self, text):
        return CODE.sub(lambda m: f"<code>{m.group(1)}</code>", html.escape(text, quote=False))

    def cite(self, path, section):
        target = ROOT / path
        if not target.is_file():
            die(f"cites {path}, which does not exist")
        shown = re.sub(r"^(docs|third_party)/", "", path)
        link = f'<a href="{BLOB}{html.escape(path)}"><code>{html.escape(shown)}</code></a>'
        if not section:
            return link
        if path not in self.headings:
            self.headings[path] = headings(target)
        if section not in self.headings[path]:
            die(f"cites {path}, {section!r}, which is not one of its headings")
        if ITEM.match(section):
            return f"{link}, {self.code(section)}"
        return f"{link}, “{self.code(section)}”"

    def ref(self, ref):
        if ref not in self.refs:
            die(f"refers to {ref}, which is not a part")
        return f'<a href="#{anchor(ref)}">{html.escape(ref)}</a>'

    def __call__(self, text):
        out = []
        last = 0
        for match in CITE.finditer(text):
            out.append(self.plain(text[last:match.start()]))
            out.append(self.cite(match.group(1), match.group(2)))
            last = match.end()
        out.append(self.plain(text[last:]))
        return "".join(out)

    def plain(self, text):
        out = []
        last = 0
        for match in REF.finditer(text):
            out.append(self.code(text[last:match.start()]))
            out.append(self.ref(match.group(1)))
            last = match.end()
        out.append(self.code(text[last:]))
        return "".join(out)


def slug(ref):
    """A part's name in the address, #<slug>: letters, digits and hyphens."""
    return re.sub(r"[^A-Za-z0-9]+", "-", ref).strip("-")


def anchor(ref):
    return "part-" + slug(ref)


def mark(kind, text):
    return f'<span class="mark c-{kind}">{html.escape(text)}</span>'


def check(data):
    """Refuses data the page would render wrongly."""
    confidence = {c["id"] for c in data["confidence"]}
    comparison = {c["id"] for c in data["comparison"]}
    groups = {g["id"] for g in data["groups"]}
    views = {v["id"] for v in data["views"]}
    seen = set()
    for view in data["views"]:
        if not (ROOT / "docs" / view["image"]).is_file():
            die(f"view {view['id']}: no {view['image']}")
    for part in data["parts"]:
        ref = part["ref"]
        if ref in seen:
            die(f"{ref} twice")
        seen.add(ref)
        if part["group"] not in groups:
            die(f"{ref}: group {part['group']}")
        for key in ("id_confidence", "role_confidence"):
            if part[key] not in confidence:
                die(f"{ref}: {key} {part[key]}")
        for model in COMPARED:
            if part[model]["status"] not in comparison:
                die(f"{ref}: {model} {part[model]['status']}")
        for view, point in part["at"].items():
            if view not in views:
                die(f"{ref}: view {view}")
            if not (0 <= point["x"] <= 100 and 0 <= point["y"] <= 100):
                die(f"{ref}: {view} at {point}")
    return seen


def legend(entries, label):
    items = "".join(
        f'<li><span class="dot c-{e["id"]}"></span><span><b>{html.escape(e["id"])}</b>: {html.escape(e["text"])}</span></li>'
        for e in entries)
    return f'<ul class="legend" aria-label="{html.escape(label)}">{items}</ul>'


def render_view(view, parts, groups):
    pins = []
    for part in parts:
        point = part["at"].get(view["id"])
        if point is None:
            continue
        title = f"{part['ref']}: {part['type']}; {groups[part['group']]}"
        title = CITE.sub(lambda m: m.group(1).rsplit("/", 1)[-1], title).replace("`", "")
        pins.append(
            f'<a class="pin c-{part["id_confidence"]}" style="left:{point["x"]}%;top:{point["y"]}%" '
            f'href="#{anchor(part["ref"])}" data-part="{slug(part["ref"])}" title="{html.escape(title)}">'
            f'<span class="ring"></span><span class="lbl">{html.escape(part["ref"])}</span></a>')
    image = html.escape(view["image"])
    wide = ' class="wide"' if view["width"] > view["height"] else ""
    style = (f"aspect-ratio:{view['width']}/{view['height']};"
             f"background-image:url('{image}'),url('{RAW}{image}')")
    return (f'<figure id="view-{view["id"]}" data-view="{view["id"]}"{wide}>\n'
            f'  <h2>{html.escape(view["title"])}</h2>\n'
            f'  <div class="photo" role="img" aria-label="{html.escape(view["alt"])}" style="{style}" '
            f'data-width="{view["width"]}" data-height="{view["height"]}">\n    '
            + "\n    ".join(pins)
            + f'\n  </div>\n  <figcaption>{html.escape(view["caption"])}  '
            f'<a href="{BLOB}docs/{image}">Full size</a>.</figcaption>\n</figure>')


def render_tools(data):
    """The script's toolbar, hidden until the script shows it."""
    def options(entries, prefix=""):
        return "".join(f'<option value="{prefix}{html.escape(e["id"])}">{html.escape(e.get("title", e["id"]))}</option>'
                       for e in entries)
    tabs = "".join(f'<button type="button" role="tab" data-view="{v["id"]}" aria-controls="view-{v["id"]}">'
                   f'{html.escape(v["title"])}</button>' for v in data["views"])
    confidence = (f'<optgroup label="Designator and marking">{options(data["confidence"], "id:")}</optgroup>'
                  f'<optgroup label="Role">{options(data["confidence"], "role:")}</optgroup>')
    return ('<div class="tools" hidden>\n'
            f'  <div class="tabs" role="tablist" aria-label="Photos">{tabs}</div>\n'
            '  <div class="zoom" role="group" aria-label="Zoom">'
            '<button type="button" data-zoom="in" aria-label="Zoom in">+</button>'
            '<button type="button" data-zoom="out" aria-label="Zoom out">&minus;</button>'
            '<button type="button" data-zoom="fit">Fit</button></div>\n'
            f'  <label>Subsystem <select name="group"><option value="">All</option>{options(data["groups"])}</select></label>\n'
            f'  <label>Confidence <select name="confidence"><option value="">Any</option>{confidence}</select></label>\n'
            '  <label class="search">Search <input type="search" name="search" placeholder="ref, marking, type or role"></label>\n'
            '  <p class="keys">Wheel or pinch to zoom and drag to pan; on a focused photo, + and - zoom, 0 fits and the arrows pan.</p>\n'
            '  <p class="none" hidden>No part matches.</p>\n'
            '</div>')


def render(data):
    refs = check(data)
    r = Renderer(refs)
    groups = {g["id"]: g["title"] for g in data["groups"]}
    views = {v["id"]: v["short"] for v in data["views"]}
    parts = data["parts"]
    out = []
    emit = out.append

    emit("<!doctype html>")
    emit('<html lang="en">')
    emit("<head>")
    emit('<meta charset="utf-8">')
    emit('<meta name="viewport" content="width=device-width, initial-scale=1">')
    emit(f"<title>{html.escape(data['title'])}</title>")
    emit("<!-- Generated by tools/board_atlas.py from docs/board-atlas.json; do not edit.\n"
         "     make board-atlas rewrites it; make check-board-atlas fails when they disagree. -->")
    emit('<link rel="preconnect" href="https://fonts.googleapis.com">')
    emit('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">')
    emit("<style>")
    emit(CSS.rstrip("\n"))
    emit("</style>")
    emit("</head>")
    emit("<body>")
    emit('<div class="wrap">')

    emit("<header>")
    emit(f"  <h1>{html.escape(data['heading'])}</h1>")
    for paragraph in data["intro"]:
        emit(f"  <p>{r(paragraph)}</p>")
    emit('  <div class="tags">' + "".join(f'<span class="tag">{html.escape(t)}</span>' for t in data["tags"]) + "</div>")
    emit("</header>")

    emit('<section class="card">')
    emit("  <h2>The marks</h2>")
    emit("  <p>A ring's color is how its part is identified, its designator and marking; the table gives that and the role's apart.</p>")
    emit("  " + legend(data["confidence"], "Confidence"))
    emit("</section>")

    emit('<div class="atlas">')
    emit(render_tools(data))
    emit('<section class="views">')
    for view in data["views"]:
        emit(render_view(view, parts, groups))
    emit("</section>")
    emit('<aside class="card detail" aria-live="polite" hidden>')
    emit("  <h2>Part</h2>")
    emit('  <div class="detail-body"><p>Select a ring on the photo, or a row of the parts table, for its part.</p></div>')
    emit("</aside>")
    emit("</div>")

    emit('<section class="card">')
    emit("  <h2>Measured nets</h2>")
    emit(f"  <p>{r(data['nets_intro'])}</p>")
    emit('  <div class="scroll"><table>')
    emit("    <thead><tr><th>Net</th><th>Path</th><th>Note</th></tr></thead>")
    emit("    <tbody>")
    for net in data["nets"]:
        emit(f"      <tr><td>{r(net['net'])}</td><td>{r(net['path'])}</td><td>{r(net['note'])}</td></tr>")
    emit("    </tbody>")
    emit("  </table></div>")
    for paragraph in data["design_note"]:
        emit(f"  <p>{r(paragraph)}</p>")
    emit("</section>")

    emit('<section class="card">')
    emit("  <h2>Parts</h2>")
    emit("  <p>Confidence is the designator and marking's, then the role's.  The last two columns compare the Z3801A's and the 58503A's boards at the same designator:</p>")
    emit("  " + legend(data["comparison"], "Comparison"))
    emit('  <div class="scroll"><table class="parts">')
    emit("    <thead><tr><th>Ref</th><th>Marking</th><th>Type</th><th>Role</th><th>Firmware</th>"
         "<th>Confidence</th>" + "".join(f"<th>{COMPARED_TITLES[m]}</th>" for m in COMPARED) + "</tr></thead>")
    columns = 6 + len(COMPARED)
    for group in data["groups"]:
        members = [p for p in parts if p["group"] == group["id"]]
        emit("    <tbody>")
        emit(f'      <tr><th class="group" colspan="{columns}" scope="rowgroup">{html.escape(group["title"])}</th></tr>')
        for part in sorted(members, key=lambda p: natural(p["ref"])):
            on = ", ".join(views[v] for v in views if v in part["at"]) or "not ringed"
            role = r(part["role"])
            if part["note"]:
                role += f' <span class="note">{r(part["note"])}</span>'
            compared = "".join(
                f"<td>{mark(part[m]['status'], part[m]['status'])}"
                + (f"<br><small>{r(part[m]['note'])}</small>" if part[m]["note"] else "") + "</td>"
                for m in COMPARED)
            emit(f'      <tr id="{anchor(part["ref"])}" data-part="{slug(part["ref"])}" data-group="{part["group"]}" '
                 f'data-id-confidence="{part["id_confidence"]}" data-role-confidence="{part["role_confidence"]}">'
                 f'<td class="ref">{html.escape(part["ref"])}<small>{html.escape(on)}</small></td>'
                 f'<td class="marking">{r(part["marking"])}</td><td class="type">{r(part["type"])}</td>'
                 f'<td class="role">{role}</td><td class="fw">{r(part["firmware"])}</td>'
                 f'<td>{mark(part["id_confidence"], "id " + part["id_confidence"])}<br>'
                 f'{mark(part["role_confidence"], "role " + part["role_confidence"])}</td>{compared}</tr>')
        emit("    </tbody>")
    emit("  </table></div>")
    emit("</section>")

    emit('<div class="notes">')
    for title, key in (("Across the three boards", "observations"), ("What the photos cannot show", "limits")):
        emit('  <section class="card">')
        emit(f"    <h2>{title}</h2>")
        for paragraph in data[key]:
            emit(f"    <p>{r(paragraph)}</p>")
        emit("  </section>")
    emit("</div>")

    emit('<section class="card">')
    emit("  <h2>Open</h2>")
    emit("  <ul>")
    for item in data["open"]:
        emit(f"    <li>{r(item)}</li>")
    emit("  </ul>")
    emit("</section>")

    emit(f'<footer>Generated by <a href="{BLOB}tools/board_atlas.py"><code>tools/board_atlas.py</code></a> from '
         f'<a href="{BLOB}docs/board-atlas.json"><code>docs/board-atlas.json</code></a>.</footer>')
    emit("</div>")
    emit("<script>")
    emit(SCRIPT.rstrip("\n"))
    emit("</script>")
    emit("</body>")
    emit("</html>")
    return "\n".join(out) + "\n"


def natural(ref):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", ref)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--write", action="store_true", help="rewrite docs/board-atlas.html")
    args = parser.parse_args()
    page = render(json.loads(DATA.read_text(encoding="utf-8")))
    if args.write:
        PAGE.write_text(page, encoding="utf-8")
        return
    current = PAGE.read_text(encoding="utf-8") if PAGE.exists() else ""
    if current != page:
        sys.stdout.writelines(difflib.unified_diff(
            current.splitlines(keepends=True), page.splitlines(keepends=True),
            str(PAGE.relative_to(ROOT)), "rendered from " + str(DATA.relative_to(ROOT))))
        die("docs/board-atlas.html differs from docs/board-atlas.json; make board-atlas rewrites it")


if __name__ == "__main__":
    main()
