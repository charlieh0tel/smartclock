// Print each I/O block of an XC3000-family bitstream, as decoded by the
// viewer at https://github.com/Arisotura/xc3000 (read at 8563397).
// That viewer is browser code; this loads its decoder files into a node
// context with the few browser globals they touch, decodes the first
// bitstream in FILE against the PLCC84 pinout, and prints one line per
// pad: pin, pad, part, and the block's configuration
// (OUT/OUTQ output, unregistered or registered, with NOT and FAST;
// TRI tristate control; I and IQ inputs; PULLUP; "-" for none).
//
//     node tools/fpga/iobdump.js target/fpga/xc3000/html third_party/firmware/images/fpga/z3805a.bit
//
// Not run in CI: it needs a clone of the viewer.  docs/firmware/outputs.md
// says what it showed.

const vm = require('vm');
const { loadViewer, readBitstream } = require('./viewer.js');

const [viewer, file] = process.argv.slice(2);
if (!viewer || !file) {
  console.error('usage: node iobdump.js VIEWER_HTML_DIR FILE.bit');
  process.exit(2);
}

const context = loadViewer(viewer);
context.bitstreamBytes = readBitstream(file);
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
