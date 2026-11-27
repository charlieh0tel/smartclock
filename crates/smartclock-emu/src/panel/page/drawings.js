// smartclock-emu's panel page's drawings (docs/emulator.md, "The panel
// page"): each receiver's face as data, `DRAWINGS`, and what draws it.
// The drawings are our own, drawn from photographs of the bench units,
// not HP's artwork.
"use strict";

const SVG = "http://www.w3.org/2000/svg";

// The display: twelve characters (58503a-3704.md, "The front panel").
const CELLS = 12;
// A character's box in font units, its stroke, and its slant, the
// degrees its verticals lean right, as measured on a photograph of the
// bench unit's display.
const GLYPH_WIDTH = 10;
const GLYPH_HEIGHT = 18;
const STROKE = 1.4;
const SLANT = 6;
// The gap between one character's box and the next's, font units.
const CELL_GAP = 4.6;

// Each segment of a character as a line in font units, (0, 0) its top
// left: a b c d e f the outline clockwise from the top, g and G the
// middle bar's left and right halves, h i j the upper diagonal, center
// and diagonal from the left, k l m the lower ones.  The diagonals stop
// a stroke short of the center bars, as on the glass, so a dim one never
// covers a lit one.  Which light for each character is font.json's.
const SEGMENTS = {
  a: [1.1, 0, 8.9, 0],
  b: [10, 1.1, 10, 7.9],
  c: [10, 10.1, 10, 16.9],
  d: [1.1, 18, 8.9, 18],
  e: [0, 10.1, 0, 16.9],
  f: [0, 1.1, 0, 7.9],
  g: [1.1, 9, 4.2, 9],
  G: [5.8, 9, 8.9, 9],
  h: [1.8, 2, 3.5, 6.9],
  i: [5, 1.4, 5, 7.6],
  j: [8.2, 2, 6.5, 6.9],
  k: [3.5, 11.1, 1.8, 16],
  l: [5, 10.4, 5, 16.6],
  m: [6.5, 11.1, 8.2, 16],
};
// The marks a character carries to its right, in font units: a point
// a little above the baseline, a comma's tail below it, and the colon's
// upper dot, whose lower dot is the point (the photograph).
const POINT = [12, 16.5];
const COMMA_TAIL = [12, 16.5, 11, 19.5];
const COLON_DOT = [12, 7.5];
const DOT_RADIUS = STROKE * 0.85;

// The indicators' colors lit.
const GREEN = "var(--led-green)";
const RED = "var(--led-red)";
const AMBER = "var(--led-amber)";

// Each panel's indicators: the label, the indicator's name in the
// state's lamp bank ("power" is the page's own: lit while the run
// answers), the color lit, for an LED, and its slot in its row.
const Z38XX_LEDS = [
  { label: "Power", name: "power", color: GREEN, slot: 0 },
  { label: "Enabled", name: "enabled", color: GREEN, slot: 1 },
  { label: "Active", name: "active", color: GREEN, slot: 2 },
  { label: "Alarm", name: "alarm", color: RED, slot: 3 },
  { label: "GPS  Lock", name: "gps_lock", color: GREEN, slot: 5 },
  { label: "Holdover", name: "holdover", color: AMBER, slot: 6 },
];
const HP58503_LAMPS = [
  { label: "Power", name: "power", slot: 0 },
  { label: "GPS Lock", name: "gps_lock", slot: 1 },
  { label: "Holdover", name: "holdover", slot: 2 },
  { label: "Alarm", name: "alarm", slot: 3 },
];

// Every drawing is this wide, in SVG units; its height is its face's
// proportion of that.
const DRAWING_WIDTH = 1000;
// How far a bezel or a plate is inset from the drawing's edge, half its
// stroke, SVG units.
const BEZEL_INSET = 1.5;
// A face's size, millimeters.  The 58503A's, with or without Option
// 001: the 58503B and 59551A, the same half-rack module, 097-58503-12
// and 097-58503-13, Specifications, "Dimensions": "88.5 mm H x 212.6 mm
// W x 348.3 mm D.  Half-Rack Module"; 097-59551-02 gives none of the
// 58503A's own.  That is 2U (2 x 44.45 mm = 88.9 mm, less clearance)
// by HP's half-rack width.
const HALF_RACK_2U = { width: 212.6, height: 88.5 };
// The Z3805A has no manual here.  A photograph of the bench unit's face,
// corrected for the camera's tilt, measures 2.5:1, which is the 58503A's
// 2U half-rack face within the measurement's error (3U at that width
// would be 1.6:1); it is the 58503A's chassis without the display.
const Z3805A_FACE = HALF_RACK_2U;
// The Z3801A: 097-z3801-01, Specifications, "General Information": "127
// mm Height x 301.5 mm Width ... (Half-Rack Module)".  127 mm is 3U (3 x
// 44.45 mm = 133.35 mm, less clearance), but 301.5 mm is wider than HP's
// 212.6 mm half-rack module, and the manual's own drawing of the face
// (page 1-2) is 2.7:1, not 2.37.  Odd, and taken as stated.
const Z3801A_FACE = { width: 301.5, height: 127 };
// How far the Z3801A's handle stands out below its plate, SVG units.
const HANDLE_REACH = 47;

