// The README's screenshots, taken from a running web view or from a
// directory of logs.
//
//     node tools/screenshots.mjs --url http://apu-0:9980 --receiver 3710A01056
//     node tools/screenshots.mjs --log-dir SNAPSHOT --receiver 3710A01056
//     node tools/screenshots.mjs ... --tui 'ssh -t apu-0 smartclockmon --daemon /run/smartclockd/ttyUSB0/socket'
//
// With --log-dir, a smartclock-web of this tree (target/debug, built
// first) serves the directory on a spare port, with its sensors.sqlite
// if it has one, and no daemon: the history pages only.  Each page is
// shown over the SPAN seconds up to the receiver's last reading, so a
// snapshot gives the same pictures every time it is shot.
// The compare page is shot only where there are two receivers or more.
// The status page is not shot: it shows where the receiver is, and
// reading it sends a query to the receiver.
//
// --tui runs COMMAND in a detached tmux window of a fixed size, waits
// for it to fill, and shoots what tmux holds, colors and all, drawn
// as text in the same browser.  The position's values are blanked:
// where a bench is never goes into the repository.
//
// Playwright and its Chromium are the browser tests' (`make web-deps`).

import { spawn, execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { mkdtempSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { parseArgs } from "node:util";

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
const TESTS = path.join(ROOT, "crates/smartclock-web/tests/browser");
// The browser tests' Chromium, which lives beside their node_modules.
process.env.PLAYWRIGHT_BROWSERS_PATH = "0";
const { chromium } = createRequire(path.join(TESTS, "package.json"))("@playwright/test");

// Window size for the web pages, in CSS pixels: desktop, as the
// README's readers mostly see the pages.  What shows in it is shot,
// not the whole page, which runs to several screens.
const VIEWPORT = { width: 1400, height: 950 };
// The terminal the TUI is drawn in, in cells, as its own tests draw it.
const TUI_SIZE = { columns: 120, rows: 40 };
// How long the TUI is given to connect and fill its views, in ms.
const TUI_SETTLE = 20000;
// How long a page is given after its charts appear, for the readout and
// the strip to fill, in ms.
const SETTLE = 2000;
const PORT = 9984;

const { values: opts } = parseArgs({
  options: {
    url: { type: "string" },
    "log-dir": { type: "string" },
    receiver: { type: "string" },
    span: { type: "string", default: "21600" },
    tui: { type: "string" },
    out: { type: "string", default: path.join(ROOT, "docs/images") },
  },
});
const OUT = path.resolve(opts.out);
if (!opts.url === !opts["log-dir"] && !opts.tui) {
  console.error("usage: screenshots.mjs (--url URL | --log-dir DIR) [--receiver SERIAL] [--span SECONDS] [--tui COMMAND] [--out DIR]");
  process.exit(2);
}

// The pages, each with the address after `?` and what shows it is drawn.
const PAGES = [
  ["web-live.png", "/", "#charts .uplot"],
  ["web-stability.png", "/adev", ".uplot"],
  ["web-compare.png", "/compare", ".uplot"],
  ["web-correlation.png", "/correlation", ".uplot", "x=sensor:temperature:ambient&y=efc_percent"],
];

async function serve(logDir) {
  execFileSync("cargo", ["build", "-q", "-p", "smartclock-web"], { cwd: ROOT, stdio: "inherit" });
  const empty = mkdtempSync(path.join(tmpdir(), "screenshots-"));
  const sensors = path.join(logDir, "sensors.sqlite");
  const server = spawn(path.join(ROOT, "target/debug/smartclock-web"), [
    "--listen", `127.0.0.1:${PORT}`, "--log-dir", logDir, "--run-dir", empty,
    "--sensor-log", existsSync(sensors) ? sensors : path.join(empty, "none.sqlite"),
    "--sensord", path.join(empty, "none.socket"),
  ], { stdio: "inherit" });
  const url = `http://127.0.0.1:${PORT}`;
  for (let tries = 0; ; tries++) {
    try {
      await fetch(`${url}/api/receivers`);
      return { url, server };
    } catch (e) {
      if (tries > 50) throw e;
      await new Promise((r) => setTimeout(r, 100));
    }
  }
}

async function web(browser, url) {
  const receivers = await (await fetch(`${url}/api/receivers`)).json();
  const receiver = receivers.find((r) => r.serial === opts.receiver) ?? receivers[0];
  if (!receiver) throw new Error(`no receivers at ${url}`);
  const to = Math.floor(Date.parse(receiver.last_seen) / 1000);
  const range = `receiver=${receiver.serial}&from=${to - Number(opts.span)}&to=${to}&refresh=off`;
  const page = await browser.newPage({ viewport: VIEWPORT });
  const shots = PAGES.filter(([, where]) => where !== "/compare" || receivers.length > 1);
  for (const [file, where, drawn, extra] of shots) {
    await page.goto(`${url}${where}?${range}${extra ? `&${extra}` : ""}`);
    await page.waitForSelector(drawn);
    // The cursor partway across the first chart, so the readout under
    // the charts has values to show.
    const chart = await page.locator(drawn).first().boundingBox();
    if (chart) await page.mouse.move(chart.x + chart.width * 0.6, chart.y + chart.height / 2);
    await page.waitForTimeout(SETTLE);
    await page.screenshot({ path: path.join(OUT, file) });
    console.log(file);
  }
  await page.close();
}

// What tmux holds of COMMAND's window, as HTML: the text with its
// colors, from `capture-pane -e`'s SGR escapes.
function tui(command) {
  const session = `screenshots-${process.pid}`;
  execFileSync("tmux", ["new-session", "-d", "-s", session,
    "-x", String(TUI_SIZE.columns), "-y", String(TUI_SIZE.rows), command]);
  try {
    execFileSync("sleep", [String(TUI_SETTLE / 1000)]);
    return hidePosition(execFileSync("tmux", ["capture-pane", "-p", "-e", "-t", session]).toString());
  } finally {
    execFileSync("tmux", ["kill-session", "-t", session]);
  }
}

// The TUI's position lines with every number in them dashed out,
// the layout kept.  The numbers in SGR escapes are left alone.
const POSITION_LINE = /latitude|longitude|height/i;
function hidePosition(text) {
  return text.split("\n").map((line) => !POSITION_LINE.test(line) ? line
    : line.split(/(\x1b\[[0-9;]*m)/).map((part) => part.startsWith("\x1b")
      ? part : part.replace(/[+-]?\d[\d.]*/g, (n) => "-".repeat(n.length))).join(""),
  ).join("\n");
}

// The 16 colors as the browser views draw their dark theme, and the
// xterm 256-color cube and ramp after them.
const ANSI16 = ["#16181d", "#f87171", "#4ade80", "#fbbf24", "#60a5fa", "#c084fc", "#2dd4bf", "#cbd5e1",
  "#64748b", "#fca5a5", "#86efac", "#fde68a", "#93c5fd", "#d8b4fe", "#5eead4", "#f8fafc"];
function color256(n) {
  if (n < 16) return ANSI16[n];
  if (n < 232) {
    const level = (v) => (v ? 55 + v * 40 : 0);
    const i = n - 16;
    return `rgb(${level(Math.floor(i / 36))},${level(Math.floor(i / 6) % 6)},${level(i % 6)})`;
  }
  const g = 8 + (n - 232) * 10;
  return `rgb(${g},${g},${g})`;
}

function ansiToHtml(text) {
  const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;");
  let style = {};
  let out = "";
  for (const part of text.split(/(\x1b\[[0-9;]*m)/)) {
    const sgr = part.match(/^\x1b\[([0-9;]*)m$/);
    if (!sgr) {
      if (!part) continue;
      const css = Object.entries(style).map(([k, v]) => `${k}:${v}`).join(";");
      out += css ? `<span style="${css}">${esc(part)}</span>` : esc(part);
      continue;
    }
    const codes = sgr[1] ? sgr[1].split(";").map(Number) : [0];
    for (let i = 0; i < codes.length; i++) {
      const c = codes[i];
      if (c === 0) style = {};
      else if (c === 1) style["font-weight"] = "bold";
      else if (c === 2) style.opacity = "0.6";
      else if (c === 7) style.filter = "invert(1)";
      else if (c === 22) { delete style["font-weight"]; delete style.opacity; }
      else if (c === 39) delete style.color;
      else if (c === 49) delete style.background;
      else if (c >= 30 && c <= 37) style.color = ANSI16[c - 30];
      else if (c >= 90 && c <= 97) style.color = ANSI16[c - 82];
      else if (c >= 40 && c <= 47) style.background = ANSI16[c - 40];
      else if (c >= 100 && c <= 107) style.background = ANSI16[c - 92];
      else if (c === 38 || c === 48) {
        const key = c === 38 ? "color" : "background";
        if (codes[i + 1] === 5) { style[key] = color256(codes[i + 2]); i += 2; }
        else if (codes[i + 1] === 2) { style[key] = `rgb(${codes[i + 2]},${codes[i + 3]},${codes[i + 4]})`; i += 4; }
      }
    }
  }
  return out;
}

async function shootTui(browser, command) {
  const page = await browser.newPage({ viewport: { width: 1200, height: 800 } });
  await page.setContent(
    `<body style="margin:0;background:${ANSI16[0]}"><pre id="t" style="display:inline-block;margin:0;padding:8px;` +
    `font:14px/1.2 'DejaVu Sans Mono',monospace;color:${ANSI16[7]}">${ansiToHtml(tui(command))}</pre></body>`,
  );
  await page.locator("#t").screenshot({ path: path.join(OUT, "tui-dashboard.png") });
  console.log("tui-dashboard.png");
  await page.close();
}

const browser = await chromium.launch();
let server = null;
try {
  if (opts.url || opts["log-dir"]) {
    const site = opts.url ? { url: opts.url.replace(/\/$/, "") } : await serve(path.resolve(opts["log-dir"]));
    server = site.server;
    await web(browser, site.url);
  }
  if (opts.tui) await shootTui(browser, opts.tui);
} finally {
  await browser.close();
  server?.kill();
}
