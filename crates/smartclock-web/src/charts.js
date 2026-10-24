// Chart helpers shared by the pages that draw time series: what each
// column is called and in what units, and how a y axis is fitted to
// it.  Loaded after common.js.
"use strict";

// What each of the host's sensor quantities is called in a title,
// after whose it is.
const SENSOR_NAMES = {
  temperature: "temperature, C",
  humidity: "relative humidity, %",
  pressure: "pressure, kPa",
};

// Name, the factor that puts the column in units worth reading, and
// an optional qualifier said beside the name in the chart's title
// only: the menu and the cursor readout use the bare name.
// The interval is stored in seconds and is a few nanoseconds: plotted
// as seconds, every axis tick rounds to 0.
const LABELS = {
  efc_percent: ["EFC, percent of range", 1],
  efc_dac: ["EFC, raw counts", 1],
  temperature_c: ["Internal temperature, C", 1],
  oven_current: ["Oven current", 1],
  oven_tempco: ["Oven current coefficient, EFC counts per unit", 1],
  time_interval_s: ["1 PPS TI, ns", 1e9],
  tfom: ["Time figure of merit", 1, "lower is better"],
  ffom: ["Frequency figure of merit", 1, "lower is better"],
  // The receiver's own words, said as counts because that is what is
  // plotted.  Nothing is added to them: "not tracked" is the count
  // `:GPS:SATellite:VISible:PREDicted?` gives less those being tracked;
  // HINTS quotes the manual on it.
  tracking: ["# sats tracked", 1],
  not_tracking: ["# sats not tracked", 1],
  holdover_predicted_s: ["Predicted holdover uncertainty, µs", 1e6],
  // The host's sensors, a chart per quantity: of the air around the
  // receiver, as against its own.
  ...Object.fromEntries(
    Object.entries(SENSOR_NAMES).map(([quantity, name]) => [`sensor:${quantity}`, [`Ambient ${name}`, 1]]),
  ),
};
// Hover text for every series, by column: what each is and where it
// comes from, which a name and a unit cannot carry, and for the figures
// of merit what their codes mean.  Taken from the manuals where they
// document it, and from the command table
// (crates/smartclock/commands.toml) for the undocumented queries.
const HINTS = {
  efc_percent: "Oscillator steering, percent of range (097-59551-02 5-22).",
  efc_dac: "The EFC as a raw 20-bit value (undocumented query).",
  time_interval_s:
    "Oscillator 1 PPS against GPS 1 PPS, ten-second mean (097-59551-02 5-25).",
  temperature_c: "Inside the receiver, not the oven (undocumented query).",
  oven_current: "Rises while the oven heats (undocumented query).",
  oven_tempco:
    "Loop constant on the oscillator current; changes only when set "
    + "(docs/firmware/loop.md).",
  tfom:
    "1 PPS error as a decade: 0 under 1 ns, 3 is 100 to 1000 ns, 9 over "
    + "0.1 s (097-59551-02 5-24).",
  ffom:
    "0 stable, 1 stabilizing, 2 holdover, 3 unlocked: do not use "
    + "(097-59551-02 5-23).",
  tracking: "Satellites tracked (097-59551-02 5-20).",
  not_tracking: "Predicted visible but not tracked (097-59551-02 5-19).",
  holdover_predicted_s:
    "Time error predicted for the first 24 hours of holdover, from the "
    + "loop's aging fit; 432 µs until the fit has enough history "
    + "(097-59551-02 5-24, docs/firmware/loop.md).",
};

// The hover text for the columns a chart draws, each once.
const hintText = (...cols) => [...new Set(cols)].map((c) => HINTS[c]).filter(Boolean).join(" ");

// Give a chart's title the hover text for its columns, underlined so it
// is found.
const hint = (chart, ...cols) => hintAs(chart, hintText(...cols));

function hintAs(chart, text) {
  const heading = chart.root.querySelector(".u-title");
  if (!text || !heading) return;
  heading.title = text;
  heading.classList.add("hinted");
}

const columnName = (c) => (LABELS[c] ?? [c, 1])[0];
const label = (c) => {
  const qualifier = LABELS[c]?.[2];
  return qualifier ? `${columnName(c)} (${qualifier})` : columnName(c);
};
const factor = (c) => (LABELS[c] ?? [c, 1])[1];

// The widest one-sigma interval, its upper bound over its lower, a
// deviation is plotted with.  Past it a point says less than its band
// does, and the last one or two of a short run read as a slope that is
// not there; the table still has them.
const MAX_BAND = 2;

// `values`, a deviation by tau, with everything from the first point
// whose interval in `bounds` is wider than MAX_BAND left out.  An
// interval that could not be computed is not judged.
function untilWide(values, bounds) {
  const cut = bounds.findIndex((b) => b && (b.lower <= 0 || b.upper / b.lower > MAX_BAND));
  return cut < 0 ? values : values.map((v, i) => (i < cut ? v : null));
}
const scaled = (values, c) =>
  factor(c) === 1 ? values : values.map((v) => (v === null ? null : v * factor(c)));

// Points to ask for: enough that a long window is not thinned to
// half-hour bins, bounded by what the server will give.
function pointsFor(seconds) {
  if (!seconds) return 5000;
  return Math.min(5000, Math.max(1500, Math.round(seconds / 30)));
}

// A y scale that begins and ends on a labeled gridline.
//
// uPlot scales to the data and then places round ticks inside it, so
// the top and bottom lines fall short by whatever the data happens to
// need -- by a different amount on every plot, which across a stack
// reads as ragged: a label at the top of one, none at the bottom of the
// next.  Snapping the scale itself to the tick step fixes it, at the
// cost of some empty space above and below the trace.
//
// Steps are 1, 2, 5 or 10 times a power of ten, so the labels are
// numbers a person would choose.  2.5 is excluded deliberately: it is a
// legitimate step and it produces axes like 35.925, which is not.
const TICK_COUNT_WEIGHT = 0.3;

// Columns that count things.  A count has no fractional value and
// cannot be negative, so its axis starts at zero and steps in whole
// numbers; the general rule would give a satellite count that never
// moved off zero an axis from -1 to 1, with a tick at -0.5.
// Each is pinned to the top of its range rather than fitted to the
// data, so the axis does not rescale as the window moves and two
// receivers can be read against one another.  TFOM runs 1 to 9 and
// FFOM 0 to 3 by definition; the satellite counts are bounded by the
// channels, and grow if a receiver ever exceeds the pin.
const COUNTS = { tracking: 8, not_tracking: 12, tfom: 9, ffom: 3 };

// Columns drawn on a log axis.  The predicted holdover uncertainty sits
// at 432 µs until the loop's aging fit has enough history, then a few
// µs to a few tens: on a linear axis the plateau flattens everything
// after it against zero.
const LOG_COLUMNS = new Set(["holdover_predicted_s"]);

