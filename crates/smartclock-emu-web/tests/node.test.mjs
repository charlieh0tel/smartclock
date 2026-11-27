// The browser's module in Node (`make test-emulator-web`): the static
// build's bindings, through their Node entry, which instantiates the
// module a browser runs, boot the 58503A from the build's files to its
// prompt, and its console, asked through `respond` as the panel page
// asks, answers `*IDN?`.  The page's clock is the test's, going a frame's
// most at a time, so the board runs as fast as Node runs it.
// EMULATOR_WEB names the static build.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { pathToFileURL } from "node:url";

const ROOT = process.env.EMULATOR_WEB ?? assert.fail("EMULATOR_WEB names the static build");
const { Emulator } = await import(pathToFileURL(join(ROOT, "pkg/node.mjs")));

// The files the loader gives the run, at their paths in the build.
const IMAGE = "third_party/firmware/images/58503a-3704.bin";
const DUMP = "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin";
const FILES = ["boards/58503a.toml", IMAGE, DUMP];
// The AT28C64B's cells, bytes.
const EEPROM_BYTES = 8192;
// UTC as the run starts, milliseconds since 1970.
const START = Date.parse("2026-10-22T12:00:00Z");
// How far the test's clock goes a frame, milliseconds: the most a frame
// runs.
const FRAME_MS = 50;
// The most board time the test waits for anything, milliseconds.
const LIMIT_MS = 20_000;
const CLIENT = "node";

const files = () => new Map(FILES.map((path) => [path, new Uint8Array(readFileSync(join(ROOT, path)))]));

// The answer to `method` on `target`, which must be a 200: its body.
function ask(emulator, method, target, body) {
  const answer = emulator.respond(method, target, body);
  try {
    assert.equal(answer.status, 200, `${target}: ${answer.body}`);
    return answer.body;
  } finally {
    answer.free();
  }
}

const console_ = (emulator) => JSON.parse(ask(emulator, "GET", `/console?since=0&client=${CLIENT}`));

// Frames run, the clock `clock.wall` going FRAME_MS a frame, until the
// console has `text`.
function framesUntil(emulator, clock, text) {
  for (const end = clock.wall + LIMIT_MS; clock.wall < end; ) {
    clock.wall += FRAME_MS;
    const frame = emulator.frame(clock.wall);
    const running = frame.running;
    frame.free();
    assert.ok(running, "the run goes on");
    if (console_(emulator).text.includes(text)) return;
  }
  assert.fail(`no ${JSON.stringify(text)} on the console: ${console_(emulator).text}`);
}

test("the 58503A boots in Node and answers its identity through respond", () => {
  const emulator = new Emulator([IMAGE], files(), START);
  try {
    const clock = { wall: 0 };
    const started = performance.now();
    framesUntil(emulator, clock, "scpi > ");
    assert.equal(ask(emulator, "POST", `/console?client=${CLIENT}`, new TextEncoder().encode("*IDN?")), "queued\n");
    framesUntil(emulator, clock, "HEWLETT-PACKARD,58503A,3710A01056");
    const seconds = (performance.now() - started) / 1000;
    console.log(`board ${(clock.wall / 1000).toFixed(2)} s in ${seconds.toFixed(2)} s of Node's`);
    const kept = emulator.eeprom();
    try {
      assert.ok(kept.cells instanceof Uint8Array && kept.cells.length === EEPROM_BYTES, "the EEPROM's cells");
      assert.equal(kept.protected, false);
    } finally {
      kept.free();
    }
  } finally {
    emulator.free();
  }
});

test("the EEPROM kept starts the next run, its cells the image and its protection an argument", () => {
  const cells = new Uint8Array(16).fill(0x5a);
  const given = files();
  given.set(DUMP, cells);
  const emulator = new Emulator([IMAGE, "--eeprom-protected", "true"], given, START);
  try {
    const kept = emulator.eeprom();
    try {
      assert.deepEqual(kept.cells.subarray(0, cells.length), cells);
      assert.equal(kept.protected, true);
    } finally {
      kept.free();
    }
  } finally {
    emulator.free();
  }
});

test("what JavaScript passes is checked, and refused with a message", () => {
  assert.throws(() => new Emulator([IMAGE], 7, START), /neither a Map nor an object/);
  assert.throws(
    () => new Emulator([IMAGE], new Map([[IMAGE, [1, 2]]]), START),
    /is not a Uint8Array/,
  );
  assert.throws(() => new Emulator([IMAGE], new Map(), START), /not among the files given/);
  assert.throws(() => new Emulator(["--bogus"], files(), START), /--bogus/);
  assert.throws(() => new Emulator([IMAGE], files(), Number.NaN), /no UTC reading/);
  const emulator = new Emulator([IMAGE], files(), START);
  try {
    assert.throws(() => emulator.frame(-1), /no clock reading/);
    assert.throws(() => emulator.respond("POST", "/console", "a string"), /not a Uint8Array but a string/);
    const answer = emulator.respond("PUT", "/state");
    assert.equal(answer.status, 405);
    assert.equal(answer.allow, "GET, HEAD");
    answer.free();
  } finally {
    emulator.free();
  }
});