// A key's cap, SVG units, its top and the side below it the camera
// sees, and where a key sits right of its column's lamp, below its
// row's top, and one row below the last, as measured on the owner's
// unit, photographed 2026-11-06 (OPTION_001_COLUMNS).
const KEY_WIDTH = 62;
const KEY_HEIGHT = 42;
const KEY_OFFSET = 19;
const KEY_ROW_PITCH = 64.5;
// The key lamp left of a key, on the key's column, so under the STATUS
// lamp above it, and below the key's top.
const KEY_LAMP = { dx: -KEY_OFFSET, dy: 16 };
// A key's legend: its left from the key's, its first baseline below the
// key's top, and its second line below its first.
const KEY_LEGEND = { dx: 8, baseline: 23, leading: 15 };
// A lamp's and an LED's radius, SVG units: the 58503A's lamps, STATUS's
// and the keys' alike, are 1.9 mm across on the photograph.
const LAMP_RADIUS = 4.5;
const LED_RADIUS = 9;
// The key caps' colors, and their legends'.
const KEY_DARK = ["var(--key-dark)", "var(--key-text-light)"];
const KEY_MID = ["var(--key-mid)", "var(--key-text)"];
const KEY_LIGHT = ["var(--key-light)", "var(--key-text)"];
const KEY_BLUE = ["var(--key-blue)", "var(--key-text-light)"];

// The badge left of the wordmark, where the real panel has HP's logo:
// plain letters in a light box, not the logo.
const BADGE = { width: 50, height: 32, gap: 8 };
// The wordmark's lines' width, and its weight, bold on every panel.
const WORDMARK_WIDTH = 95;
const WORDMARK_WEIGHT = 700;
// Where each heading sits, its second line below its first, and how
// wide its model and title are, in wordmark widths, from its
// photograph: the 58503A's model and title bold, the Z3805A's not.  The
// 58503A's from the owner's unit, photographed 2026-11-06, with a ruler:
// its title is 86 mm long, 4.26 wordmarks, where an earlier photograph
// gave 3.87.
const HP58503A_HEADING = {
  x: 81, y: 32, leading: 16.5, titleX: 206, weight: 700, modelWidth: 1.0, titleWidth: 4.26,
};
const Z3805A_HEADING = {
  x: 78, y: 36, leading: 18, titleX: 190, weight: 400, modelWidth: 0.57, titleWidth: 3.83,
};

// The 58503A's STATUS lamps' and MODE keys' columns with Option 001:
// each STATUS lamp sits in its column with the key lamps below it, Power
// over Time's and Sat's, GPS Lock over Long's and Status's, Holdover
// over Lat's and Serial Port's, Alarm over Alt's and Shift's.  Measured
// on the owner's unit, photographed 2026-11-06 with a ruler along the
// face's top edge: 29.8, 51.0, 72.3 and 93.5 mm from the face's left
// edge, a column every 21.2 mm, where an earlier photograph gave 138,
// 236, 334 and 430, closing to the right.
const OPTION_001_COLUMNS = [140, 240, 340, 440];
// A group's bar, along its box's top, SVG units: 2.4 mm on the same
// photograph.
const GROUP_BAR = 11;
// A group's box's corners' radius, SVG units.
const GROUP_RADIUS = 6;