// `room`, as `mostTicks` gives it, caps the gaps between labels.
function countScale(max, pinned, target, room) {
  const top = Math.max(pinned, Math.ceil(max) || 0, 1);
  const most = Math.min(Math.max(4, target + 3), room);
  // A step that divides the top exactly, so the axis ends on the pin
  // rather than above it: TFOM runs to 9, and rounding up to a step of
  // 2 gave it an axis to 10 and a grid line at a figure of merit the
  // receiver cannot report.
  for (const step of [1, 2, 3, 4, 5, 6, 10, 12, 20, 25, 50]) {
    if (top % step) continue;
    const n = top / step;
    if (n >= 2 && n <= most) return { lo: 0, hi: top, step, n };
  }
  for (const step of [1, 2, 5, 10, 20, 50]) {
    const hi = Math.ceil(top / step) * step;
    const n = hi / step;
    if (n >= 2 && n <= most) return { lo: 0, hi, step, n };
  }
  return { lo: 0, hi: top, step: top, n: 1 };
}

// `room`, as `mostTicks` gives it, caps the gaps between labels, unless
// no round step fits under it.
function niceScale(min, max, target, room) {
  if (!isFinite(min) || !isFinite(max)) return null;
  // A series that never moved still needs an axis with room around it.
  //
  // "Never moved" cannot be exact equality.  A constant column still
  // arrives with a spread: SQLite averages each bucket, and summing
  // the same number repeatedly in floating point does not give it back
  // exactly.  Oven current held at 104.9 came through spanning 2e-14,
  // which is not flat by `===`, so the step came out at 1e-15 and the
  // axis read 104.900000000000020.  The oven current coefficient,
  // which is constant between calibrations, would do the same.
  const noise = Math.max(Math.abs(min), Math.abs(max)) * 1e-9;
  if (max - min <= noise) {
    const pad = Math.max(Math.abs(min) * 0.05, 1);
    min -= pad;
    max += pad;
  }
  const span = max - min;
  const aim = Math.min(target, room);
  const mag = Math.pow(10, Math.floor(Math.log10(span / aim)));
  // Under a cap, a step a decade up as well: a span just under a power
  // of ten otherwise has no round step that few ticks cover.
  return roundScale(min, max, span, mag, aim, Math.min(room, MOST_TICKS), [1, 2, 5, 10, 20, 50]) ??
    roundScale(min, max, span, mag, aim, MOST_TICKS, [1, 2, 5, 10]);
}

// Two lines is not an axis; nine is a grid.
const MOST_TICKS = 8;

// The best step of `multiples` of `mag` over `min` to `max`, with at
// most `most` gaps, as `niceScale` scores them; null for none.
function roundScale(min, max, span, mag, target, most, multiples) {
  let best = null;
  for (const m of multiples) {
    const step = m * mag;
    const lo = Math.floor(min / step) * step;
    const hi = Math.ceil(max / step) * step;
    const n = Math.round((hi - lo) / step);
    if (n < 2 || n > most) continue;
    // Both matter and the balance is empirical.  Weighting the tick
    // count heavily gave internal temperature an axis of 30 to 45 for
    // data spanning 33.8 to 40.3 -- round, correctly bounded, and
    // using less than half the plot.  At this weight the same data
    // gets 32 to 42, which is still round and fills two thirds.
    const score = Math.abs(n - target) * TICK_COUNT_WEIGHT + (hi - lo - span) / span;
    if (!best || score < best.score) best = { lo, hi, step, n, score };
  }
  return best;
}

// How many gaps between labels suit a plot of this height.  About
// forty pixels each: closer and the labels crowd, further and a short
// plot ends up with two.
function tickTarget(height) {
  return Math.min(6, Math.max(3, Math.round(height / 40)));
}

// The least room a y label needs, in CSS pixels, with the gap to the
// next: closer and they overprint, as the kiosk's large labels did on
// a stack of short plots.
const LABEL_ROOM = 24;

// The most gaps between labels a stack chart of this height has room
// for, on the plot inside its ends.
const mostTicks = (height) => Math.max(2, Math.floor((height - 2 * STACK_END) / LABEL_ROOM));

// How tall each chart of a stack is, given how many there are, less
// the time axis under the bottom one.  One gets the room
// it always had; a stack divides a similar budget, with a floor below
// which a plot stops being readable.
function plotHeight(count) {
  return count === 1 ? 420 : Math.max(150, Math.round(560 / count));
}

// One tick label, at the precision its step implies.  Fixed places
// rather than toLocaleString's default, or a step of 0.05 renders as
// 35.9 and 36 and the axis looks unevenly spaced when it is not.
// Not named `tick`, which common.js uses for the status strip's poller.
function tickLabel(v, places) {
  return v.toLocaleString(undefined, {
    minimumFractionDigits: places,
    maximumFractionDigits: places,
  });
}

// The least span a column's y axis covers, in its plotted units, for
// readings whose noise would otherwise fill the plot: a room that holds
// to a tenth of a degree is drawn as steady, not as a jagged swing.
const LEAST_SPAN = { "sensor:temperature": 0.5, "sensor:humidity": 2, "sensor:pressure": 0.2 };

// `lo` and `hi` widened about their middle to the column's least span.
// A single sensor, `sensor:QUANTITY:NAME`, takes its quantity's.
function leastSpan(col, lo, hi) {
  const least = LEAST_SPAN[col.split(":", 2).join(":")] ?? 0;
  if (hi - lo >= least) return [lo, hi];
  const middle = (lo + hi) / 2;
  return [middle - least / 2, middle + least / 2];
}

// A y axis for `values` of column `col` on a plot `height` pixels
// tall: the range its scale is snapped to, and the axis's ticks,
// labels and width to match, so the lines and the range cannot
// disagree.  Both empty when there is nothing to fit.
function yAxis(col, values, height) {
  if (LOG_COLUMNS.has(col)) return logAxis(values.filter((v) => v > 0));
  const nice = !values.length
    ? null
    : col in COUNTS
      ? countScale(Math.max(...values), COUNTS[col], tickTarget(height), mostTicks(height))
      : niceScale(...leastSpan(col, Math.min(...values), Math.max(...values)), tickTarget(height), mostTicks(height));
  const places = nice ? Math.max(0, -Math.floor(Math.log10(nice.step))) : 0;
  const ticks = [];
  if (nice) {
    for (let i = 0; i <= nice.n; i++) ticks.push(nice.lo + i * nice.step);
  }
  // Wide enough for the longest label this axis will actually draw.
  // Measured from the ticks rather than from the data, since a tick
  // can be wider than every value under it.  The default gutter
  // silently clips instead of overflowing, which turned an EFC of
  // 712,793 into an axis reading 12,793 -- a plausible number, wrong
  // by seven hundred thousand.
  const widest = Math.max(
    ...(ticks.length ? ticks : [0]).map((v) => tickLabel(v, places).length),
    4,
  );
  return {
    axis: {
      size: 30 + widest * 8,
      ...(nice
        ? {
            splits: () => ticks,
            values: (u, vals) => vals.map((v) => tickLabel(v, places)),
          }
        : {}),
    },
    range: nice ? [nice.lo, nice.hi] : null,
  };
}

// A y axis on decades for `values`, all positive, in the shape `yAxis`
// returns, with the log scale it needs.  Ends on a graduation at or
// beyond the data and names 1, 2 and 5 of each decade, as the
// stability page's axes do.
function logAxis(values) {
  if (!values.length) return { axis: {}, range: null };
  const range = [snapDown(Math.min(...values)), snapUp(Math.max(...values))];
  // uPlot passes null for a split it has dropped.
  const format = (v) => v?.toLocaleString(undefined, { maximumSignificantDigits: 3 }) ?? null;
  const widest = Math.max(4, ...range.map((v) => format(v).length));
  return {
    axis: {
      size: 30 + widest * 8,
      splits: (u, axis, min, max) => graduations(u, axis, min, max) ?? range,
      values: (u, splits) => labels(splits, format),
    },
    range,
    scale: LOG,
  };
}

