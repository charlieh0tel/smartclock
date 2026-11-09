// Write an XC3000-family bitstream's design as JSON: its logic blocks,
// I/O blocks, clock buffers and lines, 3-state buffers, pull-ups and
// nets, as decoded by the viewer at https://github.com/Arisotura/xc3000
// (read at tools/fpga/decoder.mk's pin, which the JSON records as the
// clone's commit).  netlist.py turns the JSON into Verilog.
//
//     node tools/fpga/netdump.js target/fpga/xc3000/html third_party/firmware/images/fpga/z3805a.bit > z3805a.json
//
// Without node, netdump.html runs the same code in a browser
// (tools/fpga/netdump.sh drives headless Chrome).
//
// The viewer has no license, so none of its code, formulas or tables is
// copied here: this reads the state its decoder leaves behind, with the
// viewer's own names for blocks (CLB "AB", I/O block "PAD7") and pins
// ("AB.X", "PAD7.I"), so that the JSON can be checked against the
// viewer's drawing, and corrects it where XACT's MakeBits shows it wrong
// (installCorrections), reading the few bits that takes itself.  CI runs
// it on XACT's test designs (make check-fpga-decoder, which clones the
// viewer).  docs/firmware/outputs.md, "The FPGA bitstreams", says what it
// showed.