// Each drawing, by the name the state gives it: its face, how far it
// stands out below that, and its parts, drawn in order, each one of the
// kinds `PARTS` draws.
//
// The 58503A, on its face as HALF_RACK_2U: with Option 001, the dark
// window with the title and the display, then STATUS's lamps, then
// MODE's keys, all left of a blank area, where they sit on the owner's
// unit, photographed 2026-11-06 with a ruler along the face's top edge
// and corrected for the camera's angle to the face's own 212.6 by 88.5
// mm (the window 159 mm wide, 2.7 mm from the face's left edge, and 37.6
// mm tall; the groups' boxes 151 mm wide, 9.8 mm from it, STATUS's from
// 43.8 to 54.6 mm down, MODE's from 57.4 to 87.7; the keys' tops 13 by
// 7.8 mm), which moves the window, the groups and the keys right and
// down from where an earlier photograph had them, and the window and
// the groups wider; without, as the 58503B's face, 097-59551-02 page
// 1-4, the title and the lamps over a blank window, the heading and the
// lamps' size Option 001's.  The Z3805A: a black
// band with the title and the LEDs over a light window, where they sit
// on a photograph of the bench unit.  The Z3801A: a light plate with the
// LEDs, two thumbscrews and a handle, the screws where the manual's
// drawing has them (097-z3801-01 page 1-2), the rest where they sit on a
// photograph of a Nortel unit.  The Z3816A: no photograph or manual is
// here, so the Z3805A's band and window with its own heading and the
// power LED alone, the one light the firmware's known outputs give it
// (its latch's lamps not traced).
const DRAWINGS = {
  "58503a-option-001": {
    face: HALF_RACK_2U,
    below: 0,
    parts: [
      { kind: "bezel", class: "bezel", rx: 10 },
      { kind: "rect", class: "band", x: 13, y: 10, width: 748, height: 176, rx: 6 },
      { kind: "heading", model: "58503A", layout: HP58503A_HEADING, onLight: false },
      { kind: "rect", class: "vfd", x: 135, y: 79, width: 520, height: 80, rx: 3 },
      { kind: "display", x: 159, y: 93, scale: 2.7 },
      { kind: "group", title: "STATUS", x: 46, y: 206, width: 710, height: 51 },
      {
        kind: "row", indicators: HP58503_LAMPS, xs: OPTION_001_COLUMNS, y: 247,
        label: { class: "label", dx: 15, dy: 3, anchor: null, first: false, onLight: false },
      },
      { kind: "group", title: "MODE", x: 46, y: 270, width: 710, height: 142 },
      {
        kind: "keys", columns: OPTION_001_COLUMNS, y: 296,
        keys: [
          ["Time", KEY_DARK], ["Long", KEY_MID], ["Lat", KEY_MID], ["Alt", KEY_MID],
          ["Sat", KEY_LIGHT], ["Status", KEY_LIGHT], ["Serial Port", KEY_LIGHT], ["Shift", KEY_BLUE],
        ],
        // Clear Alarm, Shift's other function, right of the first row's
        // last key.
        aside: { lines: ["Clear", "Alarm"], gap: 8, y: 312, leading: 15 },
      },
    ],
  },
  "58503a": {
    face: HALF_RACK_2U,
    below: 0,
    parts: [
      { kind: "bezel", class: "bezel", rx: 10 },
      { kind: "heading", model: "58503A", layout: HP58503A_HEADING, onLight: true },
      {
        kind: "row", indicators: HP58503_LAMPS, xs: [79, 179, 279, 379], y: 132,
        label: { class: "label", dx: 0, dy: -21, anchor: "middle", first: true, onLight: false },
      },
      { kind: "rect", class: "window", x: 47, y: 205, width: 898, height: 156, rx: 4 },
    ],
  },
  "z3805a": {
    face: Z3805A_FACE,
    below: 0,
    parts: [
      { kind: "bezel", class: "bezel", rx: 4 },
      { kind: "rect", class: "band", x: 1.5, y: 1.5, width: 997, height: 199, rx: 4 },
      { kind: "heading", model: "Z3805A", layout: Z3805A_HEADING, onLight: false },
      {
        kind: "row", indicators: Z38XX_LEDS, xs: [60, 159, 257, 358, 460, 562, 667], y: 128,
        label: { class: "led-label", dx: 0, dy: -32, anchor: "middle", first: true, onLight: false },
      },
      { kind: "rect", class: "window", x: 27, y: 226, width: 946, height: 163, rx: 4 },
    ],
  },
  "z3816a": {
    face: Z3805A_FACE,
    below: 0,
    parts: [
      { kind: "bezel", class: "bezel", rx: 4 },
      { kind: "rect", class: "band", x: 1.5, y: 1.5, width: 997, height: 199, rx: 4 },
      { kind: "heading", model: "Z3816A", layout: Z3805A_HEADING, onLight: false },
      {
        kind: "row", indicators: Z38XX_LEDS.slice(0, 1), xs: [60], y: 128,
        label: { class: "led-label", dx: 0, dy: -32, anchor: "middle", first: true, onLight: false },
      },
      { kind: "rect", class: "window", x: 27, y: 226, width: 946, height: 163, rx: 4 },
    ],
  },
  "z3801a": {
    face: Z3801A_FACE,
    below: HANDLE_REACH,
    parts: [
      { kind: "bezel", class: "plate", rx: 6 },
      {
        kind: "row", indicators: Z38XX_LEDS, xs: [288, 367, 446, 523, 598, 673, 746], y: 140,
        label: { class: "led-label", dx: 0, dy: -24, anchor: "middle", first: true, onLight: true },
      },
      { kind: "screws", xs: [35, 965], y: 223, radius: 18, slot: 9 },
      { kind: "path", class: "handle", d: "M 302 337 C 296 376, 300 442, 330 446 L 700 446 C 728 442, 732 376, 726 337" },
    ],
  },
};
// The 58503B's faces are the 58503A's, with Option 001 or without: the
// same half-rack module (HALF_RACK_2U) and the same lamps and keys
// (097-58503-12, "58503B/Option 001 Front-Panel Display/Keypad at a
// Glance"), its own model in the heading.  Its wordmark is the 58503A's,
// as its *IDN? names HEWLETT-PACKARD; no photograph of one is here.
function renamed(drawing, model) {
  return {
    ...drawing,
    parts: drawing.parts.map((part) => (part.kind === "heading" ? { ...part, model } : part)),
  };
}
Object.assign(DRAWINGS, {
  "58503b-option-001": { ...renamed(DRAWINGS["58503a-option-001"], "58503B") },
  "58503b": { ...renamed(DRAWINGS["58503a"], "58503B") },
});
// The drawing shown for a name the page has none of.
const FALLBACK_DRAWING = "z3805a";