// Charts in a stack share a time axis only if their plotting areas line
// up: each gets the widest y axis of the stack, and the same right
// padding, wide enough for the bottom chart's last time label, which
// uPlot would otherwise make room for on that chart alone.
const STACK_RIGHT = 32;

// The colors every chart's axes are drawn in: the labels, then the grid
// and ticks, recessive so the readings are seen first.
const AXIS_INK = "#8b929c";
const AXIS_RULE = "#2b3038";
// An axis as every chart draws one.
const AXIS = { stroke: AXIS_INK, grid: { stroke: AXIS_RULE }, ticks: { stroke: AXIS_RULE } };
// The least room between time labels, in CSS pixels, with a larger
// font of WIDE_FONT_PX: uPlot's own 50 suits its own 12 px text.
const WIDE_TIME_SPACE = 140;
// The time axis's height with that font, for its two lines of labels:
// uPlot's own 50 clips the second.
const WIDE_TIME_SIZE = 66;
// uPlot's own time axis height, for its own font.
const TIME_SIZE = 50;
// The font size the two above are for; another is given room in
// proportion.
const WIDE_FONT_PX = 17;

// The height of the time axis under a stack's bottom chart, in CSS
// pixels, for the axes' `font`, or uPlot's own when none is given.
const timeAxisHeight = (font) => (font ? WIDE_TIME_SIZE * parseFloat(font) / WIDE_FONT_PX : TIME_SIZE);

// The room above and below a stack chart's plot, for half its top and
// bottom y labels: uPlot's own default, a third of its time axis.
// Under the bottom chart the time axis takes its place.
const STACK_END = Math.round(TIME_SIZE / 3);

// That room for the axes' `font`: a line of its type, which a kiosk
// scaled down to a small screen would otherwise lose to padding.
const stackEnd = (font) => (font ? Math.round(parseFloat(font)) : STACK_END);

// How much taller than the rest a stack's bottom chart is, for the
// axes' `font`: its time axis, less the room under its plot the axis
// takes the place of.
const timeAxisRoom = (font) => timeAxisHeight(font) - stackEnd(font);

// The height of chart `n` of a stack of `count`, each given `height`,
// with the axes' font `font`.  The bottom chart's time axis is drawn
// inside its height, so it is given the axis's room on top: otherwise
// its plot is that much shorter than the others', which on the kiosk's
// large type left it a few pixels.
const stackChartHeight = (height, n, count, font) => height + (n === count - 1 ? timeAxisRoom(font) : 0);

// The options every chart in a stack of time charts shares:
//   width, height  its size, as `stackChartHeight` gives the height
//   title          its title
//   series         its series after the time
//   fitted, ySize  its y axis as `yAxis` fitted it, and the stack's
//                  shared y axis width
//   last           whether it is the bottom chart, the one that
//                  carries the time labels
//   yStroke        the y axis labels' color, when not the usual
//   font           the axes' font, when not uPlot's own; larger
//                  labels get more room between times, or uPlot's
//                  first, dated, label runs into the next
//   legend, hooks  uPlot's, the hooks added to the drag that sets
//                  the range
function stackOptions(stack) {
  const font = stack.font ? { font: stack.font } : {};
  const wide = stack.font ? parseFloat(stack.font) / WIDE_FONT_PX : 0;
  const timeRoom = { size: timeAxisHeight(stack.font), ...(stack.font ? { space: WIDE_TIME_SPACE * wide } : {}) };
  return {
    width: stack.width,
    height: stack.height,
    padding: [stackEnd(stack.font), STACK_RIGHT, stack.last ? 0 : stackEnd(stack.font), null],
    title: stack.title,
    series: [{}, ...stack.series],
    axes: [
      { ...AXIS, ...font, ...timeRoom, show: stack.last },
      { ...AXIS, ...font, ...(stack.yStroke ? { stroke: stack.yStroke } : {}), ...stack.fitted.axis, size: stack.ySize },
    ],
    scales: {
      x: { time: true },
      ...(stack.fitted.range ? { y: { ...stack.fitted.scale, range: stack.fitted.range } } : {}),
    },
    legend: stack.legend ?? { show: true, live: true },
    cursor: { drag: { x: true, y: false }, sync: { key: stack.sync, setSeries: false } },
    hooks: { ...stack.hooks, ...TIME_HOOKS },
  };
}

// Give every chart in a stack the width its container has settled at.
// Each was made at the width the container had then, and adding charts
// can bring on a scrollbar that narrows it partway down the stack.
function settleWidths(charts, container) {
  const width = container.clientWidth;
  for (const c of charts) {
    if (width && c.width !== width) c.setSize({ width, height: c.height });
  }
}

// Each time moved to the center of its bucket, as the server numbers
// them for the window `win`: the server reports the mean time of the
// readings in a bucket, which differs between logs that share it, so
// series from two logs land on one grid only once snapped to it.
function onGrid(at, win) {
  const points = win.points ?? pointsFor(win.to - win.from);
  const width = (win.to - win.from + 1) / points;
  return at.map((t) => win.from + (Math.floor((t - win.from) / width) + 0.5) * width);
}

// uPlot.join's mode that widens a null over the alignment points next
// to it, so a gap stays a gap after joining.
const NULL_EXPAND = 2;

// Tables of [times, ...values], joined on their times into one.  A gap
// one table marks with a null is widened over the other tables' times
// inside it, or the line would be drawn straight across the gap.
const joinTables = (tables) => uPlot.join(tables, tables.map((t) => t.map(() => NULL_EXPAND)));

// Hold a chart container at the height it has now, while its charts are
// drawn again, until `release`.  Called before they are taken down:
// emptied, the page would be too short for the place it was scrolled
// to, and the browser would put it back at the top.
function hold(container) {
  container.style.minHeight = `${container.offsetHeight}px`;
}

// Let a container held by `hold` take its own height again.
function release(container) {
  container.style.minHeight = "";
}

// The y axis width a stack of fitted axes shares.
const stackWidth = (fits) => Math.max(...fits.map((f) => f.axis.size));

// Tie a time chart to the range: a drag fixes the range to the stretch
// it covered, a double click zooms out, as in Grafana.  The drag
// is read from setSelect, not setScale: setScale also fires when uPlot
// fits the scale to new data, on every draw.
const TIME_HOOKS = {
  setSelect: [
    (u) => {
      if (u.select.width <= 0) return;
      fixRange(u.posToVal(u.select.left, "x"), u.posToVal(u.select.left + u.select.width, "x"));
    },
  ],
};
function ranged(chart) {
  chart.over.addEventListener("dblclick", () => zoomOut());
  return chart;
}

// A deviation plot's size: its width is the pane's, no narrower than
// `min`, which a phone's pane can hold; its height follows the width by
// `ratio`, between `shortest` and `tallest`.  uPlot draws its text at a
// fixed size however wide the plot is, so width buys plotting area
// rather than magnifying everything -- which is the difference between
// this and scaling a drawing to fit.
const DEVIATION_PLOT = { min: 240, ratio: 2.5, shortest: 300, tallest: 560 };

