// Print each I/O block of an XC3000-family bitstream, as decoded by the
// viewer at https://github.com/Arisotura/xc3000 (read at 8563397).
// That viewer is browser code; this loads its decoder files into a node
// context with the few browser globals they touch, decodes the first
// bitstream in FILE against the PLCC84 pinout, and prints one line per
// pad: pin, pad, part, and the block's configuration
// (OUT/OUTQ output, unregistered or registered, with NOT and FAST;
// TRI tristate control; I and IQ inputs; PULLUP; "-" for none).
//
//     node tools/fpga/iobdump.js ~/src/xc3000/html third_party/firmware/fpga/z3805a.bit
//
// Not run in CI: it needs a clone of the viewer.  docs/firmware/outputs.md
// says what it showed.

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const [viewer, file] = process.argv.slice(2);
if (!viewer || !file) {
  console.error('usage: node iobdump.js VIEWER_HTML_DIR FILE.bit');
  process.exit(2);
}

// The viewer's files, in the order its index.html loads them, less the
// ones that only draw or emulate.
const SOURCES = ['chipfamily.js', 'chippackage.js', 'draw.js', 'parse.js', 'pip.js',
  'path.js', 'switch.js', 'tribuf.js', 'pullup.js', 'clock.js', 'iob.js', 'clb.js',
  'karnaugh.js', 'bitstreamDisplay.js'];

const context = {
  // The viewer logs as it parses; keep that off the report.
  console: { log: console.error, warn: console.error, error: console.error },
  Math, Uint8Array, Error, Object, Array, String, Number, JSON, parseInt,
  alert: (message) => { throw new Error(`viewer: ${message}`); },
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

const bytes = fs.readFileSync(file);
context.bitstreamBytes = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
vm.runInContext(`
  parseBitstream('bin', bitstreamBytes);
  if (!bitstreams.length) throw new Error('no bitstream found');
  selectBitstream(0, 'PLCC84');
  var lines = [];
  for (const [pad, iob] of Object.entries(iobDecoders.iobs)) {
    const parts = [];
    if (iob.oEnable) {
      parts.push('OUT' + (iob.oLatch ? 'Q' : '') + (iob.oInvert ? ':NOT' : '') + (iob.oFast ? ':FAST' : ''));
    }
    if (iob.tEnable) parts.push('TRI' + (iob.tInvert ? ':NOT' : ''));
    if (iob.iEnable) parts.push('I');
    if (iob.qEnable) parts.push('IQ' + (iob.iLatch ? ':LATCH' : ':FF'));
    if (iob.iPullup) parts.push('PULLUP');
    lines.push([iob.pin, pad, bitstreams[0].family.name, parts.join(' ') || '-'].join('\\t'));
  }
  report = lines.join('\\n');
`, context);
process.stdout.write(context.report + '\n');
