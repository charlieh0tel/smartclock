// The LCA model's speed in the browser's module (`make bench-lca`;
// docs/firmware/fpga-codegen.md, "Performance"): the Z3805A 3543B booted
// in Node, on the hand model and then with `--fpga-design lca`, its
// files read from the repository, the page's clock the script's, a frame
// at a time.  Each run's time for the board's seconds from just past the
// FPGA's configuration, at about 1.07 s, is printed, on the wall clock
// and of Node's CPU, which a loaded host moves less: a measure, not a
// check.  EMULATOR_WEB names the static build, REPOSITORY the checkout.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const BUILD = process.env.EMULATOR_WEB ?? assert.fail("EMULATOR_WEB names the static build");
const REPOSITORY = process.env.REPOSITORY ?? assert.fail("REPOSITORY names the checkout");
const { Emulator } = await import(pathToFileURL(join(BUILD, "pkg/node.mjs")));

const IMAGE = "third_party/firmware/images/z3805a-3543b.bin";
const DUMP = "third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin";
const FILES = ["boards/z3805a.toml", IMAGE, DUMP];
// UTC as the run starts, milliseconds since 1970.
const START = Date.parse("2026-10-22T12:00:00Z");
// How far the clock goes a frame, milliseconds: the most a frame runs,
// so the board's time is the clock's.
const FRAME_MS = 50;
// The board's time the measure starts at, past the configuration, and
// how long it runs, milliseconds.
const CONFIGURED_MS = 1100;
const MEASURED_MS = 10000;

const files = () => new Map(FILES.map((path) => [path, new Uint8Array(readFileSync(join(REPOSITORY, path)))]));

// Frames run to the board's time `until`, milliseconds.
function runTo(emulator, clock, until) {
  while (clock.wall < until) {
    clock.wall += FRAME_MS;
    const frame = emulator.frame(clock.wall);
    const running = frame.running;
    frame.free();
    assert.ok(running, "the run goes on");
  }
}

// Milliseconds a second, and microseconds.
const MS_PER_SECOND = 1000;
const US_PER_SECOND = 1e6;

// The seconds the measured board time takes with `args`, on the wall
// clock and of the CPU, user and system.
function measure(args) {
  const emulator = new Emulator([IMAGE, ...args], files(), START);
  try {
    const clock = { wall: 0 };
    runTo(emulator, clock, CONFIGURED_MS);
    const started = performance.now();
    const cpu = process.cpuUsage();
    runTo(emulator, clock, CONFIGURED_MS + MEASURED_MS);
    const used = process.cpuUsage(cpu);
    return {
      wall: (performance.now() - started) / MS_PER_SECOND,
      cpu: (used.user + used.system) / US_PER_SECOND,
    };
  } finally {
    emulator.free();
  }
}

const board = MEASURED_MS / MS_PER_SECOND;
const hand = measure([]);
const lca = measure(["--fpga-design", "lca"]);
for (const what of ["wall", "cpu"]) {
  console.log(
    `wasm, ${what === "wall" ? "the wall clock" : "the CPU"}, the board's ${board.toFixed(3)} s from ` +
      `${(CONFIGURED_MS / MS_PER_SECOND).toFixed(2)} s: the hand model ${hand[what].toFixed(3)} s, ` +
      `the LCA model ${lca[what].toFixed(3)} s, ${(lca[what] / board).toFixed(1)} times slower than real time, ` +
      `${(lca[what] / hand[what]).toFixed(0)} times slower than the hand model`,
  );
}