// A plot's height for `width`, by `DEVIATION_PLOT`.  It follows the
// width rather than being fixed, so a curve keeps its shape on any
// window: at a fixed height a wide plot flattens a slope that has not
// changed, which on a log-log chart is the one thing the reader is
// meant to judge by eye.  Bounded at both ends, because the ratio
// alone would make a very wide window very tall.
function figureHeight(width) {
  return Math.round(
    Math.max(DEVIATION_PLOT.shortest, Math.min(DEVIATION_PLOT.tallest, width / DEVIATION_PLOT.ratio)),
  );
}

// Give a chart drawn earlier `data` in place, at `size`.
function redata(chart, data, size) {
  chart.setData(data);
  if (size.width !== chart.width || size.height !== chart.height) chart.setSize(size);
}

// ---------------------------------------------------------------- stack
//
// The live page's stack of time charts, a chart per column sharing one
// time axis, with the host's sensors joined in below the receiver's
// temperature.  Shared with the kiosk, which draws the same stack from
// fewer columns.

// The two trace colors, named once.  Both the plot and the things
// that say which line is which read them from here, because a key that
// disagrees with the lines is worse than no key.
const TRACE = "#38bdf8";
const MATE = "#fbbf24";

function seriesQuery(cols, from, to, seconds) {
  const q = new URLSearchParams({ columns: cols.join(","), points: pointsFor(seconds) });
  if (from) q.set("from", Math.round(from));
  if (to) q.set("to", Math.round(to));
  return "/api/history?" + withUnit(q);
}

const fetchSeries = (signal, ...args) => ask(seriesQuery(...args), signal, HISTORY_TIMEOUT);

// Tracked and not tracked are one measurement split two ways: their
// sum is the satellites the receiver knows about, so a chart apiece
// hides the relationship that matters.  Drawn together, the only
// columns that are.
const PAIRED = ["tracking", "not_tracking"];

// Split two labels into what they have in common and what each adds,
// on a word boundary.  "# sats tracked" and "# sats not tracked"
// become "# sats " with "tracked" and "not tracked", so the title
// says the subject once and colors only the part that tells the two
// lines apart.
function shareName(a, b) {
  const x = a.split(" ");
  const y = b.split(" ");
  let n = 0;
  while (n < x.length && n < y.length && x[n] === y[n]) n++;
  if (n === 0) return ["", a, b];
  return [x.slice(0, n).join(" ") + " ", x.slice(n).join(" "), y.slice(n).join(" ")];
}

// The colors of a chart's lines, in order.  A chart of one line draws
// it in the first.
const LINE_COLORS = [TRACE, MATE];

// The charts of the stack, from the server's plots: a chart per
// column, with the paired columns sharing one.  Each chart is
//   column    what its y axis is fitted to and its hover text is for
//   envelope  { min, max }, drawn behind the first line, or null
//   lines     [{ column, values, color }] on one y axis, in order
function chartsOf(plots) {
  const line = (plot, n) => ({
    column: plot.column,
    values: scaled(plot.mean, plot.column),
    color: LINE_COLORS[n],
  });
  const partner = plots.find((p) => p.column === PAIRED[1]);
  const paired = plots.some((p) => p.column === PAIRED[0]) && partner;
  return plots
    .filter((p) => !(paired && p.column === PAIRED[1]))
    .map((plot) => ({
      column: plot.column,
      envelope: { min: scaled(plot.min, plot.column), max: scaled(plot.max, plot.column) },
      lines: [line(plot, 0), ...(paired && plot.column === PAIRED[0] ? [line(partner, 1)] : [])],
    }));
}

// A multi-line chart's title, each line's own words in its color, so
// the title says which line is which with the legend off.
function groupTitle(lines) {
  if (lines.some((l) => l.name)) {
    return (
      esc(label(lines[0].column)) +
      '<span class="muted">: </span>' +
      lines
        .map((l) => `<span style="color:${l.color}">${esc(l.name)}</span>`)
        .join('<span class="muted">, </span>')
    );
  }
  if (lines.length === 2) {
    const [shared, mine, theirs] = shareName(label(lines[0].column), label(lines[1].column));
    return (
      esc(shared) +
      `<span style="color:${lines[0].color}">${esc(mine)}</span>` +
      `<span class="muted"> and </span>` +
      `<span style="color:${lines[1].color}">${esc(theirs)}</span>`
    );
  }
  return lines
    .map((l) => `<span style="color:${l.color}">${esc(label(l.column))}</span>`)
    .join('<span class="muted">, </span>');
}

// Every value a drawing plots, its envelope's as well as its lines'.
const drawingArrays = (d) => [
  ...(d.envelope ? [d.envelope.min, d.envelope.max] : []),
  ...d.lines.map((l) => l.values),
];

// Whether a drawing has anything to plot.  One with no data has no y
// axis, which would shift its plot out of line with the rest of a stack.
const drawsAnything = (d) => drawingArrays(d).flat().some((v) => v != null && isFinite(v));

// Read the stack over the range: `wanted`, the receiver's columns, and
// the sensors `listed`, as `sensorsListed` gives them.  Answers
//   why     "" with something to draw, or what to say in its place
//   x       the shared time axis
//   drawn   the charts, as `chartsOf` makes them, with the sensors'
//           below the receiver's temperature, and none left empty
async function readStack(signal, wanted, listed) {
  const bounds = rangeBounds();
  let { from, to } = bounds;
  if (from === 0) from = null;
  let seconds = bounds.seconds;

  let got;
  if (wanted.length) {
    got = await fetchSeries(signal, wanted, from, to, seconds);
    // How far back the log goes is only knowable once it has been
    // asked, so "all" costs a second request: the first answers with
    // the server's default hour and the extent, the second asks for
    // the extent.  Without this the button quietly showed the last
    // hour -- the range it names is the one range it did not fetch.
    if (range.last === "all" && got.first && got.last > got.first) {
      seconds = got.last - got.first;
      got = await fetchSeries(signal, wanted, got.first, got.last, seconds);
    }
  } else {
    // The sensors alone: their own extent stands in for the receiver's.
    if (range.last === "all" && listed.first != null) {
      from = listed.first;
      to = listed.last;
      seconds = to - from;
    }
    got = { at: [], plots: [], from: Math.round(from ?? 0), to: Math.round(to ?? Date.now() / 1000) };
  }
  // The grid the server bucketed on, which the sensors are asked for
  // and snapped to as well.
  const win = { from: got.from, to: got.to, points: pointsFor(seconds) };
  const groups = await sensorGroups(listed, win, signal);
  const anything = got.at.length || groups.length;
  const why = !anything ? "no readings in this range"
    : wanted.length && !got.at.length ? "no readings from the receiver in this range"
    : "";
  if (!anything) return { why, x: [], drawn: [] };

  const drawn = chartsOf(got.plots);
  // One time axis for the receiver's charts and the sensors', joined
  // on the grid; without sensors the receiver's own times are kept.
  let x = got.at;
  const sensorDrawings = groups.map((group) => ({
    column: `sensor:${group.quantity}`,
    envelope: null,
    title: sensorTitle(group.quantity, group.lines.map((l) => l.name)),
    hint: sensorHint(group.lines),
    lines: group.lines.map((line) => ({
      column: `sensor:${group.quantity}`,
      name: line.name,
      reading: sensorReadingName(line.name, group.quantity, line.unit),
      values: line.values,
      color: line.color,
      at: line.at,
    })),
  }));
  if (sensorDrawings.length) {
    const joined = joinTables([
      [onGrid(got.at, win), ...drawn.flatMap(drawingArrays)],
      ...sensorDrawings.flatMap((d) => d.lines.map((l) => [l.at, l.values])),
    ]);
    x = joined[0];
    let k = 1;
    for (const d of [...drawn, ...sensorDrawings]) {
      if (d.envelope) {
        d.envelope.min = joined[k++];
        d.envelope.max = joined[k++];
      }
      for (const l of d.lines) l.values = joined[k++];
    }
    // Right below the receiver's own temperature, which is what a room
    // temperature is read against; at the end without it.
    const after = drawn.findIndex((d) => d.column === "temperature_c");
    drawn.splice(after < 0 ? drawn.length : after + 1, 0, ...sensorDrawings);
  }
  // Only charts with something to draw, as the receiver's are not
  // beside sensors in a range it has no readings in.
  return { why, x, drawn: drawn.filter(drawsAnything) };
}

