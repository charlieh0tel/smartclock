// A static server for the emulator's browser build in its Playwright
// tests (emulator-web.config.mjs): the directory EMULATOR_WEB names,
// on 127.0.0.1 at the port given, each file with its media type, a
// module's and the WebAssembly's among them, as a browser requires.
//
// usage: node static-server.mjs PORT

import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { extname, join, normalize, sep } from "node:path";

const ROOT = process.env.EMULATOR_WEB;
const PORT = Number(process.argv[2]);
if (!ROOT || !Number.isInteger(PORT)) {
  console.error("usage: EMULATOR_WEB=DIR node static-server.mjs PORT");
  process.exit(2);
}

// The media type of each kind of file the build has; any other is bytes.
const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".wasm": "application/wasm",
  ".toml": "text/plain; charset=utf-8",
};
const BYTES = "application/octet-stream";

createServer(async (request, response) => {
  const path = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
  const file = normalize(join(ROOT, path.endsWith("/") ? `${path}index.html` : path));
  if (file !== ROOT && !file.startsWith(ROOT + sep)) {
    response.writeHead(403).end();
    return;
  }
  try {
    const body = await readFile(file);
    response.writeHead(200, { "Content-Type": TYPES[extname(file)] ?? BYTES, "Cache-Control": "no-store" });
    response.end(body);
  } catch {
    response.writeHead(404).end();
  }
}).listen(PORT, "127.0.0.1");
