"""Renders the illustrated explainer of the five SmartClock FPGA designs,
fpga.html, into --output.  It is not committed: the top Makefile's `fpga`
target writes it for GitHub Pages
(https://charlieh0tel.github.io/smartclock/fpga.html), and
check-explainer, part of tools/fpga/Makefile's `check`, fails if it
cannot be rendered.

    python3 -I tools/fpga/explainer.py --figures target/fpga --output PAGE
    python3 -I tools/fpga/explainer.py --figures-used

The page states the figures the Markdown traces state, and takes each
from the file that is checked against a fresh run, through page.py,
which the FPGA pages share: a design's size from
tools/fpga/ref/<image>.stats (`make -f tools/fpga/Makefile check`), a
simulated figure from tools/fpga/ref/<image>.<run>.readings
(`check-readings`), the decoder's pin from tools/fpga/decoder.mk.  A
pattern that no longer matches its file is an error, not a blank.  Its
waveforms are vcd_wave.py's SVGs, which the Makefile renders under
--figures from the VCDs of the runs FIGURES names, inlined.  The page
is one file with no script and nothing loaded from elsewhere; links to
the repository's files go to GitHub, without a fragment.  The head
carries the title, description and smartclock-group meta tags the
Pages site's landing page lists it by (tools/pages_index.py on main).
"""

import argparse
import html
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import (DESIGNS, STYLE, arrow, box, decoder_commit, die, divide, document,  # noqa: E402
                  edge_periods, find, find_all, link, mark, markers, ns, num, probe, stats, trace)

# The waveform figures the page inlines, by their path under --figures,
# each a spec tools/fpga/waves/<path>.json.
FIGURES = ("z3805a/read", "z3815a/p7-divider", "z3815a.freerun/phase-detector")

# The telecom pair's P7 divide and its 1 PPS chain's GCLK prescale
# (z3815a-trace.md, "The P7 divider: P7 / 6250", "The 1 PPS divider").
P7_DIVIDE = 6250
PRESCALE = 6144


def facts():
    """Every figure the page states, read from its checked file."""
    out = {"commit": decoder_commit(), "designs": []}
    for design in DESIGNS:
        image = design.image
        table = stats(image)
        p83, ratio = divide(image)
        out["designs"].append(dict(
            image=image, name=design.name, images=design.images, tap_net=design.tap,
            blocks=int(table["clb_used"]), iobs=int(table["iob_used"]), nets=int(table["nets"]),
            clocks=int(table["clocks"]),
            preset=find(f"{image}.fast", r"the count reads ([0-9a-f]{6})$")[0],
            status=find(f"{image}.fast", r"^[\d.]+ s: status ([0-9a-f]{2})$")[0],
            p83=p83, divide=ratio,
            tap=probe(f"{image}.three", design.tap)["after_min"]))
    out["z3805a_count"] = int(find("z3805a.three", r"less the preset (\d+) ticks")[0])
    out["z3815a_counts"] = [int(v) for v in find_all("z3815a.three", r"less the preset (\d+) ticks")]
    out["sync1"] = int(find("z3805a.sync1", r"^1\.\d+ s: count \w+, less the preset (\d+) ticks")[0])
    out["sync2"] = int(find("z3805a.sync2", r"^1\.\d+ s: count \w+, less the preset (\d+) ticks")[0])
    out["sync3_p83"] = find("z3805a.sync3", r"^([\d.]+) s: P83 rises$")[0]
    out["telecom_sync3"] = probe("z3815a.sync3", "P83/GPS")["after_min"]
    out["p38"] = edge_periods("z3815a.freerun", "P38")
    out["p40"] = edge_periods("z3815a.freerun", "P40")
    lead = probe("z3815a.freerun", "LD.Y")
    out["p40_lead"] = (lead["high_min"], lead["high_max"])
    out["pll_p38"] = edge_periods("z3815a.pll", "P38")
    out["pll_p40"] = edge_periods("z3815a.pll", "P40")
    out["pll_phase"] = probe("z3815a.pll", "LE.X")["high_min"]
    out["p7_130_p40"] = edge_periods("z3815a.p7_130", "P40")
    out["ke"] = probe("z3815a.freerun", "KE.Y")["period_min"]
    out["fe"] = probe("z3815a.freerun", "FE.Y")["period_min"]
    ee = probe("z3815a.freerun", "EE.Y")
    out["ee"] = (ee["period_min"], ee["period_max"])
    out["alternate"] = find_all("z3815a.freerun_4s", r"^probe ~P14 high ([\d.]+) ns from ([\d.]+) s$")
    out["p83_4s"] = int(find("z3815a.freerun_4s", r"^rises: P83 (\d+)")[0])
    out["p20_lead"] = probe("z3815a.three", "P83")["after_min"]
    return out


