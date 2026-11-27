// smartclock-emu's panel page (docs/emulator.md, "The panel page"):
// its keys, its control strip, its console and its status line, against
// a faked run (panel-fake.mjs), and a few things against the running
// emulator itself.

import { expect, test } from "@playwright/test";

import { KEYS, PANEL_PORT, PanelFake, consoleHeld } from "./panel-fake.mjs";

test.use({ baseURL: `http://127.0.0.1:${PANEL_PORT}` });

// An exception the page does not catch fails the test it happened in.
const thrown = new WeakMap();
test.beforeEach(({ page }) => {
  thrown.set(page, []);
  page.on("pageerror", (e) => thrown.get(page).push(e.message));
});
test.afterEach(({ page }) => {
  expect(thrown.get(page), "uncaught exceptions in the page").toEqual([]);
});

// The page against `fake`, open and drawn.
async function open(page, fake = new PanelFake()) {
  await fake.install(page);
  await page.goto("/");
  await expect(page.locator("#link")).toHaveText("running");
  return fake;
}

// The key whose legend is `legend`, and its lamp, drawn just before it.
const keyOf = (page, legend) => page.locator(`#panel g.key[aria-label="${legend}"]`);
const lampOf = (page, legend) => keyOf(page, legend).locator("xpath=preceding-sibling::*[1]");

