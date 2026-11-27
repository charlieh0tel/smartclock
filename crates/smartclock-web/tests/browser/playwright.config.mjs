// Browser tests for smartclock-web's pages and smartclock-emu's panel
// page.
//
// The pages come from the real servers, built by cargo; their APIs are
// faked per test (fake.mjs, panel-fake.mjs).  The web view is started
// with empty directories and never reaches a daemon or a log.  The panel
// page is served by a running emulator, the 58503A's image from its
// bench unit's EEPROM, paced, which panel.spec.mjs's live tests talk to
// as well.  Playwright's own Chromium is in node_modules
// (PLAYWRIGHT_BROWSERS_PATH=0), not the home directory.

import { defineConfig } from "@playwright/test";

import { PANEL_PORT } from "./panel-fake.mjs";

const PORT = 9982;
const ROOT = "../../../..";
const IMAGES = `${ROOT}/third_party/firmware/images`;

export default defineConfig({
  testDir: ".",
  testMatch: "*.spec.mjs",
  // the emulator's browser build has its own (emulator-web.config.mjs)
  testIgnore: "emulator-web.spec.mjs",
  // The tests wait on the pages' own timers, a second for the strip and
  // ten for the receiver list, so they run side by side.
  fullyParallel: true,
  timeout: 60000,
  use: {
    baseURL: `http://127.0.0.1:${PORT}`,
    // Scrollbars that take width, as desktop browsers draw them, where
    // headless Chromium would hide them.
    launchOptions: { ignoreDefaultArgs: ["--hide-scrollbars"] },
  },
  webServer: [
    {
      command:
        `sh -c 'd=$(mktemp -d) && exec ${ROOT}/target/debug/smartclock-web ` +
        `--listen 127.0.0.1:${PORT} --run-dir "$d" --log-dir "$d"'`,
      url: `http://127.0.0.1:${PORT}/`,
      reuseExistingServer: false,
    },
    {
      command:
        `sh -c 'd=$(mktemp -d) && exec ${ROOT}/target/debug/smartclock-emu ${IMAGES}/58503a-3704.bin ` +
        `--eeprom "$d/eeprom.bin" --eeprom-dump ${IMAGES}/eeprom/58503a-3710A01056-eeprom.bin ` +
        `--panel 127.0.0.1:${PANEL_PORT}'`,
      url: `http://127.0.0.1:${PANEL_PORT}/`,
      reuseExistingServer: false,
      // the emulator's trace, which is every lamp and every display
      // change, is not the tests' business
      stderr: "ignore",
    },
  ],
});