// Each chart's y axis, fitted to what it draws.  The scale has to
// cover the envelope, not just the lines: the min and max are drawn,
// and an axis that ends below them would clip the very excursions they
// exist to show.
const stackFits = (read, height) =>
  read.drawn.map((chart) =>
    yAxis(chart.column, drawingArrays(chart).flat().filter((v) => v != null && isFinite(v)), height));

// What a chart of the stack is drawn as, less its data and its y
// axis's range, ticks and width: its lines, and the kind of y axis.
// Two reads that give a chart the same shape can share it.
const stackShape = (drawing, fitted) =>
  JSON.stringify([
    drawing.lines.map((l) => [l.column, l.name, l.color]),
    Boolean(drawing.envelope),
    Boolean(fitted.range),
    Boolean(fitted.axis.splits),
    Boolean(fitted.scale),
  ]);

// A fitted y axis, as `yAxis` gives it, that reads its range, ticks
// and labels from `held.fit`, and its width from `held.ySize`, each
// time the chart draws: `restack` refits a chart in place by changing
// `held`, without drawing it afresh.
function heldAxis(held) {
  const { fit } = held;
  return {
    axis: {
      ...(fit.axis.splits
        ? {
            splits: (...args) => held.fit.axis.splits(...args),
            values: (...args) => held.fit.axis.values(...args),
          }
        : {}),
    },
    range: fit.range && (() => held.fit.range),
    scale: fit.scale,
  };
}

// Draw the stack `readStack` read into `container`, and answer its
// charts.  A drawing with an `x` of its own is plotted against that
// rather than the stack's.  `style` is
//   height(count)  each chart's height, given how many there are; the
//                  bottom one is given its time axis on top
//   sync           the cursor's sync key
//   hooks          uPlot's, as `stackOptions` takes them
//   font           the axes' font, when not uPlot's own
//   yStroke        the y labels' color, when not the trace's
//   legend         uPlot's, when each chart has one; none by default
function stackCharts(container, read, style) {
  const height = style.height(read.drawn.length);
  const fits = stackFits(read, height);
  const ySize = stackWidth(fits);
  const charts = read.drawn.map((drawing, n) => {
    // A receiver's lines are colored, and named in the title, only to
    // tell several apart.
    const several = drawing.lines.length > 1;
    const host = document.createElement("div");
    container.appendChild(host);
    const held = { fit: fits[n], ySize };
    const opts = stackOptions({
      width: container.clientWidth || 800,
      height: stackChartHeight(height, n, read.drawn.length, style.font),
      title: drawing.title ?? drawing.lines.map((l) => label(l.column)).join(" and "),
      series: [
        ...(drawing.envelope
          ? [
              { label: "min", stroke: "#0e7490", width: 1, points: { show: false } },
              { label: "max", stroke: "#0e7490", width: 1, points: { show: false } },
            ]
          : []),
        ...drawing.lines.map((l) => ({
          label: l.name ?? label(l.column),
          stroke: l.color,
          width: 1.5,
          points: { show: false },
        })),
      ],
      fitted: heldAxis(held),
      ySize: () => held.ySize,
      // Only the bottom chart carries the time labels.  Repeating them
      // under every plot costs the height that makes the stack worth
      // having, and says the axes might differ when the whole point is
      // that they cannot.
      last: n === read.drawn.length - 1,
      yStroke: style.yStroke ?? TRACE,
      font: style.font,
      // No per-chart legend unless asked for.  With the cursor synced
      // there is one instant being pointed at, not one per plot, so the
      // values belong in one row, which the page builds if it wants one.
      legend: style.legend ?? { show: false },
      sync: style.sync,
      hooks: style.hooks,
    });
    const chart = ranged(new uPlot(opts, stackData(read, drawing), host));
    chart.smartclockShape = stackShape(drawing, fits[n]);
    chart.smartclockAxis = held;
    // Which lines this chart draws, and from which series, so a shared
    // readout can label them without reaching back into the request
    // that built it.  A receiver's line's color is said only where
    // there are several to tell apart; a sensor's always, since it is
    // that sensor's on every chart.  A sensor's line is read out with its
    // quantity and unit, which its chart's title says but the readout
    // does not.
    chart.smartclockLines = drawing.lines.map((l, i) => ({
      column: l.column,
      name: l.name,
      reading: l.reading,
      series: 1 + (drawing.envelope ? 2 : 0) + i,
      color: several || l.name ? l.color : null,
    }));
    if (drawing.hint) hintAs(chart, drawing.hint);
    else hint(chart, ...drawing.lines.map((l) => l.column));
    // uPlot's title is plain text, and with the legend off a chart of
    // several lines otherwise names them without saying which is which.
    if (several && !opts.legend.show) {
      const heading = chart.root.querySelector(".u-title");
      if (heading) heading.innerHTML = groupTitle(drawing.lines);
    }
    return chart;
  });
  settleWidths(charts, container);
  return charts;
}

// What a chart of the stack plots: its time axis, then its series.
const stackData = (read, drawing) => [drawing.x ?? read.x, ...drawingArrays(drawing)];

// The stack `charts`, drawn in `container`, brought up to `read`: each
// chart given its new data, y axis and size in place while the stack
// keeps its shape -- the same charts and lines -- so a page read again
// on a timer neither flashes nor moves, and drawn afresh when it does
// not.  Answers the charts, the same ones or new.
function restack(container, charts, read, style) {
  const height = style.height(read.drawn.length);
  const fits = stackFits(read, height);
  const ySize = stackWidth(fits);
  const kept = charts.length === read.drawn.length &&
    charts.every((c, n) => c.smartclockShape === stackShape(read.drawn[n], fits[n]));
  if (kept) {
    charts.forEach((c, n) => {
      const held = c.smartclockAxis;
      const widened = held.ySize !== ySize;
      held.fit = fits[n];
      held.ySize = ySize;
      c.setData(stackData(read, read.drawn[n]));
      const size = {
        width: container.clientWidth || c.width,
        height: stackChartHeight(height, n, charts.length, style.font),
      };
      if (size.width !== c.width || size.height !== c.height) c.setSize(size);
      // A new width alone changes no scale, which is what uPlot measures
      // its axes again on.
      else if (widened) c.redraw(false, true);
    });
    return charts;
  }
  hold(container);
  for (const c of charts) c.destroy();
  container.innerHTML = "";
  const drawn = stackCharts(container, read, style);
  release(container);
  return drawn;
}

