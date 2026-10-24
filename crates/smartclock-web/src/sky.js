// The sky plot, drawn from a status screen's satellite table: on the
// status page, and on the kiosk from the screen the daemon last read.
// Loaded after common.js.
"use strict";

// Sizes below are for a plot this many pixels across, and scale with it.
const DESIGN_SIZE = 320;
const MARGIN = 20;      // room for the cardinal labels and a rim dot
const LABEL_PX = 11;    // label text
const SKY_GRID = "#39414f";
const SKY_HORIZON = "#515b6b";
const DOT_MIN = 4;      // a satellite with no signal reading
const DOT_RANGE = 7;    // added at the strongest signal in the frame

// A polar plot of what the receiver can see: north up, horizon at the
// rim, zenith at the middle.  This is the view the status screen's
// table implies and never shows.  Drawn at the size the page gives it,
// in device pixels, so it stays sharp when it grows to the screen's
// height, with its labels and dots growing in proportion.
function drawSky(c, screen) {
  const ctx = c.getContext("2d");
  const side = c.clientWidth, dpr = window.devicePixelRatio || 1;
  const k = side / DESIGN_SIZE;
  c.width = c.height = Math.round(side * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const w = side, h = side, cx = w / 2, cy = h / 2, r = Math.min(cx, cy) - MARGIN * k;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#c2c8d2";
  ctx.font = `${LABEL_PX * k}px ui-monospace, monospace`;
  ctx.textAlign = "center";

  // Elevation rings at 30 degree steps, the horizon last and brighter
  // so the edge of the plot reads as an edge.
  for (const elev of [60, 30, 0]) {
    ctx.strokeStyle = elev === 0 ? SKY_HORIZON : SKY_GRID;
    ctx.beginPath();
    ctx.arc(cx, cy, (r * (90 - elev)) / 90, 0, 2 * Math.PI);
    ctx.stroke();
  }
  ctx.strokeStyle = SKY_GRID;
  for (const [deg, name] of [[0, "N"], [90, "E"], [180, "S"], [270, "W"]]) {
    const a = ((deg - 90) * Math.PI) / 180;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + r * Math.cos(a), cy + r * Math.sin(a));
    ctx.stroke();
    const out = r + (MARGIN / 2) * k;
    ctx.fillText(name, cx + out * Math.cos(a), cy + out * Math.sin(a) + (LABEL_PX / 3) * k);
  }

  const signals = (screen?.satellites ?? [])
    .map((s) => s.signal)
    .filter((v) => v != null && v > 0);
  const strongest = signals.length ? Math.max(...signals) : 0;

  for (const sat of screen?.satellites ?? []) {
    // Below the horizon, or a reading the scraper could not make sense
    // of: either would land outside the plot.
    if (
      !Number.isFinite(sat.elevation) ||
      !Number.isFinite(sat.azimuth) ||
      sat.elevation < 0
    ) {
      continue;
    }
    const a = (sat.azimuth - 90) * Math.PI / 180;
    const d = r * (90 - sat.elevation) / 90;
    const x = cx + d * Math.cos(a), y = cy + d * Math.sin(a);
    // Scaled to the strongest satellite in this frame, because the
    // receiver's own scale is undocumented and differs between models
    // -- one reports 0 to 9, another 0 to 49.  Dividing by a fixed 255
    // made every dot the same size, which is what this looked like
    // before anyone measured it.
    const size =
      sat.signal == null || !strongest
        ? DOT_MIN * k
        : (DOT_MIN + (sat.signal / strongest) * DOT_RANGE) * k;
    ctx.beginPath();
    ctx.arc(x, y, size, 0, 2 * Math.PI);
    ctx.fillStyle = sat.tracked ? "#4ade80" : sat.acquiring ? "#fbbf24" : "#6b7280";
    ctx.fill();
    ctx.fillStyle = "#e6e8eb";
    ctx.textAlign = x < cx ? "right" : "left";
    const gap = size + (LABEL_PX / 4) * k;
    ctx.fillText(sat.prn, x + (x < cx ? -gap : gap), y + (LABEL_PX / 3) * k);
    ctx.textAlign = "center";
  }
}
