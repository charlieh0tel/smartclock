// smartclock-emu's panel page's script (docs/emulator.md, "The panel
// page"): it polls /state and posts a clicked key to /key?name=NAME, and
// a simulation control from the strip below the panel to
// /event?name=NAME ("The controls"); its console polls /console and
// posts a typed line there ("The console").  What it shares with the run
// it reads from /constants.json, and the display's font from font.json.
"use strict";

// How long a key looks pressed after a click, milliseconds.
const PRESS_MS = 160;
// How long the page waits to ask again for what it starts from, when
// the run has not answered, milliseconds.
const START_RETRY_MS = 1000;
// How long the page waits for any answer before it takes the run as
// not answering, milliseconds: many polls, so an answer slowed by a busy
// host is not taken for a hung run, and short enough that a hung run is
// not shown running for long.  Without it a poll that never returns
// would leave the page as it last was, "running", for good.
const ANSWER_MS = 2000;

// `fetch`, given up after ANSWER_MS; or, on a host that answers the
// page's requests itself rather than serving them, the browser's build
// (crates/smartclock-emu-web), the `smartclockAsk(path, options)` it
// set before this script, which answers with a `Response` as `fetch`
// does.
function ask(path, options = {}) {
  if (typeof globalThis.smartclockAsk === "function") return globalThis.smartclockAsk(path, options);
  return fetch(path, { ...options, signal: AbortSignal.timeout(ANSWER_MS) });
}

// The run's constants (/constants.json): poll_ms, line_limit, kept,
// controls, typists and quick_lines.
let CONSTANTS = null;
// The display's font (font.json): `glyphs`, each character's lit
// segments, and `marks`, the characters that are marks on the character
// before them.  The 58503A's text goes to the panel's own controller as
// ASCII, so its font is not in 3704.  The table is the 53131A's, the
// same HP display, from the segment words its CPU sends, decoded in
// hp_display (GPL-3.0, its table re-expressed here, not copied):
// https://github.com/ragges/hp_display, codes in
// extras/codes-mapped.list, a word's bits to segments in
// extras/charmap.py's code2segs.  Where the photograph of the bench
// 58503A showing "UTC 01:34:43" differs, it wins: 1 is the right-hand
// verticals, not the 53131A's center bars, and 3's middle bar is whole.
// J and O, and the punctuation 3704 never shows, are not in either and
// are inferred.  '@' lights every one (inferred); with ';' after it,
// which lights every mark, it is the display test's pattern: 0x52b1c
// steps "@;" across the display in lines of thirteen bytes, one
// character cell each, so the pair is one cell.  The marks are the
// 53131A's three separator bits (hp_display's hp_msg_parse.cpp): the
// point, the point with the comma's tail, the point with the colon's
// upper dot, and all three.
let FONT = null;

// What the drawing shown draws into (drawings.js, `drawn`), the keys'
// names, in the lamp byte's order, as the state gives them, and whether
// the run answered the last poll.
let shown = drawn();
let keyNames = [];
let running = false;

// A key pressed: shown pressed for a moment, and posted by its name;
// nothing while the run is not answering, its panel out, or for a key
// the state does not name.
function press(bit, node) {
  const name = keyNames[bit];
  if (!running || !name) return;
  node.classList.add("pressed");
  setTimeout(() => node.classList.remove("pressed"), PRESS_MS);
  ask(`/key?name=${encodeURIComponent(name)}`, { method: "POST" }).catch(() => {});
}

// `text` as cells: each character in one, a mark on the cell before it
// unless that cell has one, else in a cell of its own.
function cellsOf(text) {
  const out = [];
  for (const character of text) {
    const mark = FONT.marks[character];
    const last = out[out.length - 1];
    if (mark && last && !last.mark) {
      last.mark = mark;
      continue;
    }
    out.push({ glyph: mark ? " " : character, mark: mark || null });
  }
  return out.slice(0, CELLS);
}

function showText(text) {
  const glyphs = FONT.glyphs;
  const cells = cellsOf(text);
  shown.cells.forEach((target, index) => {
    const content = cells[index] || { glyph: " ", mark: null };
    const glyph = glyphs[content.glyph] ?? glyphs[content.glyph.toUpperCase()] ?? glyphs["?"];
    for (const [name, node] of target.segments) {
      node.classList.toggle("on", glyph.includes(name));
    }
    const mark = content.mark || {};
    target.point.classList.toggle("on", !!mark.point);
    target.comma.classList.toggle("on", !!mark.comma);
    target.colon.classList.toggle("on", !!mark.colon);
  });
}

function setLit(name, lit) {
  for (const node of shown.lamps.get(name) || []) {
    node.classList.toggle("on", lit);
  }
}

// The control strip: each button by the control it posts, the antenna's
// a toggle between two (docs/emulator.md, "The controls").  The
// buttons' ids are the page's own.
const controls = { antenna: null };

function postEvent(name) {
  ask(`/event?name=${encodeURIComponent(name)}`, { method: "POST" }).catch(() => {});
}