# --- rendering ------------------------------------------------------------

def figure(figures, name, caption):
    path = figures / f"{name}.svg"
    if not path.exists():
        die(f"no {path}; make -f tools/fpga/Makefile figures renders it")
    svg = path.read_text(encoding="utf-8").strip()
    return f'<figure class="fig" data-scroll="x">\n{svg}\n<figcaption>{caption}</figcaption>\n</figure>'


def diagram(f):
    """The two families' clock and count paths, drawn from the traces."""
    ten = f["designs"][0]
    out = ['<svg viewBox="0 0 900 430" role="img" xmlns="http://www.w3.org/2000/svg" '
           'aria-label="The count and the 1 PPS divider in the two families">',
           markers(),
           '<text class="dg-h" x="0" y="18">Z3805A, 58503A/Z3801A, 58503B</text>']
    out += box(0, 36, 120, 48, "P7", "10 MHz in")
    out += box(170, 36, 190, 48, "1 PPS divider", f"/ {num(ten['divide'])}")
    out += box(410, 36, 150, 48, "internal 1 PPS", "P83")
    out += box(170, 112, 190, 48, "tap", "stops the count")
    out += box(410, 112, 150, 48, "24-bit count", "start ... stop")
    out += box(610, 112, 130, 48, "register port", "the CPU reads")
    out += box(410, 188, 150, 40, "GPS 1 PPS, P82", "")
    out += [arrow([(120, 60), (168, 60)]), arrow([(360, 60), (408, 60)]),
            arrow([(265, 84), (265, 110)]), arrow([(360, 136), (408, 136)]),
            arrow([(485, 188), (485, 162)]), arrow([(560, 136), (608, 136)])]
    out.append('<text class="dg-h" x="0" y="262">Z3815A, Z3816A</text>')
    out += box(0, 280, 120, 48, "P53, ACLK", "19.6608 MHz")
    out += box(170, 280, 130, 48, "prescaler", "ACLK / 12,288")
    out += box(340, 280, 150, 48, "GJ.Y, on GCLK", "GCLK = ACLK / 2")
    out += box(530, 280, 130, 48, "/ 1600", "1 PPS, P83")
    out += box(700, 280, 130, 48, "count, port", "as above")
    out += box(0, 370, 120, 48, "P7", "10 MHz")
    out += box(170, 370, 130, 48, "P7 divider", "/ 6250")
    out += box(340, 370, 150, 48, "phase detector", "P38, P40")
    out += box(530, 370, 170, 48, "loop on the board", "steers P53", inferred=True)
    out += [arrow([(120, 304), (168, 304)]), arrow([(300, 304), (338, 304)]),
            arrow([(490, 304), (528, 304)]), arrow([(660, 304), (698, 304)]),
            arrow([(120, 394), (168, 394)]), arrow([(300, 394), (338, 394)]),
            arrow([(415, 330), (415, 368)]), arrow([(490, 394), (528, 394)], inferred=True),
            arrow([(615, 368), (615, 340), (60, 340), (60, 330)], inferred=True)]
    out.append("</svg>")
    return '<figure class="fig" data-scroll="x">\n' + "\n".join(out) + (
        "\n<figcaption>The count and the 1 PPS divider in the two families "
        f"({mark('read')} from the netlists).  The second row's frequencies are what the ratios imply, "
        f"{mark('inferred')}.  Dashed: the loop the telecom pair's phase "
        f"detector implies, {mark('inferred')}; no board has been probed.  The arrow from "
        "the detector's side back to P53 is that loop, not a path inside the FPGA."
        "</figcaption>\n</figure>")