function faceHeight(face) {
  return DRAWING_WIDTH * face.height / face.width;
}

function el(name, attributes, parent) {
  const node = document.createElementNS(SVG, name);
  for (const [key, value] of Object.entries(attributes || {})) {
    node.setAttribute(key, value);
  }
  if (parent) parent.appendChild(node);
  return node;
}

function text(content, attributes, parent) {
  const node = el("text", attributes, parent);
  node.textContent = content;
  return node;
}

function filters(svg) {
  const defs = el("defs", {}, svg);
  for (const [id, deviation] of [["glow", 0.9], ["lamp-glow", 3], ["led-glow", 4]]) {
    const filter = el("filter", { id, x: "-100%", y: "-100%", width: "300%", height: "300%" }, defs);
    el("feGaussianBlur", { stdDeviation: deviation, result: "blur" }, filter);
    const merge = el("feMerge", {}, filter);
    el("feMergeNode", { in: "blur" }, merge);
    el("feMergeNode", { in: "SourceGraphic" }, merge);
  }
  const dome = el("radialGradient", { id: "dome", cx: "35%", cy: "30%", r: "70%" }, defs);
  el("stop", { offset: "0%", "stop-color": "#ffffff", "stop-opacity": "0.75" }, dome);
  el("stop", { offset: "45%", "stop-color": "#ffffff", "stop-opacity": "0.08" }, dome);
  el("stop", { offset: "100%", "stop-color": "#000000", "stop-opacity": "0.25" }, dome);
}

// The wordmark at (x, y), the badge left of it, and the model and title
// from titleX on the wordmark's two baselines, all four lines the same
// height.  Each line is set to its width on the photograph, as a
// multiple of the wordmark's, since no font here has the panel's.
function heading(svg, model, { x, y, leading, titleX, weight, modelWidth, titleWidth }) {
  const badgeX = x - BADGE.gap - BADGE.width;
  const badgeY = y - 15;
  el("rect", { class: "badge", x: badgeX, y: badgeY, width: BADGE.width, height: BADGE.height, rx: 5 }, svg);
  text("HP", {
    class: "badge-text", x: badgeX + BADGE.width / 2, y: badgeY + BADGE.height / 2 + 7, "text-anchor": "middle",
  }, svg);
  const line = (content, lineX, lineY, widths, lineWeight) => text(content, {
    class: "heading-line", x: lineX, y: lineY, "font-weight": lineWeight,
    textLength: widths * WORDMARK_WIDTH, lengthAdjust: "spacingAndGlyphs",
  }, svg);
  line("HEWLETT", x, y, 1, WORDMARK_WEIGHT);
  line("PACKARD", x, y + leading, 1, WORDMARK_WEIGHT);
  line(model, titleX, y, modelWidth, weight);
  line("GPS TIME AND FREQUENCY REFERENCE RECEIVER", titleX, y + leading, titleWidth, weight);
}