// -------------------------------------------------------------- sensors
//
// The host's sensors, from the sensor service's log: a chart per
// quantity, a line per sensor, joined onto the receivers' bucket grid
// so a moment sits at the same place as on the receivers' charts.

// The quantities, in the order their charts stack.
const SENSOR_QUANTITIES = ["temperature", "humidity", "pressure"];
// Sensor line colors, none of them a receiver's on the compare page or
// a note's.
const SENSOR_COLORS = ["#2dd4bf", "#fb923c", "#e2e8f0", "#84cc16"];

// The sensor log's sensors; none when there is no log or it cannot be
// read, since a host without sensors is not a fault.
async function sensorsListed(signal) {
  try {
    return await ask("/api/sensors", signal);
  } catch (e) {
    if (e instanceof Superseded) throw e;
    return { sensors: [], every_s: null, first: null, last: null };
  }
}

// Every sensor's line of `quantity` over `win`, as the server buckets
// it: [{ name, at, values }].
async function sensorHistory(quantity, win, signal) {
  const q = new URLSearchParams({
    quantity,
    from: win.from,
    to: win.to,
    points: win.points ?? pointsFor(win.to - win.from),
  });
  return ask("/api/sensors/history?" + q, signal, HISTORY_TIMEOUT);
}

// Each quantity the log holds, its sensors' lines over `win` snapped to
// the grid: [{ quantity, lines: [{ name, at, values, source, device }] }].
// A quantity whose read fails is left out rather than failing the page.
async function sensorGroups(listed, win, signal) {
  const quantities = SENSOR_QUANTITIES.filter((q) => listed.sensors.some((s) => s.quantity === q));
  const groups = await Promise.all(
    quantities.map(async (quantity) => {
      let lines;
      try {
        lines = await sensorHistory(quantity, win, signal);
      } catch (e) {
        if (e instanceof Superseded) throw e;
        return null;
      }
      return {
        quantity,
        lines: lines.map((line) => {
          const sensor = listed.sensors.find((s) => s.name === line.name && s.quantity === quantity);
          return {
            ...line,
            at: onGrid(line.at, win),
            source: sensor?.source,
            device: sensor?.device,
            unit: sensor?.unit,
            color: sensorColor(listed, line.name),
          };
        }),
      };
    }),
  );
  return groups.filter((g) => g && g.lines.length);
}

// A sensor chart's title, of `quantity` read by the sensors `names`:
// the one sensor's name, or for several the quantity's own, which
// their colors then name.
const sensorTitle = (quantity, names) =>
  names.length === 1
    ? `${names[0].charAt(0).toUpperCase()}${names[0].slice(1)} ${SENSOR_NAMES[quantity]}`
    : label(`sensor:${quantity}`);

// A sensor reading's name where its chart's title does not say the
// quantity and unit: one sensor can read several quantities.
const sensorReadingName = (name, quantity, unit) => `${name} ${quantity}, ${unit}`;

// A sensor's color, by its name among every sensor's, so one name is one
// color on every chart it is on.
function sensorColor(listed, name) {
  const names = [...new Set(listed.sensors.map((s) => s.name))].sort();
  return SENSOR_COLORS[Math.max(0, names.indexOf(name)) % SENSOR_COLORS.length];
}

// A sensor chart's hover text: where each of its sensors is read from.
function sensorHint(lines) {
  return lines
    .map((l) => `${l.name}: ${l.source ?? "unknown"}${l.device ? ` (${l.device})` : ""}`)
    .join("; ");
}

// ---------------------------------------------------------------- notes

// How near the cursor must be to a note's line, in CSS pixels, for its
// text to be shown.
const NOTE_REACH = 6;

// Each note's line, dashed across the plot area at its time, in its
// own color.  `marks` is a list of `{ at, text, color }`, `at` in
// unix seconds.
function drawNoteLines(u, marks) {
  const [lo, hi] = [u.scales.x.min, u.scales.x.max];
  const { ctx } = u;
  ctx.save();
  ctx.lineWidth = devicePixelRatio;
  ctx.setLineDash([4 * devicePixelRatio, 4 * devicePixelRatio]);
  for (const { at, color } of marks) {
    if (at < lo || at > hi) continue;
    const x = Math.round(u.valToPos(at, "x", true));
    ctx.strokeStyle = color;
    ctx.beginPath();
    ctx.moveTo(x, u.bbox.top);
    ctx.lineTo(x, u.bbox.top + u.bbox.height);
    ctx.stroke();
  }
  ctx.restore();
}

// The text of the note in `marks` under the cursor, if one is within
// reach.
function noteAt(u, marks) {
  if (u.cursor.left == null || u.cursor.left < 0) return null;
  const near = marks.find(({ at }) => Math.abs(u.valToPos(at, "x") - u.cursor.left) <= NOTE_REACH);
  return near ? near.text : null;
}

// How far the note's text sits from the pointer, and how near the
// window's edge it may come, in CSS pixels.
const TIP_OFFSET = 12;
const TIP_MARGIN = 4;

// A note's text beside the pointer on `chart`, in the page's
// `#note-tip`; hidden when there is none.  It goes below and to the
// right of the pointer, or to the left and above where that would
// leave the window, so a note near the right edge can still be read.
function noteTip(chart, text) {
  const tip = $("note-tip");
  if (!chart || text == null) {
    tip.style.display = "none";
    return;
  }
  const box = chart.over.getBoundingClientRect();
  const x = box.left + chart.cursor.left;
  const y = box.top + chart.cursor.top;
  tip.textContent = text;
  tip.style.display = "block";
  const { width, height } = tip.getBoundingClientRect();
  const right = x + TIP_OFFSET;
  const below = y + TIP_OFFSET;
  const left = right + width > innerWidth - TIP_MARGIN ? x - TIP_OFFSET - width : right;
  const top = below + height > innerHeight - TIP_MARGIN ? y - TIP_OFFSET - height : below;
  tip.style.left = `${Math.max(TIP_MARGIN, left)}px`;
  tip.style.top = `${Math.max(TIP_MARGIN, top)}px`;
}

// ----------------------------------------------------------- log axes

// Both axes are decades, which is the only way this curve is read: the
// slope between decades is what names the noise.
const LOG = { distr: 3, log: 10 };

