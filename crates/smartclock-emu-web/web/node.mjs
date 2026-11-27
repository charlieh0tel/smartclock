// The bindings' entry for Node: wasm-bindgen's web bindings with their
// module instantiated here, from the file beside them, as Node's fetch
// cannot read a file, so that Node's tests run the module a browser
// runs (as mc683xx's npm package's node.js does).  `make emulator-web`
// puts it in pkg/ beside the bindings.

import { readFileSync } from "node:fs";
import { initSync } from "./smartclock_emu_web.js";

initSync({ module: readFileSync(new URL("./smartclock_emu_web_bg.wasm", import.meta.url)) });

export * from "./smartclock_emu_web.js";