// One character cell at (x, y), scaled by `scale` from font units.  It
// glows as a whole: a filter on a segment alone, a line with a bounding
// box of no height or width, would draw nothing.
function cell(svg, x, y, scale) {
  const glowing = el("g", { filter: "url(#glow)" }, svg);
  const group = el("g", { transform: `translate(${x} ${y}) scale(${scale}) skewX(${-SLANT})` }, glowing);
  const segments = new Map();
  for (const [name, [x1, y1, x2, y2]] of Object.entries(SEGMENTS)) {
    segments.set(name, el("line", { class: "seg", x1, y1, x2, y2, "stroke-width": STROKE }, group));
  }
  const [cx1, cy1, cx2, cy2] = COMMA_TAIL;
  const comma = el("line", { class: "seg", x1: cx1, y1: cy1, x2: cx2, y2: cy2, "stroke-width": STROKE * 0.8 }, group);
  // after the tail, which starts at the point's center, so a dim tail
  // does not cross a lit point
  const point = el("circle", { class: "dot", cx: POINT[0], cy: POINT[1], r: DOT_RADIUS }, group);
  const colon = el("circle", { class: "dot", cx: COLON_DOT[0], cy: COLON_DOT[1], r: DOT_RADIUS }, group);
  return { segments, point, comma, colon };
}

// What a drawing draws into, by name, as `draw` returns it.
//   lamps     indicator name -> [element]
//   keyLamps  the key lamps, by their bit in the lamp byte
//   cells     the display's cells, each { segments: Map, point, comma,
//             colon (its upper dot) }
//   keys      each key's element, by its bit
function drawn() {
  return { lamps: new Map(), keyLamps: [], cells: [], keys: [] };
}

function remember(shown, name, node) {
  if (!shown.lamps.has(name)) shown.lamps.set(name, []);
  shown.lamps.get(name).push(node);
}

function lamp(svg, cx, cy, radius) {
  return el("circle", { class: "lamp", cx, cy, r: radius }, svg);
}

// An LED in its color, a lens over it, lit or dim.
function led(svg, color, cx, cy) {
  const node = el("circle", { class: "led", cx, cy, r: LED_RADIUS, fill: color }, svg);
  el("circle", { cx, cy, r: LED_RADIUS, fill: "url(#dome)", "pointer-events": "none" }, svg);
  return node;
}

// A key, its lamp to its left, calling `press` when clicked.
function key(svg, shown, legend, style, x, y, bit, press) {
  const [cap, ink] = style;
  const label = legend.split(" ");
  shown.keyLamps[bit] = lamp(svg, x + KEY_LAMP.dx, y + KEY_LAMP.dy, LAMP_RADIUS);
  const node = el("g", { class: "key", tabindex: 0, role: "button", "aria-label": label.join(" ") }, svg);
  el("rect", { class: "shade", x: x + 2, y: y + 4, width: KEY_WIDTH, height: KEY_HEIGHT, rx: 4 }, node);
  const face = el("g", { class: "face" }, node);
  el("rect", { class: "cap", x, y, width: KEY_WIDTH, height: KEY_HEIGHT, rx: 4, fill: cap }, face);
  label.forEach((line, index) => {
    text(line, { x: x + KEY_LEGEND.dx, y: y + KEY_LEGEND.baseline + index * KEY_LEGEND.leading, fill: ink }, face);
  });
  node.addEventListener("click", () => press(bit, node));
  node.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      press(bit, node);
    }
  });
  shown.keys[bit] = node;
}