test.describe("against a faked run", () => {
  test("each key posts its name, clicked or from the keyboard", async ({ page }) => {
    const fake = await open(page);
    const legends = ["Time", "Long", "Lat", "Alt", "Sat", "Status", "Serial Port", "Shift"];
    for (const legend of legends) await keyOf(page, legend).click();
    await keyOf(page, "Sat").focus();
    await page.keyboard.press("Enter");
    await expect.poll(() => fake.requests("/key").length).toBe(legends.length + 1);
    const posted = fake.requests("/key");
    expect(posted.every((r) => r.method === "POST")).toBe(true);
    expect(posted.map((r) => r.query.get("name"))).toEqual([...KEYS, "sat"]);
  });

  test("a clicked key looks pressed, and its lamp follows the state", async ({ page }) => {
    const fake = await open(page);
    const time = keyOf(page, "Time");
    await time.click();
    await expect(time).toHaveClass(/pressed/);
    await expect(time).not.toHaveClass(/pressed/);
    // key n's lamp on bit n - 1
    await expect(lampOf(page, "Time")).toHaveClass("lamp on");
    await expect(lampOf(page, "Shift")).toHaveClass("lamp");
    fake.state = { ...fake.state, display: { ...fake.state.display, lamps: 0x80 } };
    await expect(lampOf(page, "Shift")).toHaveClass("lamp on");
    await expect(lampOf(page, "Time")).toHaveClass("lamp");
  });

  test("while the run is not answering, a key does nothing, and once it answers again it posts", async ({ page }) => {
    const fake = await open(page);
    fake.answering = false;
    await expect(page.locator("#link")).toHaveText("not answering");
    await expect(keyOf(page, "Time")).toHaveAttribute("aria-disabled", "true");
    // clicked as a hand clicks it, which aria-disabled does not stop
    await keyOf(page, "Time").click({ force: true });
    await keyOf(page, "Time").focus();
    await page.keyboard.press("Enter");
    await expect(keyOf(page, "Time")).not.toHaveClass(/pressed/);
    expect(fake.requests("/key")).toEqual([]);
    fake.answering = true;
    await expect(page.locator("#link")).toHaveText("running");
    await expect(keyOf(page, "Time")).toHaveAttribute("aria-disabled", "false");
    await keyOf(page, "Long").click();
    await expect.poll(() => fake.requests("/key").map((r) => r.query.get("name"))).toEqual(["long"]);
  });

  test("a key the state does not name posts nothing, no name made up from its legend", async ({ page }) => {
    const fake = new PanelFake();
    fake.state = { ...fake.state, keys: KEYS.slice(0, 6) };
    await open(page, fake);
    await keyOf(page, "Serial Port").click();
    await keyOf(page, "Status").click();
    await expect.poll(() => fake.requests("/key").map((r) => r.query.get("name"))).toEqual(["status"]);
  });

  test("the antenna's button disconnects and reconnects it", async ({ page }) => {
    const fake = await open(page);
    const antenna = page.locator("#antenna");
    await expect(antenna).toHaveText("disconnect antenna");
    await expect(antenna).toHaveAttribute("aria-pressed", "false");
    await antenna.click();
    await expect.poll(() => fake.requests("/event").map((r) => r.query.get("name"))).toEqual(["antenna-off"]);
    fake.state = { ...fake.state, controls: { ...fake.state.controls, antenna: false } };
    await expect(antenna).toHaveText("reconnect antenna");
    await expect(antenna).toHaveAttribute("aria-pressed", "true");
    await antenna.click();
    await expect.poll(() => fake.requests("/event").map((r) => r.query.get("name")))
      .toEqual(["antenna-off", "antenna-on"]);
    expect(fake.requests("/event").every((r) => r.method === "POST")).toBe(true);
  });

  test("the warm-up can be skipped while warming, and not once warm", async ({ page }) => {
    const fake = await open(page);
    const skip = page.locator("#skip-warm-up");
    await expect(skip).toHaveText("skip warm-up");
    await expect(skip).toBeEnabled();
    await skip.click();
    await expect.poll(() => fake.requests("/event").map((r) => r.query.get("name"))).toEqual(["skip-warm-up"]);
    fake.state = { ...fake.state, controls: { ...fake.state.controls, warming: false } };
    await expect(skip).toHaveText("warmed up");
    await expect(skip).toBeDisabled();
  });

  test("the power cycle and the reset post theirs", async ({ page }) => {
    const fake = await open(page);
    await page.locator("#power-cycle").click();
    await page.locator("#reset").click();
    await expect.poll(() => fake.requests("/event").map((r) => r.query.get("name")))
      .toEqual(["power-cycle", "reset"]);
  });

  test("a control that does not apply is not offered", async ({ page }) => {
    const fake = new PanelFake();
    fake.state = { ...fake.state, controls: { names: ["skip-warm-up", "power-cycle", "reset"], antenna: null, warming: true } };
    await open(page, fake);
    await expect(page.locator("#antenna")).toBeHidden();
    await expect(page.locator("#skip-warm-up")).toBeVisible();
    fake.state = { ...fake.state, controls: { names: ["power-cycle", "reset"], antenna: null, warming: null } };
    await expect(page.locator("#skip-warm-up")).toBeHidden();
    await expect(page.locator("#power-cycle")).toBeVisible();
    await expect(page.locator("#reset")).toBeVisible();
  });

  test("the status line shows the run's cycle, its time and the board's LEDs", async ({ page }) => {
    const fake = await open(page);
    await expect(page.locator("#cycle")).toHaveText("123,456,789");
    await expect(page.locator("#seconds")).toHaveText("7.250");
    await expect(page.locator("#board-leds span")).toHaveText(["heartbeat", "selftest"]);
    await expect(page.locator("#board-leds span.lit")).toHaveText(["heartbeat"]);
    fake.state = { ...fake.state, cycle: 1234567890, seconds: 75.0625 };
    await expect(page.locator("#cycle")).toHaveText("1,234,567,890");
    await expect(page.locator("#seconds")).toHaveText("75.063");
  });

  test("a run that stops answering puts the panel out and says so", async ({ page }) => {
    const fake = await open(page);
    await expect(page.locator("#panel .lamp.on")).not.toHaveCount(0);
    fake.answering = false;
    await expect(page.locator("#link")).toHaveText("not answering");
    await expect(page.locator("#link")).toHaveClass("down");
    await expect(page.locator("#panel .lamp.on")).toHaveCount(0);
    await expect(page.locator(".controls button:visible")).toHaveCount(4);
    for (const button of await page.locator(".controls button").all()) await expect(button).toBeDisabled();
    fake.answering = true;
    await expect(page.locator("#link")).toHaveText("running");
  });

  test("a run that hangs, answering nothing, is shown not answering, and running once it answers", async ({ page }) => {
    const fake = await open(page);
    await expect(page.locator("#console-who")).toHaveText("SCPI port: yours");
    fake.hung = true;
    await expect(page.locator("#link")).toHaveText("not answering", { timeout: 10000 });
    await expect(page.locator("#link")).toHaveClass("down");
    await expect(page.locator("#panel .lamp.on")).toHaveCount(0);
    await expect(page.locator("#console-who")).toHaveText("host port: not answering");
    await expect(page.locator("#console-input")).toBeDisabled();
    fake.hung = false;
    await expect(page.locator("#link")).toHaveText("running", { timeout: 10000 });
    await expect(page.locator("#console-who")).toHaveText("SCPI port: yours", { timeout: 10000 });
  });

  test("the page holding the port may type, and its quick lines post", async ({ page }) => {
    const fake = await open(page);
    await expect(page.locator("#console-who")).toHaveText("SCPI port: yours");
    await expect(page.locator("#console-output")).toHaveText("scpi > ");
    const input = page.locator("#console-input");
    await expect(input).toBeEnabled();
    await input.fill("*IDN?");
    await input.press("Enter");
    await expect(input).toHaveValue("");
    const quick = page.locator("#console-quick button");
    await expect(quick).toHaveText(["*IDN?", ":SYSTem:STATus?", ":SYSTem:ERRor?", "pForth", "halt"]);
    await quick.filter({ hasText: "pForth" }).click();
    await quick.filter({ hasText: "halt" }).click();
    await expect.poll(() => fake.requests("/console", "POST").map((r) => r.body))
      .toEqual(["*IDN?", ':SYSTem:LANGuage "PFORTH"', "halt"]);
    const client = fake.requests("/console", "GET")[0].query.get("client");
    expect(client).toBeTruthy();
    expect(fake.requests("/console", "POST").every((r) => r.query.get("client") === client)).toBe(true);
  });

  test("a line refused stays refused long enough to read, then the console says who has the port", async ({ page }) => {
    const fake = await open(page);
    const who = page.locator("#console-who");
    await expect(who).toHaveText("SCPI port: yours");
    fake.refusal = { status: 409, text: "another page holds the host port\n" };
    await page.locator("#console-input").fill("*IDN?");
    await page.locator("#console-input").press("Enter");
    const refused = "host port: refused, another page holds the host port";
    await expect(who).toHaveText(refused);
    // many polls later, still there
    const polls = fake.requests("/console", "GET").length;
    await page.waitForTimeout(1500);
    expect(fake.requests("/console", "GET").length).toBeGreaterThan(polls + 3);
    await expect(who).toHaveText(refused);
    await expect(page.locator("#console-input")).toHaveValue("*IDN?");
    await expect(who).toHaveText("SCPI port: yours", { timeout: 10000 });
  });

  test("a line past the limit is not cut short and sent, but refused and kept", async ({ page, request }) => {
    const { line_limit: limit } = await (await request.get("/constants.json")).json();
    const fake = await open(page);
    const input = page.locator("#console-input");
    await expect(input).toBeEnabled();
    const long = ":SYSTem:STATus?" + " ".repeat(limit);
    await input.fill(long);
    await expect(input).toHaveValue(long);
    await input.press("Enter");
    await expect(page.locator("#console-who")).toHaveText(`host port: refused, a line is at most ${limit} bytes`);
    await expect(input).toHaveValue(long);
    // the limit is in bytes, as the run counts them
    const wide = "\u00e9".repeat(limit / 2 + 1);
    await input.fill(wide);
    await input.press("Enter");
    const fits = "x".repeat(limit);
    await input.fill(fits);
    await input.press("Enter");
    await expect(input).toHaveValue("");
    expect(fake.requests("/console", "POST").map((r) => r.body)).toEqual([fits]);
  });

  test("the port's output is appended as it comes, its bytes shown", async ({ page }) => {
    const fake = await open(page);
    await expect(page.locator("#console-output")).toHaveText("scpi > ");
    fake.console = consoleHeld("scpi > *IDN?\r\nHEWLETT-PACKARD,58503A\r\n\x07scpi > ");
    await expect(page.locator("#console-output")).toHaveText("scpi > *IDN?\nHEWLETT-PACKARD,58503A\n\\x07scpi > ");
    await expect(page.locator("#console-output .byte")).toHaveText(["\\x07"]);
    const since = fake.requests("/console", "GET").map((r) => Number(r.query.get("since")));
    expect(since[0]).toBe(0);
    expect(Math.max(...since)).toBeGreaterThan(0);
  });

  test("an emulator started again is read from its start, not from the offset of the run before", async ({ page }) => {
    const fake = await open(page);
    const output = page.locator("#console-output");
    fake.console = consoleHeld("scpi > *IDN?\r\nHEWLETT-PACKARD,58503A\r\nscpi > ");
    await expect(output).toHaveText("scpi > *IDN?\nHEWLETT-PACKARD,58503A\nscpi > ");
    const before = fake.seen.length;
    // the new run has sent less than the old one had
    fake.console = consoleHeld("HP 58503A\r\nscpi > ", "second");
    await expect(output).toHaveText(
      "scpi > *IDN?\nHEWLETT-PACKARD,58503A\nscpi > \n[the emulator started again]\nHP 58503A\nscpi > ",
    );
    const asked = fake.seen.slice(before).filter((r) => r.path === "/console" && r.method === "GET");
    expect(asked.some((r) => r.query.get("since") === "0")).toBe(true);
  });

  test("past what it keeps, the pane drops its oldest output and keeps the newest", async ({ page, request }) => {
    const { kept } = await (await request.get("/constants.json")).json();
    const fake = await open(page);
    await expect(page.locator("#console-output")).toHaveText("scpi > ");
    // one answer longer than the pane keeps, arriving as one piece
    const tail = "the newest line\r\nscpi > ";
    fake.console = consoleHeld("scpi > " + "x".repeat(kept) + tail);
    const shown = () => page.locator("#console-output").evaluate((pre) => pre.textContent);
    await expect.poll(async () => (await shown()).length).toBe(kept);
    expect(await shown()).toMatch(/x+the newest line\nscpi > $/);
    // and again, piece by piece
    fake.console = consoleHeld(fake.console.text + "\x07" + "y".repeat(100));
    await expect.poll(async () => (await shown()).endsWith("\\x07" + "y".repeat(100))).toBe(true);
    expect((await shown()).length).toBe(kept);
  });

  for (const [typist, reason, said] of [
    ["tcp", "a TCP client holds the host port", "a TCP client has it"],
    ["page", "another page holds the host port", "another page has it"],
    ["script", "a script holds the host port", "a script has it"],
  ]) {
    test(`with ${typist} at the port the console is read-only`, async ({ page }) => {
      const fake = new PanelFake();
      fake.console = { ...consoleHeld(), typist, writable: false, reason };
      await open(page, fake);
      await expect(page.locator("#console-who")).toHaveText(`SCPI port: read-only, ${said}`);
      await expect(page.locator("#console-input")).toBeDisabled();
      await expect(page.locator("#console-input")).toHaveAttribute("placeholder", reason);
      for (const button of await page.locator("#console-quick button").all()) await expect(button).toBeDisabled();
      // the lease back
      fake.console = consoleHeld();
      await expect(page.locator("#console-who")).toHaveText("SCPI port: yours");
      await expect(page.locator("#console-input")).toBeEnabled();
    });
  }
});

// The running emulator, one test at a time: a page holding its console
// keeps the lease for seconds after it closes.
test.describe("against the running emulator", () => {
  test.describe.configure({ mode: "serial" });

  test("the 58503A is drawn and its run goes on", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("#link")).toHaveText("running");
    await expect(page.locator("#panel g.key")).toHaveCount(KEYS.length);
    await expect(page.locator("#panel g[filter='url(#glow)']")).toHaveCount(12);
    const cycle = async () => Number((await page.locator("#cycle").textContent()).replaceAll(",", ""));
    const first = await cycle();
    await expect.poll(cycle).toBeGreaterThan(first);
  });

  test("a line typed on the console is answered", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("#console-who")).toHaveText("SCPI port: yours", { timeout: 15000 });
    await page.locator("#console-input").fill("*IDN?");
    await page.locator("#console-input").press("Enter");
    await expect(page.locator("#console-output")).toContainText("HEWLETT-PACKARD,58503A", { timeout: 30000 });
  });

  test("a key clicked is taken by the run", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("#link")).toHaveText("running");
    const answered = page.waitForResponse((r) => r.url().includes("/key?name=status"));
    await keyOf(page, "Status").click();
    expect((await answered).status()).toBe(200);
  });
});
