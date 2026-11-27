// The emulator's browser build in a browser (`make test-emulator-web`):
// the static directory `make emulator-web` makes, EMULATOR_WEB, served
// as files (static-server.mjs), the 58503A run in the page.  Its own
// configuration, apart from playwright.config.mjs's servers, which need
// the native binaries built; Playwright's own Chromium as there.

import { defineConfig } from "@playwright/test";

const PORT = 9984;

export default defineConfig({
  testDir: ".",
  testMatch: "emulator-web.spec.mjs",
  timeout: 60000,
  use: { baseURL: `http://127.0.0.1:${PORT}` },
  webServer: {
    command: `node static-server.mjs ${PORT}`,
    url: `http://127.0.0.1:${PORT}/`,
    reuseExistingServer: false,
  },
});