// What draws each kind of part, given the drawing's canvas, the part,
// the face's height, what the drawing draws into, and what a key calls.
const PARTS = {
  // The face's outline, inset by half its stroke.
  bezel(svg, part, height) {
    el("rect", {
      class: part.class, x: BEZEL_INSET, y: BEZEL_INSET,
      width: DRAWING_WIDTH - 2 * BEZEL_INSET, height: height - 2 * BEZEL_INSET, rx: part.rx,
    }, svg);
  },
  rect(svg, part) {
    el("rect", { class: part.class, x: part.x, y: part.y, width: part.width, height: part.height, rx: part.rx }, svg);
  },
  heading(svg, part) {
    heading(part.onLight ? el("g", { class: "on-light" }, svg) : svg, part.model, part.layout);
  },
  display(svg, part, height, shown) {
    const pitch = (GLYPH_WIDTH + CELL_GAP) * part.scale;
    for (let index = 0; index < CELLS; index++) {
      shown.cells.push(cell(svg, part.x + index * pitch, part.y, part.scale));
    }
  },
  // A group's box with its bar along its top, the bar's ends rounded
  // with the box's corners, and its title in a gap of the bar.
  group(svg, { title, x, y, width, height }) {
    el("rect", { class: "group", x, y, width, height, rx: GROUP_RADIUS }, svg);
    const middle = x + width / 2;
    const gap = title.length * 9 + 24;
    const bottom = y + GROUP_BAR;
    el("path", {
      class: "rule",
      d: `M ${x} ${bottom} V ${y + GROUP_RADIUS} Q ${x} ${y} ${x + GROUP_RADIUS} ${y} H ${middle - gap / 2} V ${bottom} Z`,
    }, svg);
    el("path", {
      class: "rule",
      d: `M ${middle + gap / 2} ${bottom} V ${y} H ${x + width - GROUP_RADIUS} Q ${x + width} ${y} ${x + width} ${y + GROUP_RADIUS} V ${bottom} Z`,
    }, svg);
    text(title, { class: "group-title", x: middle, y: bottom, "text-anchor": "middle" }, svg);
  },
  // A row of indicators, each in its slot's column, with its label: an
  // LED where it has a color, a lamp where not.
  row(svg, part, height, shown) {
    const { label } = part;
    const labels = label.onLight ? el("g", { class: "on-light" }, svg) : svg;
    for (const indicator of part.indicators) {
      const x = part.xs[indicator.slot];
      const caption = () => text(indicator.label, {
        class: label.class, x: x + label.dx, y: part.y + label.dy,
        ...(label.anchor ? { "text-anchor": label.anchor } : {}),
      }, labels);
      if (label.first) caption();
      const node = indicator.color ? led(svg, indicator.color, x, part.y) : lamp(svg, x, part.y, LAMP_RADIUS);
      remember(shown, indicator.name, node);
      if (!label.first) caption();
    }
  },
  // The keys, four to a row, each right of its column's lamp, and what
  // is printed beside them.
  keys(svg, part, height, shown, press) {
    const perRow = part.columns.length;
    part.keys.forEach(([label, style], bit) => {
      const column = bit % perRow;
      const row = Math.floor(bit / perRow);
      key(svg, shown, label, style, part.columns[column] + KEY_OFFSET, part.y + row * KEY_ROW_PITCH, bit, press);
    });
    const { aside } = part;
    const asideX = part.columns[perRow - 1] + KEY_OFFSET + KEY_WIDTH + aside.gap;
    aside.lines.forEach((line, index) => {
      text(line, { class: "clear-alarm", x: asideX, y: aside.y + index * aside.leading }, svg);
    });
  },
  // Thumbscrews, each a slotted head.
  screws(svg, part) {
    for (const cx of part.xs) {
      const cy = part.y;
      el("circle", { class: "screw", cx, cy, r: part.radius }, svg);
      el("line", { x1: cx - part.slot, y1: cy, x2: cx + part.slot, y2: cy, stroke: "#3a3a3a", "stroke-width": 3 }, svg);
    }
  },
  path(svg, part) {
    el("path", { class: part.class, d: part.d }, svg);
  },
};

// The drawing named `name` into `container`, a key clicked calling
// `press` with its bit and its element: what it draws into (`drawn`).
function draw(container, name, press) {
  const drawing = DRAWINGS[name] || DRAWINGS[FALLBACK_DRAWING];
  const height = faceHeight(drawing.face);
  const svg = el("svg", { viewBox: `0 0 ${DRAWING_WIDTH} ${height + drawing.below}` });
  filters(svg);
  container.replaceChildren(svg);
  const shown = drawn();
  for (const part of drawing.parts) {
    PARTS[part.kind](svg, part, height, shown, press);
  }
  return shown;
}
