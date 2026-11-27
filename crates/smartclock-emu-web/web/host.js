// The browser's build's host for the panel page (docs/emulator.md, "In
// a browser"), loaded before panel.js: it sets the smartclockAsk that
// panel.js's ask calls in place of fetch, each request answered by the
// run in the page once loader.mjs has started it.  The first request
// loads the loader; a loader that fails to start the run fails every
// request, which the page shows as a run not answering.
"use strict";

globalThis.smartclockAsk = (path, options) =>
  import("./loader.mjs").then((loader) => loader.ask(path, options));