// An axis ends on a named graduation -- 1, 2 or 5 in some decade --
// at or beyond the data, so the ends of the axis are labeled and a
// curve that starts at 10.0004 s (the receiver's ten-second cadence
// is not exactly ten) still gets its graduation at 10.  uPlot's own
// log range rounds out to whole decades, which for a curve ending
// just past 10⁻⁹ leaves most of a decade empty above it.
// A value within a percent of a graduation counts as on it: the
// last tau of a run is 1000.04 s, not 1000, and an axis to 2000 for
// that would be most of a decade of nothing.
const STEPS = [1, 2, 5, 10];
const SNAP = 0.01;
function snapDown(v) {
  const e = Math.floor(Math.log10(v) + 1e-9);
  const m = v / 10 ** e;
  return [...STEPS].reverse().find((s) => s <= m * (1 + SNAP)) * 10 ** e;
}
function snapUp(v) {
  const e = Math.floor(Math.log10(v) - 1e-9);
  const m = v / 10 ** e;
  return STEPS.find((s) => s >= m * (1 - SNAP)) * 10 ** e;
}
const TAU_SCALE = { ...LOG, range: (_, min, max) => [snapDown(min), snapUp(max)] };

// uPlot's axis text defaults to 12px, which is small for numbers that
// are read off, not just glanced at.
const AXIS_FONT = "14px system-ui, sans-serif";

// A value as a mantissa and a power, for the readout and the table,
// where the exact figure matters and a bare decade would not do.
function decade(v) {
  if (!Number.isFinite(v) || v <= 0) return "--";
  const exponent = Math.floor(Math.log10(v));
  const mantissa = v / 10 ** exponent;
  return `${mantissa.toFixed(mantissa < 10 ? 1 : 0)}×${tenTo(exponent)}`;
}

// A log ruler: a graduation at every 1 to 9 of each decade, so the
// spacing between them shows the scale is logarithmic and a point can
// be read off between labels.  uPlot's own splits label 1, 2 and 5 of
// each decade, which is what made the axis look busy -- the answer is
// fewer labels, not fewer graduations, so only the decades are named.
//
// A span too narrow to hold two decades keeps uPlot's own splits,
// rather than being given an axis with one mark on it.
function graduations(u, axis, min, max) {
  const first = Math.floor(Math.log10(min) - 1e-9);
  const last = Math.ceil(Math.log10(max) + 1e-9);
  const out = [];
  for (let e = first; e <= last; e++) {
    for (let m = 1; m < 10; m++) {
      const v = m * 10 ** e;
      if (v >= min * (1 - 1e-9) && v <= max * (1 + 1e-9)) out.push(v);
    }
  }
  return out.length > 1 ? out : null;
}

// The mantissa of a graduation, 1 to 9, as an integer.  Found from the
// rounded exponent rather than by dividing, because 1e-9 divided out
// does not land exactly on 1.
function mantissa(v) {
  const e = Math.floor(Math.log10(v) + 1e-9);
  return Math.round(v / 10 ** e);
}

// Both axes name 1, 2 and 5 in each decade: a curve spanning two
// decades has only three decade lines, and the reader is left
// counting grid lines to place 50 s or 2 × 10⁻¹⁰.  An axis spanning
// less than that has too few of those, and names every graduation.
function labels(splits, format) {
  const sparse = splits.filter((v) => [1, 2, 5].includes(mantissa(v)));
  const name = sparse.length >= 3 ? (v) => sparse.includes(v) : () => true;
  return splits.map((v) => (name(v) ? format(v) : null));
}

// A decade of sigma_y, which is dimensionless and always small.
// Written 10 to the power rather than as 1e-9, which is a programming
// language's spelling of a number rather than a physicist's.
const SUPERSCRIPT = { "-": "⁻", 0: "⁰", 1: "¹", 2: "²",
                      3: "³", 4: "⁴", 5: "⁵", 6: "⁶",
                      7: "⁷", 8: "⁸", 9: "⁹" };
// Ten to the power `exponent`, written with superscripts.
const tenTo = (exponent) => "10" + [...String(exponent)].map((c) => SUPERSCRIPT[c]).join("");

// A graduation as mantissa and power, the mantissa written even when it
// is 1, so 1×10⁻⁹ reads in line with 2×10⁻⁹ and 5×10⁻⁹ beside it.
function power(v) {
  if (!Number.isFinite(v) || v <= 0) return "";
  return `${mantissa(v)}×${tenTo(Math.floor(Math.log10(v) + 1e-9))}`;
}

// A graduation as the number itself: 1, 2, 5, 10, 20, 50.  Past six figures
// the exponent is shorter than the number and just as readable.
function plain(v) {
  if (!Number.isFinite(v) || v <= 0) return "";
  const exponent = Math.floor(Math.log10(v) + 1e-9);
  const m = mantissa(v);
  return exponent < 6 ? String(m * 10 ** exponent) : `${m}e${exponent}`;
}

function seconds(v) {
  if (!Number.isFinite(v)) return "--";
  if (v >= 86400) return `${(v / 86400).toFixed(v < 864000 ? 1 : 0)} d`;
  if (v >= 3600) return `${(v / 3600).toFixed(1)} h`;
  if (v >= 60) return `${(v / 60).toFixed(1)} m`;
  return `${v.toFixed(v < 10 ? 2 : 0)} s`;
}

// The tau axis, the same on both charts.
function tauAxis() {
  return {
    scale: "x",
    stroke: AXIS_INK,
    grid: { stroke: AXIS_RULE },
    label: "averaging time τ, seconds",
    labelSize: 28,
    font: AXIS_FONT,
    labelFont: AXIS_FONT,
    splits: graduations,
    // uPlot's own filter for a log axis keeps only the decades; which
    // graduations are named is decided in `values` instead.
    filter: (_, splits) => splits,
    values: (_, splits) => labels(splits, plain),
  };
}

function sigmaAxis(label) {
  return {
    scale: "y",
    stroke: AXIS_INK,
    grid: { stroke: AXIS_RULE },
    size: 72,
    label,
    labelSize: 28,
    font: AXIS_FONT,
    labelFont: AXIS_FONT,
    splits: graduations,
    filter: (_, splits) => splits,
    values: (_, splits) => labels(splits, power),
  };
}

// A y scale that ranges over the bands as well as the lines, so a wide
// interval at long tau is not cut off at the axis.  uPlot ranges from
// the series alone; the shading is not one.
function sigmaScale(bands) {
  const values = Object.values(bands).flat(2).filter((v) => v !== null && v > 0);
  const lo = Math.min(...values), hi = Math.max(...values);
  return {
    ...LOG,
    range: (_, min, max) => [snapDown(Math.min(min, lo)), snapUp(Math.max(max, hi))],
  };
}

// `sigmaScale` over `held.bands`, read each time the chart draws, so a
// chart can be given new bands in place.
const heldSigmaScale = (held) => ({ ...LOG, range: (u, min, max) => sigmaScale(held.bands).range(u, min, max) });