// Decodes BYTES (an ArrayBuffer) against the package PACKAGE_NAME and
// returns the design.  Runs in the viewer's global scope, where its
// classes and decoders are; LOG is the array its messages go to.
function xc3000Netlist(bytes, packageName, log) {
  const restore = installCorrections();
  try {
    parseBitstream('bin', bytes);
    if (!bitstreams.length) throw new Error('no bitstream found');
    selectBitstream(0, packageName);
    return collect(restore);
  } finally {
    restore();
  }

  // The PIP by which die-edge clock line NAME takes its KIND of input,
  // 'branch' or 'direct' (its clock buffer's output): the line's PIP whose
  // other path is, or is not, the line's branch.
  function clockLinePip(name, kind) {
    const branch = clockDecoders.clockInputs[name];
    return Object.values(clockDecoders.clockLines[name].pathById).find((element) => element.type === 'pip'
      && Object.values(element.obj.paths).some((end) => end.path.rootPath === branch) === (kind === 'branch'));
  }

  // Changes to the viewer for one decode; returns the function that undoes
  // them, which also carries the traces that hit the viewer's recursion
  // limit (`truncated`, by source pin): their nets may be missing branches;
  // and each logic block's configured EC and RD (`configured`).
  //
  // The viewer decodes each block before it knows which of its outputs are
  // used.  When a logic block's flip-flop turns out to feed only an output
  // (its X or Y enabled by a block decoded after it), the block has by then
  // cleared its K, EC, RD and DI routing and its EC and RD enables, and its
  // clock comes out untraced.  So:
  // - every input keeps the routing its configuration selects
  //   (Path.disableAllPips does nothing): a trace connects only the pins
  //   enabled when it runs, so an unused input still connects nothing;
  // - each logic block keeps its configured EC and RD, the first values its
  //   decoding gives them, and has them back before it rechecks its storage
  //   when an output is enabled (ClbDecoder.signalConnection).
  function installCorrections() {
    const undo = [];
    function override(target, name, descriptor) {
      const own = Object.getOwnPropertyDescriptor(target, name);
      undo.push(() => (own ? Object.defineProperty(target, name, own) : delete target[name]));
      Object.defineProperty(target, name, { configurable: true, ...descriptor });
    }

    override(Path.prototype, 'disableAllPips', { writable: true, value() {} });

    // A vertical local line reaches a horizontal Longline through the
    // 3-state buffer beside it, which, with no 3-state control, is a plain
    // PIP.  The viewer enables that PIP on the buffer's control-select bit
    // alone.  On the XC3000A and XC3100A the buffer's input-select bit
    // also chooses between that local line (1) and the neighboring logic
    // block's X or Y output (0), and the viewer connects the output when
    // it is 0, so both drove the Longline.  The local line's PIP needs the
    // input select at 1 as well, which every XC3000 bitstream has (the
    // viewer tells the families apart by it).  XACT's routed LCAs say so:
    // tools/fpga/xact/designs and the XAPP003 designs.
    // A gated PIP's control-select column in its tile -> the same buffer's
    // input-select column (TriBuf.decode reads both).
    const TBUF_INPUT_SELECT = { 4: 3, 8: 13 };
    const pipDecode = PipDecoder.prototype.decode;
    override(PipDecoder.prototype, 'decode', {
      writable: true,
      value() {
        pipDecode.call(this);
        if (!curBitstream.family.extraInter) return;
        for (const [key, data] of Object.entries(this.pipData)) {
          const [col, row, dy, dx, gate] = data;
          const inputSelect = TBUF_INPUT_SELECT[dx];
          if (gate !== 'TBUF' || inputSelect === undefined || !this.entries[key].status) continue;
          const offset = getTileOffset(col, row);
          // The viewer's addressing of a PIP's bits: a negative row
          // offset counts back from the end of the tile above.
          const y = Number(dy) < 0 && row > 0 ? getTileOffset(col, row - 1).y + 8 + Number(dy) : offset.y + Number(dy);
          if (curBitstream.data[y][offset.x + inputSelect] === 0) this.entries[key].status = 0;
        }
      },
    });

    // An upper I/O block on the left edge, below its top row, clocks its
    // output flip-flop from the die-edge line (col.A.local.12 or .13) that
    // bit 1 of its tile's first row selects: MakeBits sets that bit, and
    // only it, between the two (tools/fpga/xact/designs/clocks.lca).
    // The viewer reads bit 0 of that row, which is one of the block's
    // output multiplexer bits, so the clock it reported was the other line
    // whenever that multiplexer's bit differed.
    const LEFT_UPPER_OK_COLUMN = 1;
    const iobDecode = Iob.prototype.decode;
    override(Iob.prototype, 'decode', {
      writable: true,
      value() {
        const result = iobDecode.call(this);
        if (this.style === 'leftupper' && this.row !== 0) {
          const select = curBitstream.data[getTileOffset(this.col, this.row).y][LEFT_UPPER_OK_COLUMN];
          // The viewer's choice i is the line the bit's value i selects.
          for (const element of Object.values(this.okPath.pathById)) element.obj.status = 0;
          this.okPath.setPipStatus(select, 1);
          this.okSel = select;
        }
        return result;
      },
    });

    // A die-edge clock line takes either its clock buffer's output (the
    // direct input) or one of its branch's inputs.  The viewer's decoding
    // enables the line's PIP 1 for the direct input and PIP 0 for the
    // branch, but on the left edge's lower line (col.A.local.13) PIP 0 is
    // the one from the alternate clock buffer and PIP 1 the branch's, so
    // that line came out on the branch, which had no input selected,
    // whenever it was on its buffer.  The line's PIPs are told apart by
    // what they connect to, and its two statuses swapped where the
    // numbering is the other way about.
    const DIRECT_PIP = 1;
    const BRANCH_PIP = 0;
    const clockDecode = ClockDecoders.prototype.decode;
    override(ClockDecoders.prototype, 'decode', {
      writable: true,
      value() {
        const result = clockDecode.call(this);
        for (const name of Object.keys(this.clockLines)) {
          const line = this.clockLines[name];
          if (clockLinePip(name, 'branch') !== line.pathById[DIRECT_PIP]) continue;
          const direct = line.pathById[DIRECT_PIP].obj;
          const branch = line.pathById[BRANCH_PIP].obj;
          [direct.status, branch.status] = [branch.status, direct.status];
        }
        // The viewer counts a clock buffer used only when it drives a
        // Longline, so one that clocks only I/O blocks, through the
        // die-edge lines, was dropped with its input and output nets.
        // One whose output reaches an enabled pin is used.
        for (const clock of Object.values(this.clocks)) {
          if (clock.enabled || !clock.iPath) continue;
          if (!clock.oPath.traceFrom().isEmpty()) clock.enabled = true;
        }
        return result;
      },
    });

    const state = new Map(); // logic block -> its EC and RD enables
    const configured = new Map(); // logic block -> the values its decoding set first
    let decoding = null;
    for (const name of ['ecEnable', 'rdEnable']) {
      override(ClbDecoder.prototype, name, {
        get() { return (state.get(this) || {})[name]; },
        set(value) {
          if (!state.has(this)) state.set(this, {});
          state.get(this)[name] = value;
          if (decoding !== this) return;
          if (!configured.has(this)) configured.set(this, {});
          if (!(name in configured.get(this))) configured.get(this)[name] = value;
        },
      });
    }
    const decode = ClbDecoder.prototype.decode;
    override(ClbDecoder.prototype, 'decode', {
      writable: true,
      value() {
        decoding = this;
        try {
          const result = decode.call(this);
          if (this.fgMux) fourthInputs(this);
          return result;
        } finally { decoding = null; }
      },
    });

    // In modes F and FGM, as in FG, each function generator's fourth input
    // is D or E as one bit of the tile's row 7 (from 0) selects: column 8
    // for F, 13 for G, and 1 is E.  The viewer took D in those modes, so a block
    // whose functions read E there, as XACT's XC3020A XAPP003 designs do,
    // came out reading D.  The block's equations and its D and E enables
    // follow the inputs.
    const FOURTH_INPUT_ROW = 7;
    const FOURTH_INPUT_COLUMN = { F: 8, G: 13 };
    function fourthInputs(clb) {
      const offset = getTileOffset(clb.col, clb.row);
      const bits = curBitstream.data[offset.y + FOURTH_INPUT_ROW];
      const chosen = {};
      for (const half of ['F', 'G']) chosen[half] = bits[offset.x + FOURTH_INPUT_COLUMN[half]] ? 'E' : 'D';
      if (chosen.F === 'D' && chosen.G === 'D') return;
      const reads = (name) => ['F', 'G'].some((half) => chosen[half] === name && clb.inputUsed[half][3]);
      for (const half of ['F', 'G']) clb.lutInput[half][3] = chosen[half];
      clb.dEnable = reads('D');
      clb.eEnable = clb.eEnable || reads('E');
      if (clb.is5Input && chosen.F !== chosen.G) clb.is5Input = false;
      if (clb.is5Input) {
        clb.lutEquation.F = formula5((clb.lut.G << 16) | clb.lut.F, clb.lutInput.F.concat(['E']));
      } else {
        for (const half of ['F', 'G']) clb.lutEquation[half] = formula4(clb.lut[half], clb.lutInput[half]);
      }
    }
    const signalConnection = ClbDecoder.prototype.signalConnection;
    override(ClbDecoder.prototype, 'signalConnection', {
      writable: true,
      value(pin) {
        if ((pin === 'X' || pin === 'Y') && decoding !== this && configured.has(this)) {
          Object.assign(this, configured.get(this));
        }
        return signalConnection.call(this, pin);
      },
    });

    // The viewer stops a trace 300 steps deep, since it keeps no record of
    // the switch-matrix pins a net has passed and a loop of local lines
    // through switch matrices would otherwise never end; a large net,
    // such as one MakeBits's tie-down made, loses whatever lies beyond.
    // So each net keeps, for each switch matrix, the pins it has entered
    // or left by (the viewer's own `visited`, which it scopes to one
    // entry), a switch matrix entered again by one of them is not routed
    // again, and nested steps pass no depth for the limit to reach.
    const NESTED = 1;
    const switchPins = new WeakMap(); // net -> switch matrix -> {pin: true}
    const routeThrough = Switch.prototype.routeThrough;
    override(Switch.prototype, 'routeThrough', {
      writable: true,
      value(pin, net, level, visited) {
        if (!visited) {
          if (!switchPins.has(net)) switchPins.set(net, new Map());
          const byNet = switchPins.get(net);
          if (!byNet.has(this)) byNet.set(this, {});
          visited = byNet.get(this);
          if (visited[pin]) return undefined;
        }
        return routeThrough.call(this, pin, net, NESTED, visited);
      },
    });

    const truncated = [];
    const trace = Path.prototype.traceFrom;
    override(Path.prototype, 'traceFrom', {
      writable: true,
      value(gPt, net, level) {
        if (level) return trace.call(this, gPt, net, NESTED);
        const before = log.length;
        const result = trace.call(this, gPt, net, level);
        if (log.slice(before).some((line) => line.includes('recursion')) && this.origin.obj) {
          truncated.push(this.origin.obj.describePin(this.origin.pin));
        }
        return result;
      },
    });

    const restore = () => {
      undo.reverse().forEach((step) => step());
      // The blocks' EC and RD lived behind the accessors just removed.
      for (const [clb, values] of state) Object.assign(clb, values);
    };
    restore.truncated = truncated;
    restore.configured = configured;
    return restore;
  }

  // The design the viewer decoded, from the state it leaves behind.
  // CORRECTIONS is what installCorrections returned.
  function collect(corrections) {
    const bitstream = bitstreams[0];
    const family = bitstream.family;

    // Every block, by kind, with the pins the viewer gives it.  A pin's
    // net is the block's `<pin>Net` and its routing the `<pin>Path`.
    const kinds = {
      clb: { blocks: clbDecoders.clbDecoders, pins: ['A', 'B', 'C', 'D', 'E', 'DI', 'EC', 'K', 'RD', 'X', 'Y'] },
      iob: { blocks: iobDecoders.iobs, pins: ['I', 'Q', 'O', 'T', 'IK', 'OK', 'CLKI'] },
      clock: { blocks: clockDecoders.clocks, pins: ['I', 'O'] },
      tribuf: { blocks: tribufDecoders.tribufs, pins: ['I', 'T', 'O'] },
      pullup: { blocks: pullupDecoders.pullups, pins: ['O'] },
    };

    // The nets, found from every pin that holds one.  A 3-state Longline
    // is one net the viewer merges others into, so a net's own lists, not
    // the pins, say what it connects.
    const netIds = new Map();
    const nets = [];
    for (const kind of Object.values(kinds)) {
      for (const block of Object.values(kind.blocks)) {
        for (const pin of kind.pins) {
          const net = block[pin.toLowerCase() + 'Net'];
          if (!net || netIds.has(net) || net.isEmpty()) continue;
          netIds.set(net, nets.length);
          const sources = net.sourceList.map((end) => end.obj.describePin(end.pin));
          nets.push({
            id: nets.length,
            name: sources[0],
            sources,
            destinations: net.destList.map((end) => end.obj.describePin(end.pin)),
          });
        }
      }
    }
    const netOfPin = {};
    for (const net of nets) {
      for (const end of net.sources.concat(net.destinations)) netOfPin[end] = net.id;
    }

    // The ids of a path's programmed PIPs: for an input, which choice of
    // its multiplexer the configuration selects.
    function selectedPips(path) {
      if (!path) return [];
      return Object.entries(path.pathById)
        .filter(([, element]) => element.type === 'pip' && element.obj.status)
        .map(([id]) => Number(id));
    }

    // A block's pins: each enabled one with its net, or null when the
    // viewer traced none to it.  An input with none is unrouted, an output
    // with none (its net reaching no enabled pin) unloaded.
    const unrouted = [];
    const unloaded = [];
    function pins(block, names) {
      const result = {};
      for (const pin of names) {
        if (!block.pinEnabled(pin)) continue;
        const name = block.describePin(pin);
        result[pin] = name in netOfPin ? netOfPin[name] : null;
        if (result[pin] !== null) continue;
        const path = block[pin.toLowerCase() + 'Path'];
        const list = path && path.originType === 'source' ? unloaded : unrouted;
        list.push({ pin: name, selectedPips: selectedPips(path) });
      }
      return result;
    }

    const input = (name) => name || null;
    const hex = (table) => table.toString(16).padStart(4, '0');
    // What an unprogrammed block decodes to, as in XACT's blank bitstream
    // (tools/fpga/xact/designs/empty.lca).  A block that differs is
    // configured, and is reported whether or not anything connects to it;
    // a direct input with no net has no configuration of its own, but
    // takes the pull-up off.
    const BLANK_CLB = {
      lut: { F: 0, G: 0 },
      lutInput: { F: ['A', 'QY', 'QY', 'E'], G: ['A', 'QY', 'QY', 'E'] },
      dataInput: { X: 'F', Y: 'G' },
      output: { X: 'QX', Y: 'QY' },
      fgMux: false,
      kInvert: false,
    };
    const BLANK_CLB_STORAGE = { ecEnable: false, rdEnable: false };
    const BLANK_IOB = {
      oEnable: false, oLatch: false, oInvert: false, oFast: false, tEnable: false, tInvert: false,
      iLatch: true, iPullup: true,
    };
    const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
    function clbConfigured(clb) {
      const storage = corrections.configured.get(clb) || {};
      return Object.entries(BLANK_CLB).some(([field, value]) => !same(clb[field], value))
        || Object.entries(BLANK_CLB_STORAGE).some(([field, value]) => (storage[field] || false) !== value);
    }
    const iobConfigured = (iob) => Object.entries(BLANK_IOB).some(([field, value]) => iob[field] !== value);

    // XACT's name for what a function generator gives the outputs and
    // flip-flops: in mode F both halves are the one function F, and in
    // FGM both carry M, E's choice of F or G (DSREF3, Table 4-13).
    function generatorName(clb, name) {
      if (!clb.fgMux || (name !== 'F' && name !== 'G')) return input(name);
      return clb.is5Input ? 'F' : 'M';
    }

    const clbs = {};
    for (const [tile, clb] of Object.entries(clbDecoders.clbDecoders)) {
      if (!clb.xEnable && !clb.yEnable && !clbConfigured(clb)) continue;
      clbs[tile] = {
        row: clb.row,
        column: clb.col,
        // F mode: one function of five variables; FGM: E selects between F
        // and G; FG: two functions of four (data sheet, Figure 6).
        mode: clb.fgMux ? (clb.is5Input ? 'F' : 'FGM') : 'FG',
        // Bit i of a table is the function's value where input j of
        // lutInputs is bit j of i.
        lut: { F: hex(clb.lut.F), G: hex(clb.lut.G) },
        lutInputs: { F: clb.lutInput.F.map(input), G: clb.lutInput.G.map(input) },
        equations: { F: clb.lutEquation.F, G: clb.lutEquation.G },
        data: { X: generatorName(clb, clb.dataInput.X), Y: generatorName(clb, clb.dataInput.Y) },
        outputs: { X: generatorName(clb, clb.output.X), Y: generatorName(clb, clb.output.Y) },
        flipFlops: { X: clb.dataUsed.X, Y: clb.dataUsed.Y },
        enableClock: clb.ecEnable,
        directReset: clb.rdEnable,
        clockInvert: clb.kInvert,
        pins: pins(clb, kinds.clb.pins),
      };
    }

    // Which of an edge's two clock lines each line name is: the viewer
    // sets a line's inversion by name and keeps it in each I/O block by
    // index, so set a line both ways and see which index moves.  The
    // block keeps the line's bit twice, as okInvert and ikInvert;
    // okInvert is set where the bit is 0 at the pin and at upstream pull
    // request #3, which turns ikInvert from the bit's value to the same
    // as okInvert, so both the restore and the input's polarity below
    // read okInvert, and the decode is the same at either commit.
    const edgeOf = (name) => name.replace(/(left|right|upper|lower)$/, '');
    const clockLineNames = Object.keys(clockDecoders.clockLines);
    const lineIndex = {};
    for (const line of clockLineNames) {
      const iob = Object.values(iobDecoders.iobs).find((block) => edgeOf(block.style) === edgeOf(line));
      const saved = iob.okInvert.slice();
      iobDecoders.setClockInvert(line, 0);
      const cleared = iob.ikInvert.slice();
      iobDecoders.setClockInvert(line, 1);
      const index = iob.ikInvert.findIndex((value, i) => value !== cleared[i]);
      iobDecoders.setClockInvert(line, saved[index] ? 0 : 1);
      lineIndex[line] = index;
    }
    function clockLine(iob, index) {
      return clockLineNames.find((line) => edgeOf(line) === edgeOf(iob.style) && lineIndex[line] === index) || null;
    }

    const iobs = {};
    for (const [pad, iob] of Object.entries(iobDecoders.iobs)) {
      if (!(iob.iEnable || iob.qEnable || iob.oEnable || iob.tEnable || iob.clkiEnable || iobConfigured(iob))) continue;
      iobs[pad] = {
        pin: iob.pin,
        bonded: !/^U/.test(iob.pin),
        tile: iob.tile,
        edge: iob.style,
        input: {
          direct: iob.iEnable,
          registered: iob.qEnable,
          storage: iob.iLatch ? 'latch' : 'flip-flop',
          clockLine: iob.qEnable ? clockLine(iob, iob.ikSel) : null,
          // The die-edge line's polarity, which is one for every input
          // flip-flop and latch on it (data sheet, Figure 4, 7-7): set, the
          // flip-flops take the falling edge and the latches are
          // transparent while it is High.  The pinned viewer's ikInvert
          // reads the other way about (XACT's input flip-flops without
          // IKNOT come out with it set); okInvert, the line's bit read as
          // the output's, is the input's polarity at either commit.
          clockInvert: iob.qEnable ? iob.okInvert[iob.ikSel] : null,
        },
        output: {
          enabled: iob.oEnable,
          registered: iob.oLatch,
          invert: iob.oInvert,
          fast: iob.oFast,
          threeState: iob.tEnable,
          threeStateInvert: iob.tInvert,
          clockLine: iob.okEnable ? clockLine(iob, iob.okSel) : null,
          clockInvert: iob.okEnable ? iob.okInvert[iob.okSel] : null,
        },
        // The data sheet allows the pull-up on inputs alone (7-7); with the
        // output used, the bit the viewer reads for it is one of the
        // 3-state control's multiplexer bits.
        pullup: iob.iPullup && !iob.oEnable,
        dedicatedClock: iob.clkin,
        pins: pins(iob, kinds.iob.pins.filter((pin) => pin !== 'CLKI')),
      };
    }

    // The die-edge clock lines: each takes its direct input, its clock
    // buffer's output, or its branch, whose own PIPs choose among the
    // branch's inputs.
    const clockLines = {};
    for (const line of clockLineNames) {
      const on = (kind) => clockLinePip(line, kind).obj.status;
      clockLines[line] = {
        index: lineIndex[line],
        input: on('direct') ? 'direct' : (on('branch') ? 'branch' : null),
        branchInputs: selectedPips(clockDecoders.clockInputs[line]),
      };
    }

    const clocks = {};
    for (const [name, clock] of Object.entries(clockDecoders.clocks)) {
      if (!clock.enabled) continue;
      clocks[name] = {
        inputs: selectedPips(clock.iPath),
        outputs: selectedPips(clock.oPath),
        pins: pins(clock, clock.iPath ? ['I', 'O'] : ['O']),
      };
    }

    const tribufs = {};
    for (const [name, tribuf] of Object.entries(tribufDecoders.tribufs)) {
      if (!tribuf.enabled) continue;
      tribufs[name] = { line: tribuf.line, pins: pins(tribuf, kinds.tribuf.pins) };
    }
    const pullups = {};
    for (const [name, pullup] of Object.entries(pullupDecoders.pullups)) {
      if (!pullup.enabled) continue;
      pullups[name] = { line: pullup.line, pins: pins(pullup, kinds.pullup.pins) };
    }

    return {
      // The caller fills in the viewer clone's origin and commit.
      decoder: { url: null, commit: null },
      // The viewer names the XC3000A and XC3100A architecture for its
      // XC3100A part ("XC3142A"), which a bitstream cannot tell from the
      // XC3000A's; and recognizes it by what only it has, so an XC3000A
      // design that uses none of that decodes as the XC3000 part.
      part: family.extraInter === 1 ? family.name.replace(/^XC31(\d\d)A$/, 'XC30$1A/XC31$1A') : family.name,
      package: packageName,
      rows: family.rows,
      columns: family.cols,
      // Where the configuration data starts, in bits from the file's start.
      offsetBits: bitstream.offset,
      options: {
        inputLevels: otherDecoder.inputLevels,
        readback: otherDecoder.readback,
        crystalOscillator: otherDecoder.xtalOsc,
        doneTime: otherDecoder.doneTime,
        resetTime: otherDecoder.resetTime,
        donePullup: otherDecoder.donePullup,
      },
      clbs,
      iobs,
      // Every I/O block's package pin, "U" and its pad number when unbonded.
      padPins: Object.fromEntries(Object.entries(iobDecoders.iobs).map(([pad, iob]) => [pad, iob.pin])),
      clocks,
      clockLines,
      tribufs,
      pullups,
      nets,
      unrouted,
      unloaded,
      truncatedTraces: corrections.truncated,
      messages: log.slice(),
    };
  }
}