// The strip's buttons other than the antenna's, each by its id and the
// constant naming the control it posts.
function plainControls() {
  const names = CONSTANTS.controls;
  return [["skip-warm-up", names.skip_warm_up], ["power-cycle", names.power_cycle], ["reset", names.reset]];
}

function wireControls() {
  const names = CONSTANTS.controls;
  document.getElementById("antenna").addEventListener("click", () => {
    if (controls.antenna !== null) postEvent(controls.antenna ? names.antenna_off : names.antenna_on);
  });
  for (const [id, name] of plainControls()) {
    document.getElementById(id).addEventListener("click", () => postEvent(name));
  }
}

function showControls(state) {
  const names = new Set(state.names);
  const posted = CONSTANTS.controls;
  const antenna = document.getElementById("antenna");
  antenna.hidden = !names.has(posted.antenna_off);
  controls.antenna = state.antenna;
  antenna.disabled = state.antenna === null;
  antenna.textContent = state.antenna === false ? "reconnect antenna" : "disconnect antenna";
  antenna.setAttribute("aria-pressed", String(state.antenna === false));
  const skip = document.getElementById("skip-warm-up");
  skip.hidden = !names.has(posted.skip_warm_up);
  skip.disabled = state.warming !== true;
  skip.textContent = state.warming === false ? "warmed up" : "skip warm-up";
  // the power cycle and the reset, which always apply
  for (const [id, name] of plainControls().slice(1)) {
    const button = document.getElementById(id);
    button.hidden = !names.has(name);
    button.disabled = false;
  }
}

let drawnName = null;

function show(state) {
  if (drawnName !== state.drawing) {
    shown = draw(document.getElementById("panel"), state.drawing, press);
    drawnName = state.drawing;
  }
  keyNames = state.keys;
  setKeysLive(true);
  setLit("power", true);
  const boardLeds = [];
  for (const indicator of state.indicators) {
    if (indicator.bank === "lamp") {
      setLit(indicator.name, indicator.lit);
    } else {
      boardLeds.push(indicator);
    }
  }
  if (state.display) {
    showText(state.display.text);
    shown.keyLamps.forEach((node, bit) => {
      node.classList.toggle("on", (state.display.lamps >> bit & 1) === 1);
    });
  }
  const strip = document.getElementById("board-leds");
  strip.replaceChildren(...boardLeds.map((indicator) => {
    const span = document.createElement("span");
    span.textContent = indicator.name;
    span.className = indicator.lit ? "lit" : "";
    return span;
  }));
  showControls(state.controls);
  document.getElementById("cycle").textContent = state.cycle.toLocaleString("en-US");
  document.getElementById("seconds").textContent = state.seconds.toFixed(3);
  const link = document.getElementById("link");
  link.textContent = "running";
  link.className = "";
}

// The keys pressable, or not while the run is down.
function setKeysLive(live) {
  running = live;
  for (const node of shown.keys) node.setAttribute("aria-disabled", String(!live));
}

function down() {
  setKeysLive(false);
  setLit("power", false);
  for (const node of shown.keyLamps) node.classList.remove("on");
  for (const [name] of shown.lamps) setLit(name, false);
  if (shown.cells.length) showText("");
  for (const button of document.querySelectorAll(".controls button")) button.disabled = true;
  const link = document.getElementById("link");
  link.textContent = "not answering";
  link.className = "down";
}

async function poll() {
  try {
    const response = await ask("/state", { cache: "no-store" });
    if (!response.ok) throw new Error(response.status);
    show(await response.json());
  } catch (error) {
    down();
  }
  setTimeout(poll, CONSTANTS.poll_ms);
}

// The console: the host port's output from an offset, and one line at a
// time typed back while this page holds the port (docs/emulator.md,
// "The console").  The page's name for itself holds its lease.
const CLIENT = (crypto.randomUUID ? crypto.randomUUID() : String(Math.random()).slice(2));
// What the console has read: the run it read from, by its name, the
// offset its next poll asks from, and until when, a time in
// milliseconds, a refusal stays where the console says who has the port.
const consoleState = { run: null, offset: 0, refusedUntil: 0 };
// How long a refusal of a line stays shown, milliseconds: long enough
// to read, where the next poll would otherwise say who has the port
// again a moment later.
const REFUSAL_MS = 4000;

// `why` a line was not sent, shown for REFUSAL_MS.
function refuse(why) {
  document.getElementById("console-who").textContent = `host port: ${why}`;
  consoleState.refusedUntil = Date.now() + REFUSAL_MS;
}