def designs_table(f):
    rows = []
    for d in f["designs"]:
        rows.append(
            "<tr>"
            f'<th scope="row"><a href="fpga-detail.html#{d["image"]}">{html.escape(d["name"])}</a></th>'
            f"<td>{html.escape(d['images'])}</td>"
            f'<td>{d["blocks"]}, {d["iobs"]}, {d["nets"]}</td>'
            f'<td class="short">{d["clocks"]}</td>'
            f'<td class="short">{num(d["divide"])}</td>'
            f'<td><code>0x{d["preset"]}</code>, <code>0x{d["status"]}</code></td>'
            f'<td class="short">{num(d["tap"])} ns</td>'
            f'<td class="short">{trace(d["image"])}</td>'
            "</tr>")
    return ('<div class="wide" data-scroll="x"><table class="designs">\n<thead><tr>'
            "<th>Design</th><th>Firmware images</th><th>Logic blocks, I/O blocks, nets</th>"
            "<th>Clock buffers</th><th>Divide to the 1 PPS</th><th>Preset, status</th>"
            "<th>Tap after P83</th><th>Trace</th>"
            "</tr></thead>\n<tbody>\n" + "\n".join(rows) + "\n</tbody></table></div>")


def render(figures):
    f = facts()
    z3805a, z3815a = f["designs"][0], f["designs"][3]
    counts = f["z3815a_counts"]
    fall = counts[0] - counts[1]
    alternate = f["alternate"]
    p38, p40 = f["p38"], f["p40"]
    if len(p38) != 1 or len(f["pll_p38"]) != 1 or f["pll_p38"] != f["pll_p40"]:
        die(f"phase detector periods {p38}, {f['pll_p38']}, {f['pll_p40']}")
    sections = []

    sections.append(f"""<header>
<p class="eyebrow">SmartClock firmware &middot; FPGA</p>
<h1>Inside the SmartClock FPGAs</h1>
<p class="lede">The HP and Symmetricom SmartClock GPS receivers load a Xilinx XC3042
FPGA from their firmware.  The seven firmware images carry five bitstreams.  Each is
decoded here into a netlist, run under Icarus Verilog, and traced through its logic.  This
page is the short version, drawn from the same checked figures as the traces.</p>
<p class="note">Nothing here has been checked on a board.  Every pin's role is read
from the logic.  The confidence words: {mark('read')}, from the decoded netlist's
equations and connections; {mark('simulated')}, printed by a testbench under Icarus
Verilog and checked by <code>make check-readings</code>; {mark('inferred')}, reasoning
from those; {mark('open')}, not established.</p>
<nav><ol>
<li><a href="#job">What the FPGA does</a></li>
<li><a href="#designs">Five designs</a></li>
<li><a href="#count">The count</a></li>
<li><a href="#families">Two families</a></li>
<li><a href="#telecom">The telecom pair's second clock</a></li>
<li><a href="#sync">The sync field</a></li>
<li><a href="#outputs">Other outputs</a></li>
<li><a href="#method">How it was done</a></li>
<li><a href="#reproduce">Reproduce it</a></li>
</ol></nav>
</header>""")

    sections.append(f"""<section id="job">
<h2>What the FPGA does</h2>
<p>The FPGA is the receiver's time-interval counter.  It divides the oscillator down to
an internal 1 PPS, which leaves on P83.  The GPS engine's 1 PPS, on P82, starts a 24-bit
count, and a tap of the divider stops it.  The firmware reads the count through an
eight-bit register port, with an external interpolator's byte for the fraction of a
clock, and steers the oscillator to hold the interval steady
({link('docs/firmware/counter.md', 'counter.md')}).</p>
{diagram(f)}
</section>""")

    sections.append(f"""<section id="designs">
<h2>Five designs</h2>
<p>The sizes are each decode's ({mark('read')}, <code>tools/fpga/ref/&lt;image&gt;.stats</code>).
The divide, the preset and status reads and the tap are {mark('simulated')}: the divide is
P83's printed period over the testbench's 100 ns GCLK, and the tap is the stop net's
rise after P83 (each design's <code>check-readings-&lt;image&gt;.three</code> and
<code>.fast</code> runs).  Each design's name links to its detailed logic diagram on
<a href="fpga-detail.html">FPGA detail</a>.</p>
{designs_table(f)}
<p>The front door, {link('docs/firmware/fpga-comparison.md', 'fpga-comparison.md')},
has every pin, role and difference side by side.  Only the 58503A's and Z3801A's
bitstream is shared byte for byte.</p>
</section>""")

    sections.append(f"""<section id="count">
<h2>The count</h2>
<p>All five count the same way ({mark('read')}).  Bit 0 is a GCLK toggle gated by the
start and stop, and bit 1 is a single flip-flop.  Bits 2 to 23 are eleven two-bit
Johnson stages, each clocked by the one before on its falling edge.  That makes a ripple
counter with no carry net.  The preset resets every stage, and the read port then gives
<code>0x{z3805a['preset']}</code> in all five designs ({mark('simulated')}).  The read is linear:
<code>0x555555</code> plus the ticks counted, modulo 2<sup>24</sup>.</p>
<p>The firmware reads the 24 bits as six nibbles at addresses <code>0x12</code> to
<code>0x17</code>, the interpolator's byte complemented at <code>0x10</code> and
<code>0x11</code>, and the status at <code>0x00</code>: the stop bit in bit 0, the start bit in
bit 1.  In the Z3805A's three-second run each reading is {num(f['z3805a_count'])} ticks
over the preset ({mark('simulated')}, <code>check-readings-z3805a.three</code>).</p>
{figure(figures, "z3805a/read", "One register read on the Z3805A's port: the address written into register 1, "
        "the read, which returns <code>0x05</code>, and register 1 written back, drawn from the fast run's VCD by "
        "<code>tools/fpga/vcd_wave.py</code>: every edge where the simulator put it.")}
</section>""")

    sections.append(f"""<section id="families">
<h2>Two families</h2>
<p>The Z3805A, the 58503A/Z3801A and the 58503B run GCLK from P7, a 10 MHz input, and
divide it by {num(z3805a['divide'])} ({mark('simulated')}).  The stop tap comes
{num(z3805a['tap'])} ns after P83, four tenths of the second, in the Z3805A and the
58503A/Z3801A, and {num(f['designs'][2]['tap'])} ns after it in the 58503B, whose P83 comes a
register later.</p>
<p>The Z3815A and the Z3816A are a different design family.  They take P53 into the
alternate clock buffer, ACLK, and halve it into GCLK.  An ACLK prescaler gives one GCLK
tick in 6144 ({mark('read')}), and a GCLK chain divides that by 1600, which is
{num(z3815a['divide'])} in all ({mark('simulated')}).  That is one second of a 9.8304 MHz
GCLK, so P53 would run at 19.6608 MHz, the CDMA clock, 16 x 1.2288 Mcps
({mark('inferred')}).  The tap comes 3,932,160 GCLK clocks after the internal 1 PPS, 0.4 of
that divide, and the firmware's calibration of 0.3932162 s is that tap plus two ticks on its
own 100 ns scale.</p>
<p>The testbenches run GCLK at 10 MHz, so the telecom pair's internal 1 PPS comes every
{num(z3815a['p83'])} ns, and the Z3815A's three readings fall
{num(fall)} ticks a second: {', '.join(num(c) for c in counts)} ({mark('simulated')},
<code>check-readings-z3815a.three</code>).  At 9.8304 MHz the second would be true and the
count would hold ({mark('inferred')}).</p>
</section>""")

    sections.append(f"""<section id="telecom">
<h2>The telecom pair's second clock</h2>
<p>P7 clocks a second divider in the Z3815A and the Z3816A ({mark('read')}).  JE and KE
are a nominal /5 prescaler, and an 11-bit counter across six logic blocks after it counts
to 1249 and clears: P7 / 6250
in all, with nothing decimal in it.  With P7 at 100 ns the prescaler's enable KE.Y comes
every {num(f['ke'])} ns, FE.Y every {num(f['fe'])} ns, and the counter's bit 3, EE.Y,
every {num(f['ee'][0])} to {num(f['ee'][1])} ns ({mark('simulated')},
<code>check-readings-z3815a.freerun</code>).</p>
{figure(figures, "z3815a/p7-divider", "The Z3815A's P7 divider in the fast run's VCD.  KE.Y is High one P7 clock "
        "in five.  The thin spikes on it are the model's one-nanosecond hazards, which "
        "<code>probe.v</code> removes before it measures.")}
<p>The divider's terminal and the 1 PPS chain's own GCLK / 6144 tick, GJ.Y, set two
flip-flops, LE (P38) and LD (P40), which clear each other once both are set.  That is a
two-flip-flop phase-frequency detector ({mark('read')}).  Whichever side sets first stays
High until the other sets, and when one side sets twice before the other once, the
second set is lost.  With 10 MHz on P7 and 19.6608 MHz on P53 both sides run at 1.6 kHz:
10 MHz / 6250 = 19.6608 MHz / 12,288.  So the pins are the phase detector of a loop that
locks the 19.6608 MHz oscillator to the 10 MHz reference ({mark('inferred')}).</p>
{figure(figures, "z3815a.freerun/phase-detector", "The Z3815A's detector in a 2 ms free run with P7 as GCLK's twin.  "
        f"P38's side sets every {num(p38[0])} ns and P40's every {num(p40[0])} ns, so P40 leads, "
        f"and its High time, the phase difference, grows from {num(f['p40_lead'][0])} to "
        f"{num(f['p40_lead'][1])} ns ({mark('simulated')}, <code>check-readings-z3815a.freerun</code>).")}
<p>Run at the frequencies the ratios imply, P7 at 100 ns and P53 at 625,000 / 12,288 ns,
both pins rise every {num(f['pll_p38'][0])} ns at a fixed phase, P38 leading by
{f['pll_phase']} ns ({mark('simulated')}, <code>check-readings-z3815a.pll</code>).  With P7
slowed to 130 ns, P40's intervals are {' and '.join(num(v) for v in f['p7_130_p40'])} ns:
the detector's frequency side at work (<code>check-readings-z3815a.p7_130</code>).  The runs
close no loop.</p>
<h3>Nothing of P7 reaches the firmware</h3>
<p>Following every net from P7 onward reaches only the P7 divider, the detector with
P38 and P40, and the sync logic that samples the prescaler ({mark('read')}).  The one net
out of that set, IH.Y, goes only into JH, the full-sync strobe, and no P7 net moved JH in
the <code>sync</code> runs, the field written 0 and then the kind as the Z3815A's firmware writes it
({mark('simulated')}, at one P7 phase).  So the register port, P83 and the other 1 PPS
outputs take nothing from P7, and the internal 1 PPS, the count's ticks and the tap run on
ACLK and GCLK alone ({mark('inferred')}).</p>
<p>On a board, then, the loop the detector belongs to is all that ties the firmware's
time to P7.  Locked, P7 / 6250 = P53 / 12,288, so the board drives ACLK at
P7 x 12,288 / 6250, 19.6608 MHz from a 10 MHz reference on P7.  The internal 1 PPS,
{num(z3815a['divide'])} GCLK clocks, is then {num(z3815a['divide'] * P7_DIVIDE // PRESCALE)} P7 cycles exactly ({mark('inferred')}; no
Z3815A or Z3816A board has been probed).  The Z3816A's board differs from the others', and
its 5 MHz OCXO, an MTI Series 260, is presumably doubled on it to that 10 MHz
({mark('inferred')}, not traced).</p>
</section>""")

    sections.append(f"""<section id="sync">
<h2>The sync field</h2>
<p>Register 3 holds a two-bit sync field.  In the Z3805A ({mark('simulated')}, the
<code>check-readings-z3805a.sync1</code> to <code>.sync3</code> runs) kind 1 moves the next
internal 1 PPS one clock earlier and kind 2 one clock later, so the count after it reads
{num(f['sync1'])} and {num(f['sync2'])} ticks over the preset, against
{num(f['z3805a_count'])}.  Kind 3 restarts the divider on the GPS 1 PPS, and the next
internal 1 PPS comes a second after that, at {f['sync3_p83']} s.  The 58503A/Z3801A's and the
58503B's sync blocks are the Z3805A's, and so are their results, the 58503B's a clock later on
its pins (<code>check-readings-58503a-z3801a.sync1</code>, <code>check-readings-58503b.sync1</code>
and the rest).</p>
<p>The telecom pair differs.  There kinds 1 and 2 lengthen or shorten one cycle of the P7
divider's prescaler and leave the 1 PPS alone.  Kind 3 makes the GPS 1 PPS itself the
internal 1 PPS: P83 rises {f['telecom_sync3']} ns after it ({mark('simulated')},
<code>check-readings-z3815a.sync3</code>).</p>
</section>""")

    times = " and ".join(f"{t} s" for _, t in alternate)
    sections.append(f"""<section id="outputs">
<h2>Other outputs</h2>
<ul>
<li>P16 passes the GPS 1 PPS straight through in all five, and P2 is tied Low in all five
({mark('read')}).</li>
<li>The 58503B, the Z3815A and the Z3816A each have pins that pulse on alternate seconds,
set by a toggle of the internal 1 PPS whose phase register 2's bit 0 sets
({mark('read')}).  In a four-second free run the Z3815A's P14 goes Low for
{num(ns(alternate[0][0]))} ns at {times}, the first and third of {f['p83_4s']} internal 1 PPS edges
({mark('simulated')}, <code>check-readings-z3815a.freerun_4s</code>).</li>
<li>The telecom pair's P20 is a one-ACLK pulse {f['p20_lead']} ns ahead of each P83 at the
testbench's 50 ns ACLK ({mark('simulated')}, <code>check-readings-z3815a.three</code>).</li>
<li>The Z3815A has more: a start-source select on register 2 (P82, P84 or P78), a counter
of P84 that the odd read addresses return in bits 4 to 7 and the firmware discards, and a
counter of P35 reset at each internal 1 PPS and out on six pins ({mark('read')}; what drives
P35 and P84 is {mark('open')}).</li>
</ul>
<p>What the board takes from most of these pins is {mark('open')}; each trace lists every
output with its source.</p>
</section>""")

    sections.append(f"""<section id="method">
<h2>How it was done</h2>
<p>The designs the bitstreams were built from are lost.  Each bitstream is decoded with a
third-party viewer, Arisotura's XC3000 viewer
(<a href="https://github.com/Arisotura/xc3000">github.com/Arisotura/xc3000</a>), pinned to
commit <code>{f['commit'][:7]}</code>.  The viewer's faults that Xilinx XACT's own designs
exposed are corrected in <code>tools/fpga/netdump.js</code>, and the decode is checked against
the routed test designs XACT made.  At the viewer's upstream pull request #3 the five designs
decode to the same netlists as at the pin, and the test designs agree with XACT there too.</p>
<p>The decode becomes structural Verilog over models of the XC3000's logic and I/O blocks,
written from the data sheet.  The models give every block the same 1 ns delay: they model
function, not timing.  The viewer has no license, so none of its code is copied into the
project, and the netlists stay generated under <code>target/</code>.</p>
<p>Each design's trace has the detail:
{', '.join(trace(d['image']) for d in f['designs'])}.  How the firmware loads the
bitstreams is {link('docs/firmware/outputs.md', 'outputs.md')}.</p>
</section>""")

    sections.append(f"""<section id="reproduce">
<h2>Reproduce it</h2>
<p>You need git and network access to GitHub for the viewer's clone, node or headless
Chrome, Python 3, Icarus Verilog 12, Yosys 0.52 and <code>pdftotext</code> (poppler-utils).
From the repository root:</p>
<pre data-scroll="x">make -f tools/fpga/Makefile all        # decode, build and run every design's fast path
make -f tools/fpga/Makefile check      # the same, failing on drift from tools/fpga/ref
make -f tools/fpga/Makefile figures    # the waveform figures
make -j16 -f tools/fpga/Makefile check-readings   # the long runs behind every simulated figure</pre>
<p><code>check-readings</code> takes a while; <code>time make</code> tells you how long on
your machine.  <code>make fpga</code> builds this page from the references and the
figures (<code>tools/fpga/explainer.py</code>).</p>
</section>""")

    body = "\n\n".join(sections) + """

<footer>Generated by <code>tools/fpga/explainer.py</code> from the checked references in
<code>tools/fpga/ref</code> and the waveforms <code>tools/fpga/vcd_wave.py</code> draws.</footer>"""
    return document("Inside the SmartClock FPGAs",
                    "The five SmartClock XC3042 FPGA designs, decoded, simulated and compared.",
                    STYLE, "explainer.py", "fpga", body)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--figures", type=Path, help="where the Makefile rendered the waveform SVGs")
    parser.add_argument("--output", type=Path, help="the page to write")
    parser.add_argument("--figures-used", action="store_true",
                        help="print the SVGs the page inlines, relative to --figures")
    args = parser.parse_args()
    if args.figures_used:
        print(" ".join(f"{name}.svg" for name in FIGURES))
        return
    if not args.figures or not args.output:
        die("--figures and --output are required")
    args.output.write_text(render(args.figures), encoding="utf-8")


if __name__ == "__main__":
    main()
