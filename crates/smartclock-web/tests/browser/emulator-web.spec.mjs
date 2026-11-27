// The emulator's browser build (docs/emulator.md, "In a browser"): the
// 58503A's panel page from the static build, its run in the page, boots
// to "HP 58503A" and answers *IDN? on its console, with no server but
// the files'.

import { expect, test } from "@playwright/test";

import { KEYS } from "./panel-fake.mjs";

// An exception the page does not catch fails the test it happened in.
const thrown = new WeakMap();
test.beforeEach(({ page }) => {
  thrown.set(page, []);
  page.on("pageerror", (e) => thrown.get(page).push(e.message));
});
test.afterEach(({ page }) => {
  expect(thrown.get(page), "uncaught exceptions in the page").toEqual([]);
});

// The display's text as the run gives the page.
const display = (page) =>
  page.evaluate(async () => (await (await globalThis.smartclockAsk("/state")).json()).display?.text ?? "");

test("the 58503A boots in the page, drawn, to its banner, and its run goes on", async ({ page }) => {
  await page.goto("/");
  await expect(page.locator("#link")).toHaveText("running", { timeout: 30000 });
  await expect(page.locator("#panel g.key")).toHaveCount(KEYS.length);
  await expect.poll(() => display(page), { timeout: 30000 }).toContain("HP 58503A");
  const cycle = async () => Number((await page.locator("#cycle").textContent()).replaceAll(",", ""));
  const first = await cycle();
  await expect.poll(cycle).toBeGreaterThan(first);
  // what it started from, until its EEPROM is first kept
  await expect(page.locator("#web-status")).toHaveText(/started from|EEPROM kept/);
});

test("a line typed on the console is answered", async ({ page }) => {
  await page.goto("/");
  await expect(page.locator("#console-who")).toHaveText("SCPI port: yours", { timeout: 30000 });
  // the run starts with the page, and a line typed before the firmware
  // reads its port is lost, as on the receiver
  await expect(page.locator("#console-output")).toContainText("scpi > ", { timeout: 30000 });
  await page.locator("#console-input").fill("*IDN?");
  await page.locator("#console-input").press("Enter");
  await expect(page.locator("#console-output")).toContainText("HEWLETT-PACKARD,58503A", { timeout: 30000 });
});

test("the EEPROM is kept in the browser, and the next visit starts from it", async ({ page }) => {
  await page.goto("/");
  await expect(page.locator("#web-status")).toHaveText("started from the bench unit's EEPROM", { timeout: 30000 });
  // as when the page is left
  await page.evaluate(() => dispatchEvent(new Event("pagehide")));
  await expect(page.locator("#web-status")).toContainText("EEPROM kept in this browser");
  await page.reload();
  await expect(page.locator("#web-status")).toHaveText("started from the EEPROM this browser kept", {
    timeout: 30000,
  });
  await page.locator("#web-forget").click();
  await page.reload();
  await expect(page.locator("#web-status")).toHaveText("started from the bench unit's EEPROM", { timeout: 30000 });
});

test.describe("given this browser's location", () => {
  test.use({ geolocation: { latitude: 37.4219, longitude: -122.0841 }, permissions: ["geolocation"] });

  test("the antenna is moved there only when asked, the run started again", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("#link")).toHaveText("running", { timeout: 30000 });
    await page.locator("#web-place").click();
    await expect(page.locator("#web-status")).toHaveText(
      "antenna at 37.4219, -122.0841: the run started again there",
    );
    await expect(page.locator("#console-output")).toContainText("[the emulator started again]", { timeout: 30000 });
  });
});
