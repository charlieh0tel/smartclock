// The browser's build's loader (docs/emulator.md, "In a browser"): it
// fetches the run's files from the static build, starts the 58503A's
// run in the page (pkg/, wasm-bindgen's bindings of
// crates/smartclock-emu-web), steps it a frame at a time from
// requestAnimationFrame, and answers the panel page's requests from it
// (`ask`, which host.js hands panel.js).  The EEPROM's cells and its
// protection are kept in this browser's storage, and a later visit
// starts from them; the
// synthetic engine's place is the firmware's own unless the page is
// asked to use this browser's, and its start this browser's clock.
// Nothing leaves the browser.

import init, { Emulator, onPanic } from "./pkg/smartclock_emu_web.js";

// The run's files, by their paths in the static build, which keeps the
// repository's: the board file, the image, and the EEPROM's image, the
// bench unit's dump, the board file names.  The
// EEPROM's image is the cells this browser kept, when it kept them.
const BOARD = "boards/58503a.toml";
const IMAGE = "third_party/firmware/images/58503a-3704.bin";
const DUMP = "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin";

// Where this browser keeps the EEPROM, its cells and its protection as
// JSON, and the place it was asked to use, in localStorage.
const SAVED_EEPROM = "smartclock-emu-web/58503a/eeprom";
const SAVED_PLACE = "smartclock-emu-web/58503a/place";
// How often the EEPROM is kept, milliseconds, beside when
// the page is hidden or left.
const SAVE_EVERY_MS = 10_000;
// The decimal places a place is kept to: a tenth of a meter, past what
// a browser's position is good to.
const PLACE_DIGITS = 6;
// How long the browser may take to say where it is, milliseconds.
const PLACE_TIMEOUT_MS = 30_000;

const encoder = new TextEncoder();

// What the loader reports, for a test, or a curious console: the
// frames run, how far the board was behind the page's clock after the
// last, milliseconds, and why the run stopped, if it has.
const stats = { frames: 0, behind: 0, stopped: null };
globalThis.smartclockWeb = stats;

// The browser's strip, below the simulation's: where the run's state
// and its storage are said, and the buttons for the place and the
// EEPROM kept.
function strip() {
  const group = document.createElement("div");
  group.className = "controls";
  group.setAttribute("role", "group");
  group.setAttribute("aria-label", "This browser's");
  group.innerHTML =
    '<span class="heading">browser</span>' +
    '<button type="button" id="web-place">use my location</button>' +
    '<button type="button" id="web-forget">forget the saved EEPROM</button>' +
    '<span id="web-status">starting</span>';
  document.querySelector(".controls").after(group);
}

function say(text) {
  const status = document.getElementById("web-status");
  if (status) status.textContent = text;
}

// localStorage's `key`, or null where storage is blocked or empty.
function load(key) {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function store(key, value) {
  try {
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, value);
    return true;
  } catch {
    return false;
  }
}

function base64(bytes) {
  let text = "";
  for (const byte of bytes) text += String.fromCharCode(byte);
  return btoa(text);
}

function unbase64(text) {
  return Uint8Array.from(atob(text), (character) => character.charCodeAt(0));
}

// `path`'s bytes from the static build, beside this module.
async function fetched(path) {
  const response = await fetch(new URL(path, import.meta.url));
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return new Uint8Array(await response.arrayBuffer());
}

// The place this browser was asked to use, if any: { latitude,
// longitude }, degrees.
function savedPlace() {
  try {
    const place = JSON.parse(load(SAVED_PLACE));
    return Number.isFinite(place?.latitude) && Number.isFinite(place?.longitude) ? place : null;
  } catch {
    return null;
  }
}

// The EEPROM this browser kept, if any: { cells, a Uint8Array,
// protected }; none where what is kept is not one.
function savedEeprom() {
  try {
    const kept = JSON.parse(load(SAVED_EEPROM));
    return typeof kept?.cells === "string" && typeof kept?.protected === "boolean"
      ? { cells: unbase64(kept.cells), protected: kept.protected }
      : null;
  } catch {
    return null;
  }
}

// The run's arguments, as the command line's: the image, the EEPROM's
// protection `kept` has, if it is kept, and the place, if one was asked
// for.
function args(place, kept) {
  const protection = kept ? ["--eeprom-protected", String(kept.protected)] : [];
  const placed = place
    ? ["--gps-latitude", String(place.latitude), "--gps-longitude", String(place.longitude)]
    : [];
  return [IMAGE, ...protection, ...placed];
}

const [, board, image, dump] = await Promise.all([
  init(),
  fetched(BOARD),
  fetched(IMAGE),
  fetched(DUMP),
]);

