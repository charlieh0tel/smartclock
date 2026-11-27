// A fake of smartclock-emu's panel endpoints, installed on a Playwright
// page.
//
// The page, its script, its drawings, its font and /constants.json come
// from a real emulator, built by cargo; /state, /console, /key and
// /event are answered here, so a test decides what the run shows, who
// holds the host port, and when the run stops answering -- the states a
// running emulator cannot be made to show on demand.

// Where the emulator serving the page listens (playwright.config.mjs).
export const PANEL_PORT = 9983;

// The keys' names in the lamp byte's order, as the 58503A's state gives
// them.
export const KEYS = ["time", "long", "lat", "alt", "sat", "status", "serial", "shift"];

// A 58503A with Option 001, at a moment: GPS Lock lit, the display
// showing the time, Time's lamp lit, two of the board's LEDs.
export function snapshot() {
  return {
    drawing: "58503a-option-001",
    cycle: 123456789,
    seconds: 7.25,
    indicators: [
      { bank: "lamp", name: "gps_lock", lit: true },
      { bank: "lamp", name: "holdover", lit: false },
      { bank: "lamp", name: "alarm", lit: false },
      { bank: "led", name: "heartbeat", lit: true },
      { bank: "led", name: "selftest", lit: false },
    ],
    display: { lamps: 0x01, text: "UTC 01:34:43" },
    keys: KEYS,
    controls: { names: ["antenna-off", "antenna-on", "skip-warm-up", "power-cycle", "reset"], antenna: true, warming: true },
  };
}

// The console as this page holds it, its output so far `text`, in the
// run named `run`.
export function consoleHeld(text = "scpi > ", run = "first") {
  return { from: 0, offset: text.length, text, typist: "page", writable: true, reason: null, run };
}

// The state a test drives.
export class PanelFake {
  constructor() {
    // What /state answers.
    this.state = snapshot();
    // What /console answers a GET, before its `since` is applied: the
    // whole output from offset 0.
    this.console = consoleHeld();
    // Whether the run answers at all; a hung one never does.
    this.answering = true;
    this.hung = false;
    // The status a POST to /key, /event or /console is answered with,
    // and its text.
    this.refusal = null;
    // Every request seen, { method, path, query, body }, in order.
    this.seen = [];
  }

  // The requests seen to `path`, by `method`.
  requests(path, method = null) {
    return this.seen.filter((r) => r.path === path && (method === null || r.method === method));
  }

  async install(page) {
    await page.route(/\/(state|console|key|event)(\?|$)/, async (route) => {
      const request = route.request();
      const url = new URL(request.url());
      this.seen.push({ method: request.method(), path: url.pathname, query: url.searchParams, body: request.postData() });
      if (this.hung) return; // never answered
      if (!this.answering) return route.abort("connectionrefused");
      if (request.method() === "POST") {
        if (this.refusal) {
          return route.fulfill({ status: this.refusal.status, contentType: "text/plain", body: this.refusal.text });
        }
        return route.fulfill({ contentType: "text/plain", body: "queued\n" });
      }
      if (url.pathname === "/state") {
        return route.fulfill({ contentType: "application/json", body: JSON.stringify(this.state) });
      }
      // the console's output after `since`, as the run answers it
      const since = Number(url.searchParams.get("since") ?? 0);
      const { text, offset } = this.console;
      const from = Math.min(Math.max(since, offset - text.length), offset);
      const answer = { ...this.console, from, text: text.slice(from - (offset - text.length)) };
      return route.fulfill({ contentType: "application/json", body: JSON.stringify(answer) });
    });
  }
}