// The port's text as the pane shows it: CR dropped, LF and tab kept,
// backspace taking the character before, any other byte not printable
// ASCII as \xHH, dimmed.  The pane keeps as many characters as the run
// keeps bytes.
function appendOutput(text) {
  const pre = document.getElementById("console-output");
  const atEnd = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 4;
  let plain = "";
  const flush = () => {
    if (plain) pre.append(plain);
    plain = "";
  };
  for (const character of text) {
    const code = character.charCodeAt(0);
    if (character === "\r") continue;
    if (character === "\n" || character === "\t" || (code >= 0x20 && code < 0x7f)) {
      plain += character;
    } else if (code === 0x08) {
      if (plain) {
        plain = plain.slice(0, -1);
      } else {
        flush();
        const last = pre.lastChild;
        if (last && last.nodeType === Node.TEXT_NODE && last.data.length) {
          last.data = last.data.slice(0, -1);
        }
      }
    } else {
      flush();
      const span = document.createElement("span");
      span.className = "byte";
      span.textContent = "\\x" + code.toString(16).padStart(2, "0");
      pre.append(span);
    }
  }
  flush();
  trimOutput(pre, CONSTANTS.kept);
  if (atEnd) pre.scrollTop = pre.scrollHeight;
}

// The pane's oldest characters dropped until it holds `kept`, its
// newest: a byte's \xHH whole, plain text from its front, so that one
// long answer, a single text node, loses its start and not all of it.
function trimOutput(pre, kept) {
  let over = pre.textContent.length - kept;
  while (over > 0 && pre.firstChild) {
    const first = pre.firstChild;
    const length = first.textContent.length;
    if (first.nodeType === Node.TEXT_NODE && length > over) {
      first.data = first.data.slice(over);
      return;
    }
    first.remove();
    over -= length;
  }
}

// The console's quick lines (/constants.json's `quick_lines`), each a
// button that sends its line as though typed.
function wireQuickLines() {
  const row = document.getElementById("console-quick");
  for (const { label, line } of CONSTANTS.quick_lines) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    button.title = line;
    button.disabled = true;
    button.addEventListener("click", () => sendLine(line));
    row.append(button);
  }
}

// The input and the quick lines, usable while this page holds the port.
function setWritable(writable) {
  document.getElementById("console-input").disabled = !writable;
  for (const button of document.querySelectorAll("#console-quick button")) {
    button.disabled = !writable;
  }
}

function showConsole(state) {
  // An emulator started again counts its offsets from 0 again, so an
  // answer to an offset of the run before is set aside, and the next
  // poll asks from the new run's start.
  if (consoleState.run !== null && state.run !== consoleState.run) {
    appendOutput("\n[the emulator started again]\n");
    consoleState.offset = 0;
  } else {
    if (state.from > consoleState.offset && consoleState.offset > 0) {
      appendOutput(`\n[${state.from - consoleState.offset} bytes not kept]\n`);
    }
    if (state.text) appendOutput(state.text);
    consoleState.offset = state.offset;
  }
  consoleState.run = state.run;
  const input = document.getElementById("console-input");
  setWritable(state.writable);
  input.placeholder = state.writable ? "a line for the port, Enter to send" : state.reason;
  if (Date.now() < consoleState.refusedUntil) return;
  document.getElementById("console-who").textContent = state.writable
    ? "SCPI port: yours"
    : `SCPI port: read-only, ${CONSTANTS.typists[state.typist] || state.typist}`;
}

async function pollConsole() {
  try {
    const query = `since=${consoleState.offset}&client=${encodeURIComponent(CLIENT)}`;
    const response = await ask(`/console?${query}`, { cache: "no-store" });
    if (!response.ok) throw new Error(response.status);
    showConsole(await response.json());
  } catch (error) {
    setWritable(false);
    document.getElementById("console-who").textContent = "host port: not answering";
  }
  setTimeout(pollConsole, CONSTANTS.poll_ms);
}

// Posts `line` for the port, as typed or from a quick line: whether it
// was queued, the refusal shown if not.
async function sendLine(line) {
  try {
    const response = await ask(`/console?client=${encodeURIComponent(CLIENT)}`, {
      method: "POST",
      body: line,
    });
    if (response.ok) return true;
    refuse(`refused, ${(await response.text()).trim()}`);
  } catch (error) {
    refuse("not answering");
  }
  return false;
}

// A line typed and sent with Enter.  The input sets no length of its
// own, which would cut a pasted line short without a word and send what
// was left; a line past the run's limit, in bytes as the run counts them,
// is refused here as the run would refuse it, and kept to be edited.
document.getElementById("console-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const input = document.getElementById("console-input");
  if (new TextEncoder().encode(input.value).length > CONSTANTS.line_limit) {
    refuse(`refused, a line is at most ${CONSTANTS.line_limit} bytes`);
    return;
  }
  if (await sendLine(input.value)) input.value = "";
});

// `path`'s JSON, or a failure.
async function load(path) {
  const response = await ask(path, { cache: "no-store" });
  if (!response.ok) throw new Error(response.status);
  return response.json();
}

// The constants and the font, asked again until the run answers, then
// the controls, the console and the polls.
async function start() {
  try {
    [CONSTANTS, FONT] = await Promise.all([load("/constants.json"), load("/font.json")]);
  } catch (error) {
    down();
    setTimeout(start, START_RETRY_MS);
    return;
  }
  wireControls();
  wireQuickLines();
  poll();
  pollConsole();
}

start();