// The run's files, the EEPROM's image the cells `kept` has, if it is
// kept, or else the dump.
function files(kept) {
  return new Map([
    [BOARD, board],
    [IMAGE, image],
    [DUMP, kept ? kept.cells : dump],
  ]);
}

// The run's EEPROM as it stands: { cells, protected }, or null once the
// run has failed.
function eeprom() {
  const kept = emulator.eeprom();
  if (!kept) return null;
  try {
    return { cells: kept.cells, protected: kept.protected };
  } finally {
    kept.free();
  }
}

strip();
onPanic((message) => stop(`panicked: ${message}`));

// The run, from the EEPROM this browser kept, or the bench unit's dump
// should the kept one not start (cells the part refuses).  What is kept
// but is not an EEPROM's is dropped.
let emulator;
let place = savedPlace();
{
  const kept = savedEeprom();
  if (!kept) store(SAVED_EEPROM, null);
  try {
    emulator = new Emulator(args(place, kept), files(kept), Date.now());
    say(kept ? "started from the EEPROM this browser kept" : "started from the bench unit's EEPROM");
  } catch (error) {
    if (!kept) throw error;
    console.warn(`the kept EEPROM did not start: ${error.message}`);
    store(SAVED_EEPROM, null);
    emulator = new Emulator(args(place, null), files(null), Date.now());
    say("started from the bench unit's EEPROM: the kept one did not start");
  }
}

// The run stopped, `why`: no more frames, and every request refused.
function stop(why) {
  if (stats.stopped) return;
  stats.stopped = why;
  say(`the run stopped: ${why}`);
}

// Whether the EEPROM is kept: not once it has been forgotten, until the
// next visit.
let keeping = true;

// The EEPROM kept in this browser.
function save() {
  if (stats.stopped || !keeping) return;
  try {
    const kept = eeprom();
    const text = kept && JSON.stringify({ cells: base64(kept.cells), protected: kept.protected });
    if (text && store(SAVED_EEPROM, text)) {
      say(`EEPROM kept in this browser at ${new Date().toLocaleTimeString()}`);
    }
  } catch (error) {
    stop(error.message);
  }
}

// The run started again from its EEPROM as it stands, the place
// `next`: what a power cycle with the antenna moved does.  The run
// before goes on if the new one cannot start.
function restart(next) {
  const kept = eeprom();
  const started = new Emulator(args(next, kept), files(kept), Date.now());
  emulator.free();
  emulator = started;
  place = next;
}

document.getElementById("web-forget").addEventListener("click", () => {
  keeping = false;
  store(SAVED_EEPROM, null);
  store(SAVED_PLACE, null);
  say("the saved EEPROM and place forgotten: the next visit starts from the bench unit's");
});

document.getElementById("web-place").addEventListener("click", () => {
  if (!navigator.geolocation) {
    say("this browser gives no location");
    return;
  }
  say("asking this browser where it is");
  navigator.geolocation.getCurrentPosition(
    (position) => {
      const next = {
        latitude: Number(position.coords.latitude.toFixed(PLACE_DIGITS)),
        longitude: Number(position.coords.longitude.toFixed(PLACE_DIGITS)),
      };
      try {
        restart(next);
        store(SAVED_PLACE, JSON.stringify(next));
        say(`antenna at ${next.latitude}, ${next.longitude}: the run started again there`);
      } catch (error) {
        say(`not started there: ${error.message}`);
      }
    },
    (error) => say(`no location: ${error.message}`),
    { timeout: PLACE_TIMEOUT_MS },
  );
});

setInterval(save, SAVE_EVERY_MS);
addEventListener("pagehide", save);
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "hidden") save();
});

function tick(time) {
  if (stats.stopped) return;
  try {
    const frame = emulator.frame(time);
    try {
      stats.frames += 1;
      stats.behind = frame.behind;
      if (!frame.running) {
        stop("its time is up");
        return;
      }
    } finally {
      frame.free();
    }
  } catch (error) {
    stop(error.message);
    return;
  }
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

// The panel page's request for `path`, as `fetch` answers it: a
// `Response`, from the run.
export function ask(path, options = {}) {
  if (stats.stopped) return Promise.reject(new Error(stats.stopped));
  const method = (options.method ?? "GET").toUpperCase();
  const body = options.body === undefined ? undefined : encoder.encode(String(options.body));
  const answer = emulator.respond(method, path, body);
  try {
    const headers = { "Content-Type": answer.kind };
    if (answer.allow !== undefined) headers.Allow = answer.allow;
    return Promise.resolve(new Response(answer.body, { status: answer.status, headers }));
  } finally {
    answer.free();
  }
}
