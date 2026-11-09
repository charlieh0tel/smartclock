// Loads the decoder of the XC3000 viewer at
// https://github.com/Arisotura/xc3000 (read at 8563397) into a node
// context, for iobdump.js and netdump.js.  The viewer has no license, so
// none of its code is copied here: it is read from the caller's clone.
// It is browser code; the context gives it the few browser globals it
// touches.

const fs = require('fs');
const path = require('path');
const vm = require('vm');

// The viewer's files, in the order its index.html loads them, less the
// ones that only draw or emulate.
const SOURCES = ['chipfamily.js', 'chippackage.js', 'draw.js', 'parse.js', 'pip.js',
  'path.js', 'switch.js', 'tribuf.js', 'pullup.js', 'clock.js', 'iob.js', 'clb.js',
  'karnaugh.js', 'bitstreamDisplay.js'];

// A context holding the viewer's decoder.  Its messages go to `log`, an
// array, as well as to stderr, so they stay off a report on stdout.
function loadViewer(viewer, log = []) {
  const message = (...parts) => {
    log.push(parts.join(' '));
    console.error(...parts);
  };
  const context = {
    console: { log: message, warn: message, error: message },
    Math, Uint8Array, Error, Object, Array, String, Number, JSON, Map, Set, parseInt,
    alert: (text) => { throw new Error(`viewer: ${text}`); },
    // jQuery is only used for page controls, none of which decoding reaches.
    $: () => ({ on() {}, html() { return this; }, val() {}, each() {}, ready() {} }),
    document: {},
  };
  // Browser code reads globals through `self` and `window`.
  context.self = context;
  context.window = context;
  vm.createContext(context);
  for (const source of SOURCES) {
    vm.runInContext(fs.readFileSync(path.join(viewer, source), 'utf8'), context, { filename: source });
  }
  return context;
}

// FILE's bytes as an ArrayBuffer, as the viewer's parser takes them.
function readBitstream(file) {
  const bytes = fs.readFileSync(file);
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
}

module.exports = { loadViewer, readBitstream };