// The one-sigma interval on each curve, drawn as a shaded band between
// its lower and upper bounds.  Painted by hand rather than as two more
// uPlot series with a fill between them: series appear in the legend
// and take a color, and a band is not a curve.  `bands` maps a series
// index to its [lower, upper] arrays; a point without an interval is a
// break in the band, as it is in the line.
function shadeBands(bands) {
  return (u) => {
    const { ctx } = u;
    ctx.save();
    ctx.beginPath();
    ctx.rect(u.bbox.left, u.bbox.top, u.bbox.width, u.bbox.height);
    ctx.clip();
    for (const [idx, [lower, upper]] of Object.entries(bands)) {
      const series = u.series[Number(idx)];
      if (!series.show) continue;
      ctx.fillStyle = series.stroke() + "30";
      const xs = u.data[0];
      let run = [];
      const flush = () => {
        if (run.length < 2) {
          run = [];
          return;
        }
        ctx.beginPath();
        run.forEach(([x, hi], i) => {
          const px = u.valToPos(x, "x", true), py = u.valToPos(hi, "y", true);
          if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
        });
        for (let i = run.length - 1; i >= 0; i--) {
          const [x, , lo] = run[i];
          ctx.lineTo(u.valToPos(x, "x", true), u.valToPos(lo, "y", true));
        }
        ctx.closePath();
        ctx.fill();
        run = [];
      };
      xs.forEach((x, i) => {
        if (lower[i] === null || upper[i] === null) flush();
        else run.push([x, upper[i], lower[i]]);
      });
      flush();
    }
    ctx.restore();
  };
}

const bound = (b, side) => (b ? b[side] : null);

// The curves of one `/api/adev` answer's points, each ending where its
// band grows too wide (MAX_BAND):
//   tau                    the averaging times
//   modified, plain        MDEV and ADEV, dimensionless
//   time, mtie             TDEV and MTIE, in seconds
//   sigmaBands, timeBands  `shadeBands`' bands for the sigma chart's
//                          series and for TDEV's
// A point the modified form lacks (it needs three whole windows) is a
// hole in its series, which uPlot leaves as a break in the line.
function deviationCurves(points) {
  const modifiedBounds = points.map((p) => p.modified?.bounds);
  const band = (pick) => {
    const bounds = points.map(pick);
    return [
      untilWide(bounds.map((b) => bound(b, "lower")), bounds),
      untilWide(bounds.map((b) => bound(b, "upper")), bounds),
    ];
  };
  return {
    tau: points.map((p) => p.tau),
    modified: untilWide(points.map((p) => (p.modified ? p.modified.deviation : null)), modifiedBounds),
    time: untilWide(points.map((p) => (p.modified ? p.modified.time : null)), modifiedBounds),
    plain: untilWide(points.map((p) => p.deviation), points.map((p) => p.bounds)),
    mtie: points.map((p) => (p.mtie ? p.mtie.peak : null)),
    sigmaBands: { 1: band((p) => p.modified?.bounds), 2: band((p) => p.bounds) },
    timeBands: { 1: band((p) => p.modified?.time_bounds) },
  };
}

// MDEV and ADEV on one chart in `el`, shaded to one sigma.  The
// modified deviation is the one this record supports exactly -- the
// receiver's reading is already a ten-second mean, which is the
// innermost block of the modified form's own averaging; the plain
// Allan deviation is kept because data sheets quote that one.  Both
// are measured curves and both are drawn at full strength, told apart
// by color and legend: a dashed or dimmed line reads as a reference or
// a fit.  `opts` is
//   show            { MDEV, ADEV }, whether each is drawn
//   legend, cursor  uPlot's
//   hooks           uPlot's, beside the shading
//   font            the axes' font, when not AXIS_FONT
// Its bands are kept on it, as `smartclockBands`, so `resigma` can
// give it new curves in place.
function sigmaChart(el, size, curves, opts) {
  const font = opts.font ? { font: opts.font, labelFont: opts.font } : {};
  const held = { bands: curves.sigmaBands };
  const curve = (name, stroke) => ({
    label: name,
    stroke,
    width: 1.5,
    show: opts.show[name],
    points: { show: true, size: 5 },
    value: (_, v) => decade(v),
  });
  const chart = new uPlot(
    {
      width: size.width,
      height: size.height,
      title: "Stability of the 1 PPS against GPS, shaded to one sigma",
      scales: {
        x: TAU_SCALE,
        y: heldSigmaScale(held),
      },
      axes: [{ ...tauAxis(), ...font }, { ...sigmaAxis("deviation σy"), ...font }],
      series: [
        { label: "τ", value: (_, v) => seconds(v) },
        curve("MDEV", "#38bdf8"),
        curve("ADEV", "#a78bfa"),
      ],
      legend: opts.legend,
      cursor: opts.cursor,
      hooks: { ...opts.hooks, draw: [(u) => shadeBands(held.bands)(u)] },
    },
    sigmaData(curves),
    el,
  );
  chart.smartclockBands = held;
  return chart;
}

const sigmaData = (curves) => [curves.tau, curves.modified, curves.plain];

// Give a chart `sigmaChart` drew new curves, in place.
function resigma(chart, curves) {
  chart.smartclockBands.bands = curves.sigmaBands;
  chart.setData(sigmaData(curves));
}

// ---------------------------------------------------------- correlation
//
// Statistics over two series on one regular grid of buckets, a null
// where a bucket holds nothing.  Only buckets where both have a value
// count.

// Fewest pairs a statistic is given for.
const MIN_PAIRS = 3;

// The pairs where both have a value: [[x, y], ...].
function paired(xs, ys) {
  const pairs = [];
  for (let i = 0; i < xs.length && i < ys.length; i++) {
    if (xs[i] != null && ys[i] != null) pairs.push([xs[i], ys[i]]);
  }
  return pairs;
}

// Pearson's r, or null for too few pairs or a series that never moves.
function pearson(xs, ys) {
  const pairs = paired(xs, ys);
  if (pairs.length < MIN_PAIRS) return null;
  const n = pairs.length;
  const mx = pairs.reduce((s, p) => s + p[0], 0) / n;
  const my = pairs.reduce((s, p) => s + p[1], 0) / n;
  let sxy = 0, sxx = 0, syy = 0;
  for (const [x, y] of pairs) {
    sxy += (x - mx) * (y - my);
    sxx += (x - mx) ** 2;
    syy += (y - my) ** 2;
  }
  return sxx > 0 && syy > 0 ? sxy / Math.sqrt(sxx * syy) : null;
}

// The least-squares line y = slope * x + intercept, or null.
function leastSquares(xs, ys) {
  const pairs = paired(xs, ys);
  if (pairs.length < MIN_PAIRS) return null;
  const n = pairs.length;
  const mx = pairs.reduce((s, p) => s + p[0], 0) / n;
  const my = pairs.reduce((s, p) => s + p[1], 0) / n;
  let sxy = 0, sxx = 0;
  for (const [x, y] of pairs) {
    sxy += (x - mx) * (y - my);
    sxx += (x - mx) ** 2;
  }
  if (!(sxx > 0)) return null;
  const slope = sxy / sxx;
  return { slope, intercept: my - slope * mx };
}

// Each bucket's change from the one before, null unless both have a
// value.  Correlated changes are what is left of a relationship once
// two slow drifts that merely happen together are taken away.
function changes(values) {
  return values.map((v, i) => (i > 0 && v != null && values[i - 1] != null ? v - values[i - 1] : null));
}

// r with `ys` moved `lag` buckets against `xs`, for every lag out to
// `reach` either way: [[lag, r], ...].  A positive lag pairs each x with
// the y that many buckets later, so a peak there means x leads.
function lagCurve(xs, ys, reach) {
  const curve = [];
  for (let lag = -reach; lag <= reach; lag++) {
    const shifted = xs.map((_, i) => ys[i + lag] ?? null);
    curve.push([lag, pearson(xs, shifted)]);
  }
  return curve;
}
