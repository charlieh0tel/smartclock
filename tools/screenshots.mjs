// The README's screenshots, taken from a running web view or from a
// directory of logs.
//
//     node tools/screenshots.mjs --url http://apu-0:9980 --receiver 3710A01056
//     node tools/screenshots.mjs --log-dir DIR [--log-dir DIR ...] --receiver 3710A01056
//
// With --log-dir, a smartclock-web of this tree (target/debug, built
// first) serves the logs on a spare port, with a sensors.sqlite if one
// is among them, and no daemon: the history pages only, without the
// live strip, which would say only that no daemon is attached.  Logs
// in several directories are linked into one scratch directory, not
// copied.  Each page is shown over the SPAN seconds up to the last
// reading every receiver has, so logs give the same pictures every
// time they are shot.
// The compare page is shot only where there are two receivers or more.
// The status page is not shot from a real receiver: it shows where the
// receiver is, and reading it sends a query to the receiver.  --sim
// shoots it from the simulator instead, under a daemon and a web view
// of this tree, each on a spare port or in a scratch directory.
//
// Playwright and its Chromium are the browser tests' (`make web-deps`).

import { spawn, execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { mkdtempSync, mkdirSync, existsSync, readdirSync, symlinkSync } from "node:fs";
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
// How long a page is given after its charts appear, for the readout and
// the strip to fill, in ms.
const SETTLE = 2000;
const PORT = 9984;
// The simulator's port and its web view's, for --sim.
const SIM_PORT = 9985;
const SIM_WEB_PORT = 9986;
// How long the simulated receiver's daemon is given to read it and
// list it, in ms.
const SIM_START = 30000;

const { values: opts } = parseArgs({
  options: {
    url: { type: "string" },
    "log-dir": { type: "string", multiple: true },
    receiver: { type: "string" },
    span: { type: "string", default: "21600" },
    // The stability pages' span, longer: a deviation wants a long run.
    "stability-span": { type: "string" },
    sim: { type: "boolean", default: false },
    against: { type: "string" },
    only: { type: "string", multiple: true },
    out: { type: "string", default: path.join(ROOT, "docs/images") },
  },
});
const OUT = path.resolve(opts.out);
if (!opts.url === !opts["log-dir"] && !opts.sim) {
  console.error("usage: screenshots.mjs (--url URL | --log-dir DIR) [--receiver SERIAL] [--span SECONDS] [--stability-span SECONDS] [--against MEASURE] [--only FILE ...] [--sim] [--out DIR]");
  process.exit(2);
}

// The pages, each with its path, what shows it is drawn, and the
// panels to shoot, from the one holding the first selector to the one
// holding the last; FULL for the whole page; the window's worth from
// the top without either.
const FULL = "full";
const PAGES = [
  // From the tabs down through the charts and their readout, without
  // the receiver's journal below them.
  ["web-live.png", "/", "#charts .uplot", ["nav", "#charts"]],
  ["web-stability.png", "/adev", "#chart-tdev .uplot", FULL],
  ["web-compare.png", "/compare", "#chart-deviation .uplot", FULL],
  // The receivers' stability side by side, at the foot of the page.
  ["web-compare-stability.png", "/compare", "#chart-deviation .uplot", ["#chart-deviation", "#chart-deviation"]],
  // The scatter and the lag, what the page is for, and the page.
  ["web-correlation.png", "/correlation", "#scatter .uplot", ["#scatter", "#lag"]],
  ["web-correlation-page.png", "/correlation", "#scatter .uplot", FULL],
];

// What the correlation page sets EFC against: --against, a measure as
// the page names it (`sensor:temperature:NAME`, `efc:SERIAL`), or else
// the room's temperature where there is a sensor for it, or else
// another receiver's EFC.
async function correlated(url, receivers, receiver) {
  if (opts.against) return `x=${opts.against}&y=efc_percent`;
  const { sensors = [] } = await (await fetch(`${url}/api/sensors`)).json().catch(() => ({}));
  const temperature = sensors.find((s) => s.quantity === "temperature");
  const other = receivers.find((r) => r.serial !== receiver.serial);
  const x = temperature ? `sensor:temperature:${temperature.name}` : other ? `efc:${other.serial}` : null;
  return x ? `x=${x}&y=efc_percent` : "";
}

async function serve(logDirs) {
  execFileSync("cargo", ["build", "-q", "-p", "smartclock-web"], { cwd: ROOT, stdio: "inherit" });
  const empty = mkdtempSync(path.join(tmpdir(), "screenshots-"));
  let logDir = logDirs[0];
  if (logDirs.length > 1) {
    logDir = path.join(empty, "logs");
    mkdirSync(logDir);
    for (const dir of logDirs) {
      for (const name of readdirSync(dir).filter((n) => /\.sqlite(-wal|-shm)?$/.test(n))) {
        symlinkSync(path.join(dir, name), path.join(logDir, name));
      }
    }
  }
  const sensors = path.join(logDir, "sensors.sqlite");
  const server = spawn(path.join(ROOT, "target/debug/smartclock-web"), [
    "--listen", `127.0.0.1:${PORT}`, "--log-dir", logDir, "--run-dir", empty,
    "--sensor-log", existsSync(sensors) ? sensors : path.join(empty, "none.sqlite"),
    "--sensord", path.join(empty, "none.socket"),
  ], { stdio: "inherit" });
  const url = `http://127.0.0.1:${PORT}`;
  await until(url, () => true, 5000);
  return { url, servers: [server], strip: false };
}

// Wait up to `ms` for the web view at `url` to list receivers that
// `ready` accepts.
async function until(url, ready, ms) {
  for (const start = Date.now(); ; ) {
    try {
      if (ready(await (await fetch(`${url}/api/receivers`)).json())) return;
    } catch (e) {
      if (Date.now() - start > ms) throw e;
    }
    if (Date.now() - start > ms) throw new Error(`nothing ready at ${url}`);
    await new Promise((r) => setTimeout(r, 200));
  }
}

// The simulator, a daemon reading it and a web view of both.
async function simulate() {
  execFileSync("cargo", ["build", "-q", "-p", "smartclock-sim", "-p", "smartclockd", "-p", "smartclock-web"],
    { cwd: ROOT, stdio: "inherit" });
  const dir = mkdtempSync(path.join(tmpdir(), "screenshots-sim-"));
  const run = path.join(dir, "run");
  mkdirSync(path.join(run, "sim"), { recursive: true });
  const bin = (name) => path.join(ROOT, "target/debug", name);
  const sim = spawn(bin("smartclock-sim"), [`127.0.0.1:${SIM_PORT}`], { stdio: "inherit" });
  await new Promise((r) => setTimeout(r, 500));
  const daemon = spawn(bin("smartclockd"), ["--device", `tcp://127.0.0.1:${SIM_PORT}`,
    "--log-dir", dir, "--socket", path.join(run, "sim/socket")], { stdio: "inherit" });
  const web = spawn(bin("smartclock-web"), ["--listen", `127.0.0.1:${SIM_WEB_PORT}`,
    "--run-dir", run, "--log-dir", dir, "--sensor-log", path.join(dir, "none.sqlite"),
    "--sensord", path.join(dir, "none.socket")], { stdio: "inherit" });
  const url = `http://127.0.0.1:${SIM_WEB_PORT}`;
  await until(url, (list) => list.some((r) => r.instance), SIM_START);
  return { url, servers: [web, daemon, sim], strip: true };
}

async function shootStatus(browser, url) {
  const page = await browser.newPage({ viewport: VIEWPORT });
  await page.goto(`${url}/status`);
  await page.click("#read");
  await page.waitForSelector("#screen:not(.muted)", { timeout: SIM_START });
  await page.waitForTimeout(SETTLE);
  await page.screenshot({ path: path.join(OUT, "web-status.png") });
  console.log("web-status.png");
  await page.close();
}

async function web(browser, { url, strip }) {
  const receivers = await (await fetch(`${url}/api/receivers`)).json();
  const receiver = receivers.find((r) => r.serial === opts.receiver) ?? receivers[0];
  if (!receiver) throw new Error(`no receivers at ${url}`);
  const to = Math.min(...receivers.map((r) => Math.floor(Date.parse(r.last_seen) / 1000)));
  const range = (span) => `receiver=${receiver.serial}&from=${to - Number(span)}&to=${to}&refresh=off`;
  const page = await browser.newPage({ viewport: VIEWPORT });
  const shots = PAGES
    .filter(([file]) => !opts.only || opts.only.includes(file))
    .filter(([, where]) => where !== "/compare" || receivers.length > 1);
  for (const [file, where, drawn, panels] of shots) {
    const extra = where === "/correlation" ? await correlated(url, receivers, receiver) : "";
    const span = file.includes("stability") ? opts["stability-span"] ?? opts.span : opts.span;
    await page.goto(`${url}${where}?${range(span)}${extra ? `&${extra}` : ""}`);
    if (!strip) await page.addStyleTag({ content: "#status { display: none; }" });
    await page.waitForSelector(drawn);
    // The cursor partway across the first chart, so the readout under
    // the charts has values to show.
    const chart = await page.locator(drawn).first().boundingBox();
    if (chart) await page.mouse.move(chart.x + chart.width * 0.6, chart.y + chart.height / 2);
    await page.waitForTimeout(SETTLE);
    const clip = Array.isArray(panels) && await page.evaluate(([first, last]) => {
      const box = (sel) => document.querySelector(sel).closest(".panel").getBoundingClientRect();
      const [a, b] = [box(first), box(last)];
      return { x: a.x, y: a.y + scrollY, width: a.width, height: b.bottom - a.top };
    }, panels);
    await page.screenshot({ path: path.join(OUT, file), fullPage: panels === FULL, ...(clip ? { clip, fullPage: true } : {}) });
    console.log(file);
  }
  await page.close();
}

const browser = await chromium.launch();
const servers = [];
try {
  if (opts.url || opts["log-dir"]) {
    const site = opts.url
      ? { url: opts.url.replace(/\/$/, ""), servers: [], strip: true }
      : await serve(opts["log-dir"].map((d) => path.resolve(d)));
    servers.push(...site.servers);
    await web(browser, site);
  }
  if (opts.sim) {
    const site = await simulate();
    servers.push(...site.servers);
    await shootStatus(browser, site.url);
  }
} finally {
  await browser.close();
  for (const server of servers) server.kill();
}