// The clone the viewer is read from: its origin and commit, the commit
// marked "-dirty" when the clone has local changes.
function viewerProvenance(viewer) {
  const { execFileSync } = require('child_process');
  const git = (...args) => execFileSync('git', ['-C', viewer, ...args], { encoding: 'utf8' }).trim();
  try {
    const dirty = git('status', '--porcelain') ? '-dirty' : '';
    return { url: git('remote', 'get-url', 'origin'), commit: git('rev-parse', 'HEAD') + dirty };
  } catch (error) {
    return { url: 'unknown', commit: 'unknown' };
  }
}

if (typeof module !== 'undefined' && require.main === module) {
  const vm = require('vm');
  const { loadViewer, readBitstream } = require('./viewer.js');
  const [viewer, file, packageName = 'PLCC84'] = process.argv.slice(2);
  if (!viewer || !file) {
    console.error('usage: node netdump.js VIEWER_HTML_DIR FILE.bit [PACKAGE]');
    process.exit(2);
  }
  const log = [];
  const context = loadViewer(viewer, log);
  Object.assign(context, { bitstreamBytes: readBitstream(file), packageName, log });
  vm.runInContext(`${xc3000Netlist}\nreport = xc3000Netlist(bitstreamBytes, packageName, log);`, context);
  context.report.decoder = viewerProvenance(viewer);
  process.stdout.write(JSON.stringify(context.report, null, 1) + '\n');
}
