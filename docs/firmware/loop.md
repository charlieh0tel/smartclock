# The disciplining loop

Addresses are the Z3816A 4001's unless an image is named.  Another
image's are in its `names.tsv` and `types/smartclock.h`; "The FFOM"
gives each image's loop block.

## The stages

From the state strings at `0x2c5be` to `0x2c758`: `coarse`, then
`fine` -- with sub-states `fine start`, `fine sync meas`, `fine sync
1`, `fine sync 2`, `fine meas 1`, `fine meas delay`, `fine meas 2`,
`fine slew`, `fine slew meas`, `fine fine slew` -- then `startup pll`,
then `normal pll`.

The stage is the byte at `0x10282c`, the first of the loop's state
block.  The console word that prints `PLL state: ` switches on it at
`0x2bba8`:

| Value | Name |
| ----- | ---- |
| 0 | `invalid` |
| 1 | `powerup`, with sub-states from `start` through `checking time` (`0x2c67f` to `0x2c74c`) |
| 2 | `powerup recovery` |
| 3 | `holdover` |
| 4 | `holdover recovery` |
| 5 | `startup pll` |
| 6 | `normal pll` |
| 7 | `diag` |
| 8 | `idle` |
| 9 | `fatal error` |

The pSOS tasks are created at `0x231f0` to `0x232d4`: `gpsm` (entry
`0x50bf2`), `hmon` (`0x33dc4`), `pllp` (`FUN_0004b088`, task id at
`0x103d4e`) and `curv` (`0x450d4`, task id at `0x103d5e`); the SCPI
task, `sci` (entry `0x39706`, task id at `0x103d5a`), is created by
`FUN_00039732`.  The Z3805A's `:DIAGnostic:OS:PROCess?` lists the same
names (see `../scpi/undocumented.md`).  The loop below runs in `pllp`; the fit
in "The aging fit" runs in `curv`.

## Fine acquisition

`FUN_00048db6` is a state machine that takes fifty one-second readings
of the interval (state 5, `DAT_00102845 < 0x33`), accumulates the sums
of a straight-line fit, and sets the EFC from the fitted slope -- the
frequency offset.  Its messages:

    i= %d, ti= %.1f, y0= %.2e, x0= %.2e
    y0 = %f, efc =  %d
    fine - ti too far off
    fine - frequency too far off
    Bad TI in frequency measurement

## The loop

[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#the-loop),
"The loop", explains the update below: its block diagram, its law in
the page's symbols and what each term does.  This section is the
evidence: where each value lives and the code that runs it.

`FUN_0004824a` is `pll_normal`: its failure message is `pll_normal -
Error with measurement` (`0x487f8`) and its report is

    %d/%d/%d %02d:%02d:%02d efc= %d ti= %.1f loop= %d curr= %.2f

It keeps its state in a block at `0x10282c`.  Each second it adds that
second's reading (see `interval.md`, "One reading") to a sum at `0x10272c`; every
tenth second it runs the update below, in single-precision floating
point.  The values and their addresses:

| Symbol | Address | What it is |
| ------ | ------- | ---------- |
| x̄ | `0x102c0c` | the mean of the ten readings -- what the query returns; t_internal − t_GPS, the physical sign inferred (`counter.md`, "The sign, and the loop") |
| x₀ | `0x102c1c` | the setpoint subtracted from it: cleared at `0x4b1dc`, otherwise written only by the console word `phase_off` (`0x2b87c`), which nothing else calls |
| f | `0x102be0` | the filtered error, kept between updates |
| I | `0x102728` | the integrator |
| B | `0x102be4` | a base EFC, set on each entry to `pll_normal` (and, in the Z3805A and 58503A, to `startup_pll`) and fixed in between |
| τ | `0x102548` | the loop's time constant |
| G | `0x102c28` | a gain; `0x102c30` and `0x102c34` hold 1/G |
| c | `0x1023cc` | the value `:DIAGnostic:ROSCillator:TCOefficient?` reports, from the checksummed block at `0x400080`; see "s, the oscillator current" |
| s | -- | the oscillator current: `FUN_000324b0(6)`, channel 6 of 8 measured channels |
| M | `0x102c38` | a clamp, set to 6.25 × 10⁻¹⁰ / \|G\| |
| u | `0x10285e` | the EFC value the update produces |

The update:

    e  = x̄ − x₀
    f ← (1 − a)·f + a·e                      a = 29.75 / τ
    d  = clamp(p·2700 / q + r, −M, M)        p = [0x102bdc], r = [0x102bd8],
                                             q = [0x100c08], seconds
    I ← I + 10·k·(f + d / (2700·k))          k = 1 / (4·G·τ²)
    u  = K·f + B + I + c·s                   K = 1 / (G·τ)

and `FUN_00033798(u)` converts the result.  On entry the integrator is
cleared and B is set to the EFC in force less c·s (`0x4835c`), so
starting the loop does not move the oscillator; the update (`0x4863a`)
reads it.  In the Z3805A and 58503A, `startup_pll` clears I and sets B
the same way on its own entry and reads B in its update, and holdover
recovery hands back to `startup_pll` ("Starting up"), so B is set again
at each entry to either loop stage.  Holdover and the aging fit leave it
alone; between entries the oscillator's drift accumulates in I.

q is the seconds counter `FUN_00023818` returns, which `FUN_00023624`
advances once a second; it lies in the region a warm restart preserves
(see "τ and G").  p and r are the c and b of a fit to the EFC's own
history ("The aging fit", "Back to the loop"), so d is that fit's
slope now; what it does to the integrator is
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#the-aging-feed-forward),
"The aging feed-forward".

What kind of loop this is, its poles and damping, is worked out in
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#what-kind-of-loop-that-is),
"What kind of loop that is"; this file is the evidence for its
numbers.

## Starting up

`startup_pll`, at `0x47bac`, runs before `pll_normal` and applies the
same update law, except that its time constant is its own, at
`0x102be8`, and it recomputes K, k and a from it every update rather
than once on entry.  That time constant is set to 150 s at `0x47af6`
and `0x4af58`.

Its first state counts ten-second means whose magnitude is below
150 ns -- the double 1.5 × 10⁻⁷, loaded as two halves at `0x47e60` and
`0x47e66` -- and clears the count at the first that is not; once the
count reaches fifteen (`0x47e46`, `moveq #15` and `ble`) the next
update ends the state.  Then, after a power-up, it waits (below);
then, at `0x47ede` to `0x47fce`, it
lengthens its constant by 5 s (`0x47efe`) each update until it is
within 5 s of τ, sets it to τ, and hands over to `pll_normal`.  The
Z3801A's `FUN_000442ca` does the same.

In the Z3805A and 58503A 3704 (`startup_pll`, the lengthening state;
Z3805A `rewritten/pll_stages.c`, `pll_stages_rescale_integral`), each
step of the constant, the last to τ included, also adjusts the
integrator,

    I ← I + (f / G)·(1/τ_old − 1/τ_new)        τ_old taken as τ_new − 5

`pll_normal`'s entry (see "The loop") clears I and sets B to the EFC
in force less c·s but leaves f.  What each does to u is
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#starting-up),
"Starting up".

The Z3805A's `startup_pll` (`0x4429e`) and the 58503A 3704's
(`0x4429e`) have a state between those two, and so do the Z3816A's,
the Z3801A's and 3633's.  On entry the Z3805A's and 3704's clear f,
I and the count and set B (see "The loop").  After the fifteen, if
the aging fit's sampler is already collecting, the constant starts to
lengthen; otherwise -- after a power-up -- the state sets the flag
that starts the sampler and waits until the EFC history holds its
first three samples (`stop_ptr` 3, `start_ptr` 0; each image's test
is in "The FFOM"'s table), three windows of 2700 s (the sampler's
count of 2700, Z3805A `0x4146e`; its deadline 2700 s on, 3704
`0x41474`), before it lengthens.  The Z3815A and 58503B have the
state but wait in it, after the fifteen, until the uptime passes
7200 s, at the test the same table gives; after a later holdover the
state ends at its next update.

Holdover recovery hands back to `startup_pll`, not `pll_normal`.  It
leaves the start-up constant as it was -- τ, if the loop had got there,
so the hand-over follows the fifteen -- when the holdover lasted no
longer than the table in "In every image" gives for the image, its
last column, and the interval is within 500 ns; otherwise it sets it
back to 150 s
(`holdover_recovery`, Z3805A `0x43f9a`, 3704 `0x43fc0`).  `pll_init`
sets it to 150 s too (Z3805A `0x473d2`, 3704 `0x475ac`).

The `holdover` stage (`FUN_000473de`), not `startup_pll`, sets the EFC
from the aging fit below, at `0x475b8` to `0x476b4`: u =
FUN_00033798(a + b·y + c·ln y + c·s), with y = q / 2700 and the ln term
omitted when y ≤ 1.  The Z3805A's `holdover` (`0x43a90`) and 3704's
(`0x43ab8`) do it every ten seconds.

## The FFOM

`get_ffom` gives the FFOM, the frequency figure of merit, from the
stage byte alone: a switch on it, 2 for holdover and holdover
recovery, 1 for `startup pll`, 0 for `normal pll` and 3 for every
other stage, and a return of 1.  The loop's error, the TFOM and the
aging fit do not enter it.  Read from each image's switch table, with
what `startup_pll` waits for after a power-up ("Starting up"), at the
test it makes:

| Image | `get_ffom` | Stage byte | FFOM 2 | FFOM 1 | FFOM 0 | After a power-up, `startup_pll` waits for |
| ----- | ---------- | ---------- | ------ | ------ | ------ | ----------------------------------------- |
| Z3816A 4001 | `0x46368` | `0x10282c` | 3, 4 | 5 | 6 | three samples of the aging fit, `0x47eba` |
| Z3815A 4010 | `0x4bcba` | `0x10286e` | 3, 4 | 5 | 7 | 7200 s of uptime, `0x4d9e2` |
| Z3801A 3543 | `0x42a44` | `0x10215a` | 3, 4 | 5 | 6 | three samples of the aging fit, `0x4459e` |
| Z3805A 3543B | `0x42a18` | `0x102162` | 3, 4 | 5 | 6 | three samples of the aging fit, `0x44572` |
| 58503A 3633 | `0x42ac2` | `0x10246e` | 3, 4 | 5 | 6 | three samples of the aging fit, `0x4458e` |
| 58503A 3704 | `0x42ac2` | `0x102476` | 3, 4 | 5 | 6 | three samples of the aging fit, `0x445c8` |
| 58503B 1.01.04 | `0x478b2` | `0x102bb2` | 3, 4 | 5 | 6 | 7200 s of uptime, `0x494ec` |

So in the Z3815A `n3 lock pll`, its stage 6 ("The Z3815A's extra
stage"), gives 3, as power-up does.  097-59551-02 (5-23) defines 0 as
"PLL stabilized", 1 "PLL stabilizing", 2 "PLL unlocked (holdover)"
and 3 "PLL unlocked (not in holdover)"; the firmware's 2 also covers
holdover recovery.  The Z3805A's callers are the time code's f
(`timecode_update`, at `0x13cb2`), the FFOM query's handler
(`scpi_query_SOURce_PTIMe_FFOMerit`, `0x3005a`), the status screen's
snapshot (`status_snapshot`, `0x4a6a4`) and a console diagnostic
(`diag_clk_synch`, `0x1b35a`); 3704 adds the front panel's Status
line (`front_panel_status_text`, `0x52a68`), whose
`STABILIZING` and `10MHZ STABLE` are FFOM 1 and 0
(`58503a-3704.md`, "The front panel").  The status screen's
`stabilizing frequency` suffix follows the stage the same way
(`../screen-format-strings.md`, "Mode suffixes").

How long the receiver takes to reach FFOM 0, after a power-up and
after a holdover, is
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#what-the-receiver-shows),
"What the receiver shows".

## The aging fit

The `curv` task fits a curve to the EFC's history; the loop feeds the
curve's slope into its integrator.

**Sampling.**  `FUN_00044cbe`, which `pll_normal` calls on each pass
(`0x48a66`, `0x48be2`), works on a ring of 64 samples at `0x102876`:
tail byte at `0x102876`, head byte at `0x102877`, and four arrays of
64 -- e at `0x102880`, weight bytes at `0x102980`, y at `0x1029c0`, w at
`0x102ac0`.  While the stage is 6, or 5 with the byte at `0x102838` not
1, each call adds u − c·s -- the EFC in force at `0x10285e` less the
oscillator-current term -- to a sum at `0x102bc4` and counts it at
`0x10287e`.  When q reaches the deadline at `0x102bc0` the deadline
moves on by 2700 s and, if anything was counted, one sample is
written at the head:

    e = sum / count
    y = q / 2700
    w = y·ln(y / (y − 1)) + ln(y − 1) − 1        or −1 when q ≤ 2700
    weight = 3 if count = 2700; 2 if count ≥ 675; else 1;
             and 1 regardless while q < 10800

then the sum and count are cleared and event 0x100 is sent to `curv`
(`FUN_0004507a`).  With the debug byte at `0x102c13` set, each sample
is printed as `e_avg= %.1f time= %f weight= %d log= %.2f tfom= %.1e`
(`0x463a6`), time being y / 32 -- days.

The sum is a single-precision float, and each u − c·s goes into it
whole, in the Z3816A above and in the Z3805A 3543B, Z3801A and 58503A
3704 (3704 `efc_history_update`, `0x414b2` to `0x414d0`, sum at
`0x102810`); the Z3815A, Z3817A, 58503B and Z3805A 1.01.03 sum each
less the window's first and add that back to the mean
(`rewritten/*/pll_aging.c`); the 58503A 3633's was not checked.  A
float carries 24 bits, so from 2³⁰ to 2³¹ the sum is a multiple of
128: an EFC of u counts takes it there after 2³⁰ / u additions, 1534
at u = 7 × 10⁵, and each addition after rounds to the nearest 128
(`softfloat.md`).  With the EFC steady each rounds the same way, so
the errors add: the mean is off by an amount that varies with the
EFC's level, with a period of 128 counts, and the fit's slope is
distorted unless the EFC moves well over 128 counts across the
history.  `uv run tools/firmware/softfloat_check.py --sum` gives the
3704's error at each level, through its own add and divide.

What that does in service follows from the law.  Locked, the
integrator takes up a slope error of ε counts a second and moves the
standing time interval by 4·G·τ²·ε ("Simulating it").  In holdover
the EFC is set from the fit ("Starting up"),
and every fit but mode 0 passes through the newest sample, so an error
of e counts in that sample's mean is a frequency offset of G·e from
the start of holdover and a time error of G·e·t after t seconds; ε
adds G·ε·t²/2.  G and τ are in "In every image".  On a settled
oscillator the EFC barely moves and e stands; the further it moves
within a window, the more of that window's errors cancel.

w is ∫ ln t dt over the window from y − 1 to y, since ∫ ln t dt =
t·ln t − t: the mean of ln t over the 2700 s the sample averages.  The
fit below regresses e on y and w, so a + b·y + c·w is the window mean
of a′ + b·t + c·ln t with a′ = a + b/2, the linear term's mean being
b·(y − ½): b and c carry over to the instantaneous curve, the intercept
is off by half a window's change.  How the weight is used is
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#the-aging-feed-forward),
"The aging feed-forward", and the fit's modes below.

**The fit.**  `FUN_00045116` runs on each event.  Its record is at
`0x1026a2`: mode byte at +0, a at +0x12, b at +0x16, c at +0x1a, rms at
+0x22, HQ at +0x26; its debug lines are `pts= %d a= %.1f b= %.1f c=
%.1f rms= %.1f` (`0x46415`), `HQ= %.1e mode= %d` (`0x46441`) and
`failed line fit` (`0x46455`).  It counts n₂, the samples with weight
2 or more, and n₁, weight 1 or more, and finds the oldest and newest y
(`FUN_00045680`).  Then, first match wins:

| Mode | When | Fit |
| ---- | ---- | --- |
| 4 | n₂ = 64 and the newest y > 128 | b = (Σe over the newest 32 − Σe over the oldest 32) / 1024, a = ē − b·(y_newest − 32.5), c = 0 (`FUN_00045392`) |
| 3 | the oldest y > 128 and n₂ > 2 | a straight line through the weight-2 samples (`FUN_00045ce0`), c = 0 |
| 3 or 2 | n₂ > 5 | the straight line, then the three-term fit (`FUN_000455de`); the latter is taken, and the mode is 2, when its rms is below 0.75 of the line's or n₂ < 16 |
| 3 or 2 | n₁ > 9 | the same on the weight-1 samples |
| 1 | n₁ > 2 | the straight line through the weight-1 samples |
| 0 | otherwise | a = ē, b = c = 0 (`FUN_00045570`) |

`FUN_00045ce0` is an unweighted least-squares line of e on y,
returning the slope, the intercept and the rms of the residuals over
n − 2; with fewer than three points or no spread in y it fails and
clears its results.  The three-term fit forms a first c and a step
from the line (`FUN_00045782`), refits the line to e − c·w with c
moved by that step while the rms falls (`FUN_0004587a`), and sets c
from the last three trials (`FUN_00045a78`).  Every fit but mode 0
ends by resetting a so the curve passes through the newest sample
(`FUN_0004571c`).  HQ is `FUN_00045f94`'s result for modes 2 to 4 and
4.32 × 10⁻⁴ for modes 0 and 1:

    r_i  = e_i − (a + b·y_i + c·w_i)        over the samples in the ring
    A    = √( Σ ½(r_i − r_{i−1})² / (n − ½) ) n the number of differences
    T    = Δy · 86400 / 32                  the ring's span, in seconds
    u    = rms · 86400 / 1.6 × 10¹² · √(K / T)
    v    = k · A / 16 / (T / 2700)^1.5
    HQ   = 2.5 · √(u² + v²)

with k = 2.5 × 10⁻⁴ and K = 345600 in mode 2, k = 5.5 × 10⁻⁵ and
K = 259200 in modes 3 and 4 (`0x460f6` to `0x46196`, `0x461ae` to
`0x4623e`, the common tail `0x46244` to `0x462da`); the 16 is the
code's division by 10⁻¹¹ and then by 1.6 × 10¹².  A is an Allan-like
deviation of the residual EFC at 2700 s.

HQ is the predicted holdover uncertainty, in seconds.  The handler
both `:ROSCillator:` and `:SYNChronization:HOLDover:TUNCertainty:PREDicted?`
reach (`0x3f5f2`) calls `0x46316`, which in stages 3 to 6 returns the
long at +0x3a of the loop block, `0x102866`, HQ's copy, and in other
stages nothing, so the query errors; the status screen fills its
`Predict ... /initial 24 hrs` line from the same getter (`0x4ec56`,
printer `0x4f1c8`).  The default 4.32 × 10⁻⁴ is the `432.0 us/initial
24 hrs` a receiver shows while its fit is in mode 0 or 1.  The 58503A
3704 (handler `0x2fb7a`, getter `0x42a70`, cell `0x1024b2`) and the
Z3801A (`0x2ecb0`, `0x429f2`, `0x102194`) do the same.  The getter's
other two results shape the reply.  The byte 0xf9, −7, is the exponent
of its resolution: the formatter (`0x35ea4`, through `0x349d8`) prints
the value to 10⁻⁷ s in no more than six digits, the "100 nanoseconds"
of 097-59551-02 5-24, so 432 µs is `+4.320E-004`.  A flag set only in
stages 3 and 4 is the reply's second number, which the manual gives as
1 in holdover and 0 otherwise.  Where the getter answers nothing, the
status screen prints `--` (`0x53bac`; 3704 `0x4f0da`, Z3801A
`0x4e2ad`).  What the letters stand for was not established.  In the bench logs to
2026-10-11, the 58503A reported 432 µs from 2026-09-20 to 2026-10-02
and 10.2 µs to 324 µs after it, mostly 13 to 26 µs; the Z3805A 432 µs
to 2026-09-29, then 2.3 µs to 88 µs, mostly about 3 µs; the Z3801A
6.5 µs to 12 µs at times, and 432 µs again after its holdovers of
2026-10-10.  128
in units of 2700 s is 96 hours.

**Back to the loop.**  `curv` then sends event 0x100 to `pllp`.  At the
start of each sampling pass `FUN_0004508e` asks for that event
(`FUN_000274d6`) and, when it has arrived, copies a to `0x102bd4`, b to
`0x102bd8` -- the loop's r -- c to `0x102bdc` -- its p -- and HQ to
`0x102866`.  All four sit in the loop block a warm restart preserves,
so after such a restart the loop starts with the last fit.  After a
power-up b and c are zero until a slope fit, which needs three
samples -- mode 1 or above; mode 0 sets a alone, from as few as one.
The `holdover` stage, `FUN_000473de` (its return of 1 moves the stage byte
to 4 at `0x4b36a`), seeds a with u − c·s when a is zero (`0x47440` to
`0x47460`).

The console's `last efc average = %.1f` (`0x2c3a5`) prints the newest
e; `dmes_curv` (`0x2befe`) sets the byte at `0x103d8a`.

### The aging fit in the Z3805A 3543B

The record is `aging_curve` (`0x101fd8`, `structures.md`), the same
layout, and the ring is `pll.efc_history`.

| Above | Here | |
| ----- | ---- | - |
| `FUN_00045116` | `aging_fit` (`0x417c6`) | |
| `FUN_00045680` | `fit_span` (`0x41d30`) | |
| `FUN_00045392` | `fit_linear_halves` (`0x41a42`) | mode 4 |
| `FUN_00045ce0` | `fit_linear` (`0x41ba0`) | through `linear_regression` (`0x42390`) |
| `FUN_000455de` | `fit_logarithmic` (`0x41c8e`) | |
| `FUN_00045570` | `fit_constant` (`0x41c20`) | mode 0 |
| `FUN_00045782` | `fit_log_initial` (`0x41e32`) | |
| `FUN_0004587a` | `fit_log_search` (`0x41f2a`) | |
| `FUN_00045a78` | `fit_three_point` (`0x42128`) | |
| `FUN_0004571c` | `fit_evaluate_latest` (`0x41dcc`) | |
| `FUN_00045f94` | `fit_quality` (`0x42644`) | |
| `FUN_0004508e` | `aging_fit_collect` (`0x4173e`) | into `fit_a` to `fit_c` and `predicted_uncertainty` |

## s, the oscillator current

`FUN_000324b0(n)` returns channel n of eight measured channels, each a
48-byte record at `0x102258` + 0x30·n: the live value when the flag at
`0x10225c` + 0x30·n is set, otherwise a default from a ROM table at
`0x325b2` + 0x2a·n.  The channels' names follow that table at
`0x326e6`, in order:

    12B  5V  12C  -12B  -12C  -12D  Oscillator current  Antenna current

and their defaults are 12, 5, 12, −11.5, −11.5, −11.5, 250 and 50.  The
loop reads channel 6, so c·s is a stored constant times the oscillator
current.

The record does not hold the latest reading.  Each channel's
descriptor at `0x32596` + 0x2a·n names a conversion (`FUN_00031e66`
for channel 6) that `FUN_00031eac(n)` applies to a fresh ADC read, and
a writer that files the result: `FUN_00031f3e` for most channels,
which averages ten readings; `FUN_00031fc4` and, for channel 6,
`FUN_00032048`, which keep an exponential average, s ← 0.1·fresh +
0.9·s (`0x3dcccccd`, `0x3f666666`) on each of the health monitor's
passes.  So the loop's s lags the current by about ten passes, and a
reading flickering between two ADC levels barely moves it.  The SCPI
queries read different cells: `:DIAGnostic:ROSCillator:CURRent?`
(handler `0x3b18c`) returns `FUN_00031eac(6)`, a fresh conversion, not
the loop's average; `:DIAGnostic:ROSCillator:EFControl:ABSolute?`
(`0x3b1f0`) and `:RELative?` (`0x3b1a2`) both read u at `0x10285e`,
the value the update writes, so on this firmware the DAC word a
monitor logs is the whole update, c·s included.

c is what `:DIAGnostic:ROSCillator:TCOefficient` reads and writes.
The query's node names the handler `FUN_0003b248`, which formats the
record at `0x43324`: the cell `0x1023cc`, the getter `FUN_00022bb8`,
the limits −200 and 200 (`0x43314`, `0x43318`) and the setter
`FUN_000404be`, which writes the calibration block back to the EEPROM
at `0x400080`.  Nothing else writes the cell: the loop applies it every
update, `startup_pll` and the aging fit's sampler use it, and none
adjusts it.  So in this firmware the "temperature coefficient" is a
stored calibration constant multiplying the oscillator current -- in
EFC units per unit of that channel, nominally 250 -- applied all the
time; it is not a coefficient on temperature and is not learned while
locked.

The console word `xcal` (`0x2bf78`) measures it.  Given a number of
seconds, it prints `curr= %d efc= %.1f, sec remaining= %d` (`0x2c7db`)
every ten seconds for that long -- the raw current from the ADC
(`FUN_0002e3b0`, the reading shifted right by two) and the EFC in
force at `0x10285e` -- then `now do a least square line fit to
data...` and `tempco = %f` (`0x2c803`, `0x2c82f`), the slope of EFC on
current.  It stores nothing; the value is entered afterwards with the
SCPI command.

The 58503A images 3633 and 3704 apply the same term (`console.md`,
"The 58503A image"), and the bench 58503A does: setting the coefficient
to 0 moved its DAC word by c·s at the next update (`efc.md`, "The
regression").  The Z3805A's loop reads channel 3 of its monitors
(`adc_average`, `0x2410e`), the oscillator current, and 3704's channel
6, `Primary oven current` (`0x222a6`); each is the 0.1 / 0.9
exponential average, 250 until the first reading.  The Z3801A's
image reads channel 3 of its own function, `FUN_00022fd2`; its report
strings list Temperature, 5V, +15V, −15V, Oven, Double oven and Antenna
current, but which is its channel 3 was not traced.

## τ and G

`FUN_0002b358` sets τ from its argument.  Nothing calls it directly; a
pointer to it sits in the console's word table at `0x2d368`, beside the
name `loop_time`, and the message `max loop time = %d` (`0x2c370`)
prints τ.  It is the only code that writes τ's address, and the
start-up code clears the RAM τ lives in, so its value in service comes
from a 25-byte block of defaults in ROM.

τ is at offset 0x14 of a 25-byte block at `0x102534`.  The
initialization routine `FUN_00022c7c` fills that block one of two ways:

- from ROM, `memcpy(0x102534, 0x40174, 0x19)` (`0x22dd8`, `0x22e06`),
  whose bytes at `0x40188` are `43 fa 00 00`, the float 500.0.  The same
  routine first copies a longer block of defaults, `0x400de` to
  `0x40173`, into `0x10249e` to `0x102532`.  Its cells that parameter
  structures (`restart.md`, "Restarting") name are the survey state at
  `0x1024a8`, which the `:GPS:POSition:SURVey:STATe` setter posts
  (default 5), and the cells `:GPS:INITial:DATE` (`0x10250e`, default
  1961, 8, 22), `:GPS:INITial:TIME` (`0x10251a`, 10, 48, 0) and
  `:GPS:INITial:POSition` (`0x102526` to `0x102531`, 0, 0, 0) set;
  the rest -- small integers, single and double floats -- no structure
  names, and a sweep for any access through an address register loaded
  with an address in the block finds only the copy itself (`0x22cdc`)
  and the survey state's readers;
- or from the region `0x100000` to `0x100c3b`, which the start-up code
  does not clear (its clear runs from `0x100c3c`) and into which the
  pllp task keeps writing its state (`0x4b5fc`: the τ block to
  `0x100b82`, the loop block to `0x100622`, another to `0x100004`).
  That copy is taken when a byte checksum of the region (`FUN_00040056`
  over 0xb9c bytes) matches the word stored at its start, the flag word
  at `0x100002` is 1, the byte at `0x10262c` is clear, and bits 7 and 6
  of the reset-status register at `0xfffa07` are clear -- EXT and POW
  in MC68331UM D.3.4: the reset was neither external nor a power-up
  (the other bits are SW, HLT, LOC, SYS and TST; `FUN_00022c7c` at
  `0x22dc4` also requires the register to be exactly SYS, a RESET
  instruction).  Then the loop block, the health-monitor records and
  the τ block are restored; otherwise the defaults are loaded.  The
  log entry is the pllp task's, as it starts (`FUN_0004b088`), from
  the loop block's stage byte (`0x10282c`): not 0, as a restored
  block's is, and it logs `Re-boot` (`FUN_00041180(2)`, `0x4b1c6`);
  0, as the defaults leave it, and it logs `Power on`, after posting
  event 0x19 (`FUN_0003ec76`, `0x4b17e`; `FUN_00041180(1)`,
  `0x4b188`), when the flag word at `0x100002` is 1, as every start
  but `:SYSTem:PRESet`'s restart leaves it, and `System preset`
  (`FUN_00041180(0x1b)`, `0x4b196`) when it is not.  So a warm start
  logs `Re-boot`, a cold one `Power on`, and only the restart after a
  preset `System preset`.  The 58503A's log shows it so on hardware: a
  watchdog reset's warm start logged `Re-boot`, and a power-up's and
  an external reset's cold starts `Power on`
  (`../hardware-investigations.md` item 7, "Measured, resets on the
  58503A").

So a value set with `loop_time` survives a reset that leaves RAM
intact, and the loop uses the ROM's 500 s after a power-up.  How long
the startup ramp (above) takes to reach τ in each model is
[`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#symbols-and-constants)'s
"By model" table.

The Z3801A's image does the same with its block at `0x101e6e`, filled
from ROM `0x2f846` (`0x12ce0`), whose float at offset 0x14 is
`44 7a 00 00`: 1000.0.

The rest of the 25-byte block, from its ROM defaults
`00 00 00 01 00 00 01 00 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00 |
43 fa 00 00 | 00` and the code that names its bytes by address:

| Offset | Default | Read and written by |
| ------ | ------- | ------------------- |
| +0, word | 0 | nothing but the block copies |
| +2 | 0 | the alarm LED: `FUN_00049f76`, called from `pllp`, gathers bits from `FUN_0003ed82`, the +0x18 flag, `FUN_000324fc` and `FUN_00049f06` into the byte at `0x102c16` and sets +2 when any is set (`0x4a008`); the health monitor folds it into a status bit (`0x33d12`); `:LED:ALARm?` and `:LED:ALARm:MAJor?` return it (`FUN_0003d3d0`) |
| +3 | 1 | the cell of the toggle structure at `0x431bc` (`restart.md`, "Restarting"); nothing refers to that structure by address, and it is not among the saved settings |
| +4 | 0 | `:LED:ACTive?` returns it (`FUN_0003d3b6`) |
| +5 | 0 | `:LED:ENABled?` returns it (`FUN_0003d458`) |
| +6 | 1 | `:SYNChronization:HOLDover:RECovery:AUTO` (`FUN_0003f57a`; `:ROSCillator:HOLDover:RECovery:AUTO` is the same node): set to 1 when the loop starts (`0x4af60`); consulted when holdover begins (`0x472bc`, under the stage byte's move to 3) |
| +7 | 0 | set to 1 by the `powerup` sub-state machine `FUN_0004a34a` (`0x4a764`) once it has stored a surveyed position in +8 to +0x13; the `:GPS:POSition` setter `FUN_0003c268` tests it (`0x3c330`) and the query `FUN_0003c6b0` fails with code 0xc while it is clear (`0x3c6de`) |
| +8, +0xc, +0x10, longs | 0 | the surveyed position: latitude, longitude and height, which the `powerup` machine converts from the survey's averages at `0x10142e`, `0x101436` and `0x101446` (`0x4a71c` to `0x4a758`, the block's address passed in at `0x4a382`); while +7 is set the `:GPS:POSition` setter writes them from its argument (`0x3c35c`, three values through `FUN_00038f04`), and when a position request is pending (`0x102c5e`, `restart.md`) the machine copies them to the position cell `0x102574` and saves the settings (`0x4a796` to `0x4a7b4`) |
| +0x14, float | 500 | τ |
| +0x18 | 0 | a holdover-recovery flag: `FUN_000473ac`, called from the `holdover recovery` stage (`FUN_000478ce`, message `holdover recovery - Error with measurement`), counts its calls at `0x102bf6` and sets the flag with event 0x2e when the count passes the limit at `0x102566`; the `powerup` machine clears it (`0x4a094`) and so does the loop's start (`0x4afa0`); it is bit 0x20 of the alarm summary above.  3704 also drives a pin with it: `FUN_00045ec8` clears bit 1 of `0xfff925` when the flag is set (`0x43aae`, beside event 0x2e) and sets it when it is cleared (`0x4673a`, beside event 0x2f, and at start-up, `0x12e50`).  That bit is PWMC's F1A, the level the PWMA pin is forced to while FPWMA is set (MC68331UM D.2.14), and the start-up code sets FPWMA and FPWMB (`ori.b #3` into `0xfff924`, `0x121a6`), so PWMA goes low while the flag is set |

Bytes +2 to +6 and +8 also appear, each with its address, in 28-byte
descriptor records at `0x43168` to `0x43228` and `0x4369e`, alongside
the same getter `FUN_00022bb8`; what those records serve was not
traced.  The event codes passed to `FUN_0003ec76` -- 0x19 to 0x5c,
posted to the queue at `0x10356e` -- are not the log's codes: the log
writer `FUN_00041180` takes a code from 0 to 0x1b, looks up its text --
`Log cleared`, `Power on`, `Re-boot`, ... `System preset` (`0x41562`
to `0x41834`) -- and writes the entry through `FUN_00041138` under the
lock at `0x1023fa`.

G is a constant.  `FUN_0004b088` reads the hardware word at
`0x302000` and passes −1.25 × 10⁻¹² if bit 8 is set and
−2.125 × 10⁻¹² if clear (`0x4b14c` to `0x4b166`); the Z3801A's image
passes a fixed +6.25 × 10⁻¹³ (`0x475bc`).  That word is all the
firmware does with chip select 7: the primary's reset code makes
`0x302000` a 2 KB, 16-bit, read-only block with one wait state
(CSBAR7 `0x3020`, CSOR7 `0x6870` at `0x24706`, `0x2470e`;
`README.md`'s chip-select table), `0x4b14c` is the only access to it
in the image -- every other form of the address was searched for --
and only bit 8 is examined.  So it is an input port the processor reads once, when the
loop task starts; what drives bit 8 -- a link, a switch, or a signal
from the oscillator or DAC board -- is on the board, not in the image.
Bit 8 of the word is bit 0 of the byte at `0x302000`, which on the
58503A's board is S1's position 1, OFF reading 1, measured
(`restart.md`, "The switch byte at `0x302000`"); if the Z3816A's
board wires S1 the same way, position 1 OFF gives −1.25 × 10⁻¹² and
ON −2.125 × 10⁻¹², but no Z3816A board has been checked.
Nothing the EEPROM holds changes it ("G and the calibration block",
below).  In the Z3801A the address is only inside chip select 5's 64 KiB
window, with the latch and the FPGA (`restart.md`, "The chip
selects"), and its image never reads it.  What the sign of G stands
for was not traced.

`FUN_0004b022(G)` stores G, sets both gain constants to 1/G, sets M to
6.25 × 10⁻¹⁰ / |G|, and sets a second limit at `0x102c3c` to
5.787 × 10⁻¹⁴ / G -- 5.787 × 10⁻¹⁴ being 5 × 10⁻⁹ per day expressed per
second.  Its one caller is `FUN_0004b088`.

## In every image

Every image holds the loop's strings -- the stage names through `fine
fine slew` and `normal pll`, `pll_normal - Error with measurement`,
the fit's `pts= %d a=` and `failed line fit`, the sampler's `e_avg= `,
the console's `loop_time` -- and the constants of the update and the
fit: 29.75, 2700.0, the clamp's 6.25 × 10⁻¹⁰ and HQ's 4.32 × 10⁻⁴.  The
update law above was transcribed from the Z3816A only; in the others
these show the same code is there, not that it is unchanged -- but for
the Z3805A 3543B, whose decompiled `pll_normal` (`0x448fc`, gcc's
build of it agreeing with the image in the harness) gives the same
law term for term, below, and the 58503A 3704, whose `pll_normal`
(`0x4495a`) does too.  What
does differ is where τ and G come from.  Read on 2026-10-09, the
Z3805A 1.01.03 and the Z3817A 3752 on 2026-11-16.  The
table is made from `loop-constants.json`, which
`docs/pages/loop.html`'s "By model" table is made from too (`make
loop-constants`).  The addresses after a value are where the image
holds it, for τ the start of the ROM block of defaults, and `make
check-loop-constants` reads every one ("How this is checked"); those
after "by" are the code that chooses.

<!-- loop_constants.py evidence -->
| Image | τ, as the ROM block sets it | Startup's constant | G | Longest holdover keeping startup's constant |
| ----- | --------------------------- | ------------------ | - | ------------------------------------------- |
| Z3816A 4001 | 500 s (`0x40174`) | 150 s (`0x47af8`, `0x4af5a`) | −1.25 × 10⁻¹² (`0x4b15a`) or −2.125 × 10⁻¹² (`0x4b162`), by bit 8 of the word at `0x302000` (`0x4b14c`) | not traced |
| Z3815A 4010 | 500 s (`0x44c9e`, `0x50c0c`) or 1000 s (`0x50c00`), by bit 12 of the word at `0x302000` (`0x50be8`) | 150.0 present, not traced | +1.2 × 10⁻¹² (`0x45046`) or −2.125 × 10⁻¹² (`0x45052`), by that bit (`0x4503c`) | not traced |
| Z3801A 3543 | 1000 s (`0x2f846`) | 150 s | +6.25 × 10⁻¹³ (`0x475be`) | not traced |
| Z3805A 3543B | 1000 s (`0x3094c`) | 150 s (`0x47404`, `0x441f4`) | +6.25 × 10⁻¹³ (`0x475f4`) | 500 s (`0x441ba`) |
| 58503A 3633 | 700 s (`0x305ae`) | 150.0 present, not traced | +6.25 × 10⁻¹³ (`0x476bc`) | not traced |
| 58503A 3704 | 700 s (`0x30840`) | 150 s (`0x475e2`, `0x441ea`) | +6.25 × 10⁻¹³ (`0x477dc`) | 350 s (`0x441b0`) |
| 58503B 1.01.04 | 700 s (`0x407b4`), settable: `:DIAGnostic:ROSCillator:LTIMe:MAX` | 150 s (`0x407b4`), settable: `:DIAGnostic:ROSCillator:LTIMe:INIT` | −1.25 × 10⁻¹² (`0x40dfc`) or +6.25 × 10⁻¹³ (`0x40e08`), by oscillator type, `:DIAGnostic:ROSCillator:TYPE` | not traced |
| Z3805A 1.01.03 | 1000 s (`0x323a4`), settable: `:DIAGnostic:ROSCillator:LTIMe:MAX` | 150 s (`0x323a4`), settable: `:DIAGnostic:ROSCillator:LTIMe:INIT` | +6.25 × 10⁻¹³ (`0x329ec`) or +9.238 × 10⁻¹³ (`0x329f8`), by oscillator type, `:DIAGnostic:ROSCillator:TYPE` (`0x3277a`) | 500 s, half of τ (`0x44990`) |
| Z3817A 3752 | 1000 s (`0x41450`) or 500 s (`0x4ca5a`), settable: `:DIAGnostic:ROSCillator:LTIMe:MAX`; by oscillator kind, `:DIAGnostic:ROSCillator:TYPE`, 500 s for kind 1 alone (`0x4ca4e`) | 150 s (`0x41450`), settable: `:DIAGnostic:ROSCillator:LTIMe:INIT` | +5 × 10⁻¹³ (`0x4180a`) or −2.125 × 10⁻¹² (`0x41820`), by oscillator kind, `:DIAGnostic:ROSCillator:TYPE`, the first for kind 0 alone (`0x41800`) | 500 s or 250 s, half of τ (`0x492f2`) |
<!-- end loop_constants.py -->

**The update, compared.**  Each image's `pll_normal` and fit
dispatcher (`pts= %d a=`) were compared with the Z3816A's instruction
by instruction, with every address and branch target masked and each
call named by a hash of the routine it calls, so the same float
routine compares equal across images and a different one does not.

- The fit dispatcher is identical, callees included, in the Z3801A,
  Z3805A and both 58503As.
- `pll_normal` in 58503A 3633 differs in two places: it reads the
  oscillator current through channel 3 of another routine, and stores
  1 × 10⁻⁸ (`0x322bcc77`) where the Z3816A stores 1 × 10⁻⁷ at offset
  0x46 of the loop block.  The Z3801A and Z3805A differ the same way
  and in their hardware routines -- the reading (`0x489de` in the
  Z3816A), the DAC conversion (`0x33798`) and `0x4782e` -- with every
  float operation the same.  3704 adds a field that moves the block's
  later offsets by two.  So these four run the update above.
- The Z3815A and 58503B differ in the arithmetic.  Both compute K, k
  and a from a τ held in a register each pass, as `startup_pll` does,
  and both replace the drift term: where the Z3816A forms
  d = p·2700/q + r every ten seconds, they form the same expression
  once per fit: when the fit's event arrives, the 58503B's `0x4658c`
  (the Z3815A's at `0x4aa26`) copies a, b and c and stores
  x = c·2700/q + b (`0x10327a`).  Each update then moves a running
  value toward it, v ← ((τ − 10)·v + 10·x)/τ (`0x10327e`, at
  `0x49c38`), clamps v to ±M, and adds it to the integrator where the
  Z3816A adds d ([`loop.html`](https://charlieh0tel.github.io/smartclock/loop.html#the-aging-feed-forward),
  "The aging feed-forward", says what that changes).  Their fit dispatchers differ
  from the Z3816A's too, and from each other.
- The Z3817A 3752's `pll_normal` (`0x49b1e`) is the Z3815A's but that
  it subtracts no setpoint (its console has no `phase_off`), its
  current is the constant 0, and the integral's step is multiplied by
  the block's length in readings where the Z3815A's multiplies by 10
  (`z3817a-3752.md`, "The update law").

The τ block has the 25-byte layout above in every image but the
58503B, the Z3805A 1.01.03 and the Z3817A 3752 (below), the 58503B's block (ROM `0x407b4`, copied to `0x1028b8` at
`0x22d6e`) has 29 bytes: the startup constant at +0x14 and τ at +0x18.
Two SCPI parameters, undocumented, write them: `:DIAGnostic:
ROSCillator:LTIMe:INIT` (`0x1028cc`, 10 to 1000 s) and `:LTIMe:MAX`
(`0x1028d0`, 10 to 10000 s); each setter keeps the start at or below
the maximum (`0x4aa84`, `0x4aaf6`), and the time constant in force is
at `0x10328a`.  The Z3816A and Z3801A hard-code the start, 150 s.

The 58503B takes G from a table of 12-byte entries at `0x40dfc` --
G and two limits -- indexed by the byte at `0x102726` (`0x40b8e`).
That byte is what `:DIAGnostic:ROSCillator:TYPE?` returns, and its
setter accepts a value below the number of types (`0x3b58c`).  Two
entries are filled: G −1.25 × 10⁻¹², the Z3816A's, with limits
±10⁻¹¹, and +6.25 × 10⁻¹³, the 58503A's; the type selects which.  In both the 58503B and the Z3815A the cell holding G is also
`:DIAGnostic:ROSCillator:EFControl:ASLOPe`'s, so G can be read and set
over SCPI within its limits (`../scpi/undocumented.md`).

The Z3805A 1.01.03, the 58503B's code with the Z3815A's Furuno side
(`z3805a-1.01.03.md`), sets τ and G the 58503B's way.  Its 29-byte
block (ROM `0x323a4`, copied to `0x10291a` at `0x12d3e`) holds the
startup constant, 150 s, at +0x14 and τ, 1000 s, the Z3805A 3543B's,
at +0x18.  `system_defaults_init` copies it on every start, warm or
cold, and nothing restores it from the preserved region, so the ROM's
values are in force after any reset.  The same two parameters write
them, `:LTIMe:INIT` (`0x10292e`, 10 to 1000 s) and `:LTIMe:MAX`
(`0x102932`, 10 to 10000 s), whose done hooks keep the start at or
below the maximum (`0x463a2`, `0x46414`); neither descriptor
(`0x35c2e`, `0x35c6a`) marks its value saved, and the time constant in
force is at `0x1032ec`.  G comes from a table of two 12-byte entries
at `0x329ec`, indexed by the oscillator type at `0x102788`
(`efc_calibration_defaults`, `0x3277a`), which
`:DIAGnostic:ROSCillator:TYPE` sets to 0 or 1 (`oscillator_type_most`,
`0x32852`) and an erased or bad calibration block reads as 0:
+6.25 × 10⁻¹³, the 58503A's and the Z3805A 3543B's, with limits
10⁻¹⁵ and 10⁻¹¹, and +9.238 × 10⁻¹³, which no other image here holds,
with limits ±10⁻¹¹.  That is `:ASLOPe`'s default; the loop takes G
from `:ASLOPe`'s cell (`0x102780`) as it starts (`0x48094`), so an
`:ASLOPe` the calibration block holds, or one an EFC calibration
measures, stands in for the type's.  Holdover recovery restarts the
ramp at the startup constant after a holdover longer than half of τ,
taken as an integer and shifted right once (`0x44990`), or with the
interval past 500 ns: 500 s at τ's default.  Its `pll_normal`
(`0x4520c`) eases the aging term in as the 58503B's does
(`z3805a-1.01.03.md`, "The link list").

The Z3817A 3752 sets τ and G by its oscillator's kind, where the
Z3815A goes by the switch word's bit 12 (`z3817a-3752.md`, "The loop's
constants").  Its 29-byte block (ROM `0x41450`, copied to `0x1023b4` at
`0x22e5e` and `0x22e94`) holds the startup constant, 150 s, at +0x14
and τ, 1000 s, at +0x18.  `system_init` copies it on a cold start and
on a warm one restores the copy the preserved region keeps
(`0x22dec`), so a setting written over SCPI outlasts a warm restart
but not a power-up.  The same two parameters write them,
`:LTIMe:INIT` (`0x1023c8`, 10 to 1000 s) and `:LTIMe:MAX` (`0x1023cc`,
10 to 10000 s), whose done hooks keep the start at or below the
maximum (`0x4ae46`, `0x4aeb8`); neither descriptor (`0x44a58`,
`0x44a94`) marks its value saved, and the time constant in force is
`pll` + `0x3c8` (`0x102a62`).  The kind is the calibration block's byte at the
EEPROM's `0x40009c` (`oscillator_kind`, `0x10222e`, read by
`oscillator_kind_read`, `0x418ec`, 0 for an erased byte), which
`:DIAGnostic:ROSCillator:TYPE` sets to 0 or 1 before a power-on reset
(`0x3c38a`).  Kind 1 sets τ to 500 s in `pll_init` (`0x4ca4e`), which
runs after the block's copy and at each restart of the loop; any other
kind keeps the block's.  G is `:ASLOPe`'s default, which
`efc_calibration_defaults` (`0x417fa`) gives by the kind: +5 × 10⁻¹³
for kind 0, no other image's, with `:BSLOPe` 3.3 × 10⁻²⁴ (`0x41814`),
and −2.125 × 10⁻¹², the Z3815A's second, with `:BSLOPe` 0, for any
other.  So kind 0 runs at τ 1000 s and +5 × 10⁻¹³, kind 1 at 500 s and
−2.125 × 10⁻¹², the Z3815A's pair with its bit clear, and a byte past 1,
which only the EEPROM can hold, at 1000 s and −2.125 × 10⁻¹².  A
calibration block taken for this kind, or an erased kind, stands in
for the defaults (`efc_calibration_read`, `0x41834`).  Unlike the
58503B's and the Z3805A 1.01.03's, the loop takes the gain as
A + B·u² at the EFC in force u, as the Z3815A's does: from `:ASLOPe`
alone as `pllp_task` starts (`0x4cbfe`), then in each block's first
second of the start-up and normal loops (`0x4962a`, `0x49dbe`) and at
each aging fit (`0x47842`) through `efc_gain_from_calibration`
(`0x4d76c`); with kind 0's defaults G grows with the EFC's square from
the table's +5 × 10⁻¹³ at EFC 0.  Holdover recovery restarts the ramp
at the startup constant after a holdover longer than half of τ, taken
as an integer and shifted right once (`0x492f2`), or with the interval
past 500 ns: 500 s at τ 1000 s, 250 s at 500 s.  Its update law is the
Z3815A's with the differences above; `loop.html`'s columns take a
block of ten readings, the 1PPS's: with a PP2S its integral's step is half
(`z3817a-3752.md`, "The update law").

**The gain's second term and the measured uncertainty, Z3815A and
58503B only.**  Beside G (A, `ASLOPe`) these two carry a second
coefficient B (`BSLOPe`, the 58503B's `0x102722`) and a DAC-to-ADC
ratio (`DADC`, `0x10271a`).  An EFC calibration in both sets them: in
the 58503B, the state machine `0x4d310` (called from `0x4c6b6`) steps
the EFC through ten points 116508 counts apart, measuring the
frequency offset at each (`point= %d delta_f= %e efc=  %d`), and fits a
straight line to them (`0x4d1c8`): the least-squares slope of offset
against EFC, (Σx·Σy − 10·Σxy) / ((Σx)² − 10·Σx²) in double precision,
becomes G, the intercept is not kept, and B is cleared, not computed
-- the `a= %e b= %e` line that follows prints that 0; measures the ratio at a low and a high EFC
(`low= %d high= %d dac/adc= %e`); and, finishing, replaces a with the
type's G when it falls outside the type's limits (`OCXO cal a out of
range`), reports it (`OCXO cal, a= %e`), clears B (`0x4d744`), saves,
and sets the loop's gain through `0x4d196` to A + B·u² at the EFC in
force u -- which, with B just cleared, is A.  Choosing the oscillator
type resets all three (G from the type's table, B to 0, the ratio to
16.0).  The older images have neither the
coefficients nor the calibration.  B's one use in arithmetic is the
measured holdover uncertainty: at each new aging fit, `0x45d56` runs
`0x45b26` over the last 32 samples, 24 hours, with the fit's
coefficients as they stood 32 samples back, summing for each later
sample (e − (a + b·y + c·ln y)) · (A + B·e²) · 2700 -- the old fit's
EFC error times an EFC-dependent gain times the window's seconds -- and
stores the result at `0x102bfc`, valid while `0x1032af` is set, before
saving the new fit's coefficients for the next run.
`:SYNChronization:HOLDover:TUNCertainty:MEASured?` returns it
(`0x4781c`).  The loop does not apply B: the gain setter `0x4c7fc` --
this image's counterpart of the Z3816A's `FUN_0004b022`, which derives
1/G and the clamp from G -- has two callers, the loop's start
(`0x4c924`), which passes G alone, and the end of the calibration,
which passes A + B·u² just after clearing B.  The `BSLOPe` setter only
stores and saves.  So nothing in the firmware computes B: both the
calibration and the type selection set it to 0, it changes only when
`:BSLOPe` sets it, and it enters only the measured-uncertainty sum.

**The Z3815A's extra stage.**  The Z3815A's state printer names one
stage more than the others, between `startup pll` and `normal pll`:
`n3 lock pll`, stage 6 by its jump table (`0x2c1d8`), so that normal
pll, diag, idle and fatal error are 7 to 10, with sub-states `sync1`, `wait1`, `wait2`, `measure1`
to `measure3`, `calc_freq`, `sync2`, `slew`, `leapsec`, `check_time`,
`pll`, `exit1` and `exit2` (names from `0x2cd9b` on); its log names the
stage `Went into PLL lock to other card`.  Around it the image runs a
card state of its own -- `[ Active ]`, `[ Standby ]`, `[ Unlocked ]`,
`[ Blocked ]` and `[ ALARM ]` on the status screen (`0x5973c` on) --
whose transitions it logs with a reason (`0x34f00` on): `ACT from
STBY`, `ACT from STBY time deviation`, `ACT from UNLOCK`, `STBY from
ACT` with `other card`, `time deviation` or `10 min hold`, `BLK from
ACT` with `other card`, `user` or `extractor`, `WARMUP from ACT, clock
off`, and `switchover ACT unstable`; it prints `locked_ticks`,
`standby_unlck_ticks` and the measured uncertainty (`tunc:meas`) beside
them.  So the Z3815A is built to work as one of two cards, the standby
one locking its oscillator to the other's; its console words
`n3_phase`, `n3_duart_res`, `set_mux` and the `s3_` words
(`pforth/models.md`) are its alone.  None of the other images has the
stage or the card states.

The Z3815A reads the word at `0x302000` as the Z3816A does, but bit 12
where the Z3816A reads bit 8, and with it chooses τ as well as G, in
two places.  `efc_calibration_defaults` (`0x45038`) gives `:ASLOPe`
(`0x1023c8`) +1.2 × 10⁻¹² with the bit set and −2.125 × 10⁻¹² with it
clear, and `:BSLOPe` (`0x1023cc`) 0, and a valid calibration block's A
and B stand in for them (`efc_calibration_read`, `0x4506c`).  Then
`pll_init` (`0x50b4c`) tests the bit again (`0x50bf2`): set, it makes τ
1000 s; clear, it makes τ 500 s (`0x50c0c`), clears `:BSLOPe`
(`0x50c14`), stores −2.125 × 10⁻¹² in `:ASLOPe` (`0x50c1c`) and clears
`efc_compensation` (`0x1021da`, `0x50c24`).  `system_init` reads the
block first (`0x22d4e`) and runs `pll_init` on every start that loads
the defaults (`0x22e8a`, `0x22eb8`), and `pll_restart` runs it at each
restart of the loop (`0x50c46`); only a warm start that restores the
preserved region skips it.  So with bit 12 set a stored calibration
stands in for the defaults, and with it clear it counts only from such
a warm start to the loop's next restart.  The loop takes G from
`:ASLOPe` as `pllp_task` starts (`0x50dd4`) and then as A + B·u² at the
EFC in force u (`efc_gain_from_calibration`, `0x52872`).

Channel names differ.  The `Oscillator current` table of "s, the
oscillator current" is in the Z3816A, Z3815A and 58503B; the Z3801A,
Z3805A and 58503A 3633 name `Double oven` instead, and 3704 neither.
The console word `xcal` is in the Z3816A only, and its `tempco = `
message in all but the Z3815A and 58503B.

### G and the calibration block

The calibration block is the EEPROM's 64 bytes at `0x400080`, its
first word a checksum, the byte sum of the other 62 bytes over its
complement, its floats big-endian IEEE singles.  In the Z3816A,
Z3801A, Z3805A 3543B and 58503A 3704 it holds the TC (`+0x2`) and the
time offset (`+0x6`), and in the Z3816A and 3704 a word at `+0xa`, the
3704's 1PPS cable delay: nothing of G's.  The Z3815A, 58503B, Z3805A
1.01.03 and Z3817A add an EFC calibration, DADC (`+0xc`), A (`+0x10`,
`:ASLOPe`) and B (`+0x14`, `:BSLOPe`), and the last three an
oscillator type or kind, the byte at `+0x1c` (the EEPROM's
`0x40009c`).  The table is made from `loop-gain.json` and the EEPROMs
in `third_party/firmware/images/eeprom/`, each decoded by its unit's
image's rules; the project's synthetic EEPROMs are listed there too,
marked `synthetic`, and decoded for neither table.  `make check-loop-gain`
(`tools/firmware/loop_gain.py`) reads every address after a value or
a clause from its image, decodes every unit's EEPROM afresh, and fails if
this table or `docs/pages/loop.html`'s "G and the EEPROM" is not what
they give (`make loop-gain` rewrites both).  u is the EFC in DAC
counts, 0 to 2²⁰.

<!-- loop_gain.py evidence -->
| Image | G comes from | Can the EEPROM change it? | The EEPROMs held |
| ----- | ------------ | ------------------------- | ---------------- |
| Z3816A 4001 | Bit 8 of the word at `0x302000`, read once as the loop starts (`0x4b14c`): set −1.25 × 10⁻¹², clear −2.125 × 10⁻¹² | No: G goes to the gain setter as an immediate (`0x4b166`); the block holds the TC, the time offset and a word at `+0xa`, nothing of G's (`0x4045c`) | None held |
| Z3815A 4010 | Bit 12 of the word at `0x302000`, which picks τ too (`0x45038`): set +1.2 × 10⁻¹², τ 1000 s; clear −2.125 × 10⁻¹², τ 500 s; then A + B·u² at the EFC in force u (`0x52872`) | With bit 12 set: a valid block's A and B stand in for the defaults (`0x4506c`), but with bit 12 clear `pll_init` (`0x50b4c`) reads the bit again (`0x50bf2`), sets τ to 500 s (`0x50c0c`), clears B (`0x50c14`), sets A to −2.125 × 10⁻¹² (`0x50c1c`) and clears `efc_compensation` (`0x50c24`); it runs after the block is read (`0x22d4e`), on every start that loads the defaults (`0x22e8a`, `0x22eb8`) and at each restart of the loop (`0x50c46`); only a warm start that restores the preserved region skips it, until the loop restarts | None held |
| Z3801A 3543 | A constant, +6.25 × 10⁻¹³ (`0x475be`) | No: the block holds the TC and the time offset alone (`0x2fb14`), written only by those two parameters' done hooks (`0x2fb56`) | [`z3801a-3542A01548-eeprom.bin`](../../third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin): block erased; G +6.25 × 10⁻¹³, the image's |
| Z3805A 3543B | A constant, +6.25 × 10⁻¹³ (`0x475f4`) | No: the block holds the TC and the time offset alone (`0x30c1e`), written only by those two parameters' done hooks (`0x30c60`) | [`z3805a-3625A01487-eeprom.bin`](../../third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin): block erased; G +6.25 × 10⁻¹³, the image's |
| 58503A 3704 | A constant, +6.25 × 10⁻¹³ (`0x477dc`) | No: the block holds the TC, the time offset and the 1PPS cable delay alone (`0x30ba8`), written only by those parameters' done hooks (`0x30b24`) | [`58503a-3710A01056-eeprom.bin`](../../third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin): block valid: TC −33.65, time offset 0, cable delay 0; G +6.25 × 10⁻¹³, the image's |
| 58503B 1.01.04 | The oscillator type's entry in the table at `0x40dfc` (`0x40b8a`): type 0 −1.25 × 10⁻¹², type 1 +6.25 × 10⁻¹³ | Yes: a valid block's A replaces the type's as the loop starts (`0x4c91e`); its B is never applied.  The type is the block's byte at `+0x1c`, 0 if erased or past 1 (`0x40c62`, `0x40c66`); the EFC part is valid when the checksum holds (`0x40cf0`), none of DADC, A and B has all of bits `0xe8000000` set (`0x40baa`, `0x40e14`), A is not 0, and the type byte is the type or erased (`0x40bf4`) | [`58503b-L00000-eeprom.bin`](../../third_party/firmware/images/eeprom/58503b-L00000-eeprom.bin): block valid, type 0, A −6.684 × 10⁻¹³: G −6.684 × 10⁻¹³ in place of the type's −1.25 × 10⁻¹² |
| Z3805A 1.01.03 | The 58503B's code, the type's entry in the table at `0x329ec` (`0x3277a`): type 0 +6.25 × 10⁻¹³, type 1 +9.238 × 10⁻¹³ | Yes: as in the 58503B, a valid block's A (`0x3279a`) replaces the type's as the loop starts (`0x48094`) | None held |
| Z3817A 3752 | The oscillator kind, the block's byte at `+0x1c`, EEPROM `0x40009c`, 0 when erased or the block bad (`0x418ec`, `0x41910`): kind 0 A +5 × 10⁻¹³ and B +3.3 × 10⁻²⁴, τ 1000 s; any other A −2.125 × 10⁻¹² and B 0, τ 500 s for kind 1 (`0x417fa`); G = A + B·u² at the EFC in force u, each block (`0x4d76c`) | Yes: a valid block's A and B replace the defaults, valid as in the 58503B (`0x41834`, `0x41a18`) | [`z3817a-3832A03064-eeprom.bin`](../../third_party/firmware/images/eeprom/z3817a-3832A03064-eeprom.bin): block valid, kind 0, A +4.828 × 10⁻¹³ and B +1.941 × 10⁻²⁴: G +4.828 × 10⁻¹³ at EFC 0 and +1.016 × 10⁻¹² at u = 2¹⁹, where the defaults give +5 × 10⁻¹³ and +1.407 × 10⁻¹²; τ 1000 s |
<!-- end loop_gain.py -->

Of the EEPROMs held, the Z3817A's and the 58503B's carry an EFC
calibration; their bytes do not say whether a calibration measured it
or `:ASLOPe` and `:BSLOPe` set it.

### The update in the Z3805A 3543B

`pll_normal` keeps its block in `loop_steering` (`0x10205e`,
`structures.md`) and its error and filtered error in `pll`
(`0x102162`); τ is `max_loop_time` (`0x101e84`) and G
`efc_gains.gain` (`0x102554`).

| Symbol | Where | |
| ------ | ----- | - |
| x̄ | `loop_steering.ti_sum` | the ten seconds' valid readings, then their mean, copied to `interval_mean` |
| x₀ | `0x10254a` | the setpoint, which the console word `phase_off` sets |
| f | `pll.ti_filtered` | |
| a | `loop_steering.filter_weight` | 29.75 / τ |
| M | `efc_gains.step_limit` | 6.25 × 10⁻¹⁰ / \|G\| |
| k | `loop_steering.integral_gain` | (1/G) / τ² / 4 |
| I | `loop_steering.integral` | |
| K | `loop_steering.proportional_gain` | (1/G) / τ |
| B | `pll.efc_base` | |

`startup_pll` uses the same block, and counts in
`settled_blocks` the consecutive updates whose mean is within
150 ns, to 15; one outside clears it.

The mean is of that ten seconds' readings within the holdover limit
threshold, not always ten.  A ten seconds with none adds a hold cause
-- a TI error, 4, if a reading failed, else the limit exceeded, 8
(`0x44b88` to `0x44ba0`) -- and still updates, with the mean taken as
0: e = −x₀, and f, I and u move.  The loop clamps u to 0 to 1048560
(`efc_clamp`, `0x2426e`), and the health monitor's task, not `pllp`,
writes it on each tick: `efc_write_dithered` (`0x2421c`) rounds it,
adds a small random dither and sends the top 16 of its 20 bits to the
DAC.

The 58503A 3704 runs the same update (`pll_normal`, `0x4495a`;
`efc_write_dithered`, `0x23540`), with two differences: s is channel 6
("s, the oscillator current"), and while the GPS reference is
questionable its readings are not summed, and a ten seconds with none
skips the update, the EFC left as it was: without a hold cause if a
second in it was questionable, else with the Z3805A's 4 or 8
(`0x44c0a` to `0x44c32`).

## Simulating it

`smartclock-cli simulate-loop` runs `pll_normal`'s update ("The
loop") at an image's τ and G from `loop-constants.json`, closed around
a model oscillator, against a disturbance, and prints TSV: the second,
x̄ in nanoseconds (`ti_ns`, what the TI queries return) and u in
percent (`efc_percent`, `:EFControl:RELative?`'s scale, 2²⁰ counts
from −100 to +100 %).  The law is the tests' (`smartclock::discipline`,
"How this is checked"), in double precision.  Each second the time
interval moves by y − S·(u − u₀): y the free-running fractional
frequency offset, u₀ the EFC the loop starts at, `--efc`, and S the
oscillator's sensitivity, `--sensitivity`, in fractional frequency an
EFC percent and G's sign, by default G's.  A change of u acts from the
second after the update.  The loop starts at rest, its time interval
0, or at `--ti` nanoseconds with its filtered error f there; either
way u starts at u₀, I at 0 and B takes the rest.

    smartclock-cli simulate-loop --image 58503a-3704 --seconds 10000 \
        --efc 35 --frequency-step 600:-1e-9

Disturbances, any number of each, at AT seconds: `--phase-step
AT:NANOSECONDS`, `--frequency-step AT:SIZE` and `--drift AT:RATE`, a
rate a day.  `--aging-slope`, EFC percent a day, feeds a constant d to
the integrator in place of the aging fit's, clamped to ±M.  An image
with two τ or two G, by a switch or an oscillator type, needs `--tau`
or `--gain`.  With `--log` and `--from`, the latest row of a snapshot
TSV with columns `at` (nanoseconds since the Unix epoch),
`time_interval_s` and `efc_percent` at each update, within a minute,
is printed beside it, for comparing a logged receiver with the law.

The update's c·s term ("s, the oscillator current") is in: c is
`--tc-coefficient` or, in a replay, by default the log's at each
update, s the oscillator current, `--oven-current` held constant or,
in a replay, the log's.  s is the health monitor's average,
s ← 0.1·fresh + 0.9·s, taken once an update, the ten-second pass
`../efc.md`, "The regression", infers.  B is set to u − c·s at the
start, as on the loop's entry, so a constant s moves nothing and c
acts only through changes of s.

The aging feed-forward: `--fit-b` and `--fit-c` set the fit's b and c
the loop holds, r and p, in EFC counts a window and a unit of w, as
the `curv` debug line prints them ("The aging fit"), and `--uptime` q
at the start, so that d is p·2700/q + r, within ±M, at each update.
`--aging-fit` runs the fit itself from an empty history, as a
power-up leaves it, though at the uptime `--uptime` gives: the sampler sums u − c·s each second, each
2700 s of uptime writes an entry with its weight, y and w, the fit
runs at once, and the loop takes its b and c as r and p the second
after.  Until the first fit, r and p are `--fit-b` and `--fit-c`, 0 by
default.  It is the 58503A 3704's fit and sampler only, transcribed in
`smartclock::aging` (`crates/smartclock/src/aging.rs`) from that
image's hand-written C (`rewritten/58503a-3704/pll_aging.c` and
`rewritten/common/pll_aging.c`), which the function check holds to the
image (`rewrite.md`); the Z3805A's sampler times an entry by its
samples, and the Z3815A fits halves of 32.  The output gains the d fed
at each update, in EFC percent a day, and the fit's mode.  HQ is not
computed.  Two things the code does not settle are left as they fall:

- How the image's soft-float rounds is not established (`softfloat.md`
  does not say); the transcription rounds to nearest.  It matters in
  the sampler, which sums 2700 values at the EFC's level, hundreds of
  thousands of counts, in a single-precision float (`efc_sum`): the
  sum's rounding moves an entry's mean by an amount that varies with
  the level, so the fit's slope depends on the rounding mode.
- Where `fit_log_search` turns down without taking a step down, the
  image takes a vertex through a point it never set
  (`rewritten/common/pll_aging.c`); the simulator stops there with an
  error rather than guess.

**Replaying a logged run.**  `--replay`, with `--log` and `--from`,
starts the loop where the log stood at `--from` and drives it with
what the logged oscillator did, for asking what the time interval
would have been with another c, aging slope or disturbance.  The free
oscillator is recovered from the log, each second's y being the
logged time interval's change plus S·(u_log − u₀), u_log the logged
EFC in force; run back through the model, that makes the simulated
time interval the logged one plus the sum of −S·(u − u_log) since the
start, u the simulated EFC.  The replay computes it that way, as a
difference from the log, so it needs neither u₀ nor the phase within a
block, only each update's logged x̄ and u.  It starts with u the
logged EFC at `--from`, f the prefilter run over the logged x̄ of the
five filter time constants, 5/a updates, before it, s the monitor's
average of the log's oscillator current over the same updates, I at 0
and B taking the rest, so u holds.  Synthetic disturbances add to the
recovered one.

c is the log's at each update, the latest `oven_tempco` at or before
it, unless `--tc-coefficient` holds one c for the whole run.  The
receiver takes a new c at its next update without recomputing B
(`console.md`, "The 58503A image"), so u steps by Δc·s there, and the
replay with the log's c takes the same step at the same update and
reproduces the log across it.  With `--tc-coefficient` the loop holds
its c through the change and does not take the step.  The step is in
u_log, which the recovery of the free oscillator adds back through S,
so the oscillator the replay runs is the same either side of it, and
the replay is the run that never changed c: its departure from the log
after the change is minus the logged loop's response to the step, the
time interval the step pushed and the loop's pull back from it.  A
change the receiver made between two of the daemon's readings of
`oven_tempco` reaches the log late, and the replay takes the step as
late, departing from the log until it does.

The log's columns, by what needs them:

| Column | Needed |
| ------ | ------ |
| `at`, `time_interval_s`, `efc_percent` | always |
| `oven_tempco` | by a replay without `--tc-coefficient` and by `--estimate-aging`: the latest at or before each update is c |
| `oven_current` | by a replay with c not 0 at some update and without `--oven-current`, and by `--estimate-aging` where the log's c is not 0: the latest at or before each update is the fresh reading fed to s |
| `ffom` | read where present; a replayed update whose row is not 0, `normal pll`, stops the replay; `--estimate-aging` takes no estimate from it |

What a replay assumes:

- S.  With the c and the aging slope the receiver ran, the replay
  reproduces the logged time interval whatever S is given, because the
  oscillator is recovered through S and run back through it: a
  reproduction does not test S.  A counterfactual's departure from the
  log is −S·(u − u_log) summed, closed through the loop, so it scales
  with S; give the oscillator's own (`../efc.md`, "The pull,
  measured"), not G's, where it is known.
- The aging slope.  The receiver's d comes from its aging fit, whose
  p, r and q the log does not hold; reproducing the log needs the d
  the receiver had, from `--fit-b`, `--fit-c` and `--uptime`, or
  estimated from the log with `--estimate-aging` (below), and a
  difference in d shows as a drift between the two.  The fit's history
  is not in the log either, so `--aging-fit` can replay a cleared fit
  but not the receiver's own as it went on.
- The counter's noise stays in the logged x̄ the replay adds to, so the
  simulated loop sees the readings the receiver saw; that is exact for
  noise added to the reading, whatever u does.
- The log's rows.  x̄ and u are taken at each update, every 10 s from
  `--from`, from the latest row within a minute before it; an offset
  between that grid and the receiver's updates delays every row
  alike, by less than an update.  A minute without a row stops the
  replay; it does not bridge a gap.  The log's `oven_current` is a
  fresh conversion (`:DIAGnostic:ROSCillator:CURRent?`), read less
  often than every update and held between readings; the receiver
  averages its own readings, so s is an estimate of the receiver's.

**Estimating the aging slope from the log.**  `--estimate-aging`, with
`--log` and `--from`, finds the d the receiver fed its integrator by
running the update backward.  B is fixed within the stage, so B + I is
u − c·s − K·f, and I grows by 10·k·f + 10·d/2700 an update:

    d = 2700·(Δ(u − c·s − K·f)/10 − k·f)

u the logged EFC, c the log's `oven_tempco` at each update, s the
monitor's average of the log's `oven_current` formed as the replay
forms it, and f the prefilter run over the logged x̄.  It takes K and
k from the image's τ and G, which the firmware holds exactly; S does
not enter.  A change of c in the log moves u by Δc·s at once, and the
c·s term takes it out.  The output gains the estimate at each update,
`estimated_aging_slope_percent_per_day`, EFC percent a day like
`--aging-slope`; a replay feeds it to the loop as d, r the estimate
and p 0, within ±M, in place of `--fit-b` and `--fit-c`, and starts
from the f and s the estimate rebuilt at `--from`, so that the two
agree.

The estimate reproduces a log; it does not show what another c would
have done.  The logged EFC already holds the loop's answer to the c the
receiver ran, the estimate takes that into d, and a replay with
`--estimate-aging` and another `--tc-coefficient` departs little from
the log.  A change of c at one moment is a frequency step of S·Δc·s:
`--frequency-step` with that shows its transient.

One update's estimate alone differences a rounded number: an error of
one count in u is 270 counts a window in d.  u is a whole DAC count,
and `:EFControl:RELative?` reports four decimals of a percent, about
half a count (`../efc.md`); together they leave an error of about a
third of a count rms in each logged u.  An update's estimate is therefore
the mean of the updates' within half of `--aging-window` either side.
The changes of B + I in that mean add up to its change across the
window, so the rounding enters only at the window's two ends and the
mean's error is √2·σ·270/N counts a window rms, σ that error and N the
updates in the window.  The default window is 2700 s, the aging fit's
own: a new fit can change d at most once in it, so the mean smears a
fit's step over one window and no further, and its 271 updates bring
the rounding's share down to about √2·σ, under half a count a window.
A longer window lowers that and smears steps further; `--aging-window
10` gives each update's estimate alone.  An error δd in the d fed to a
replay stands its time interval off from the log's by
4·G·τ²·δd/2700, as a drift does (below).

What the log must hold, and what its rows do to the estimate:

- Each update needs a row of its own, logged in the 10 s before it; a
  log whose rows come less often gives no estimate.  The grid of
  updates is `--from`'s, not the receiver's, so a row may stand for
  the update before; where that changes from one update to the next,
  one update is counted twice and its neighbor missed, which moves
  the window's sum only by 10·k times the difference of their f,
  since the changes of B + I still add up to the change across it.
- A row missing, out of `normal pll` (`ffom` not 0) or with u at
  either end of its range, which the loop clamps u to, breaks the
  walk: f and s start again after it, and no update's estimate is
  taken until the prefilter has run five time constants, which leaves
  e⁻⁵ of its starting error.  An update's estimate needs more than
  half the window's updates; with fewer, as across a gap, there is
  none, and a replay stops there.  The row before a gap can stand for
  the update after it, whose d is then missed from the window's mean.
- `oven_tempco` is required: the estimate needs the receiver's c, not
  `--tc-coefficient`'s.  A wrong c, or an s off from the receiver's,
  moves B + I and so the estimate, by 270/N times the change of the
  error in c·s across the window.
- d beyond ±M was applied at ±M, and the estimate finds the value
  applied.

What a replay on the estimate shows:

- Fed each update's estimate alone, a replay with the log's c
  reproduces the log by construction, whatever d was: the estimate
  takes up every change of B + I the law does not explain -- the
  receiver's d, and with it an error in s, a c the log does not hold
  or the DAC's rounding -- so the reproduction tests nothing.  Fed the
  window's mean, it departs from the log by what the mean does not
  explain, which tests that the integrator's unexplained climb is slow,
  as an aging slope is.
- With another c the replay is still a difference from the log, and
  the law is linear, so a d equal to the receiver's cancels and the
  departure is the response to the change of c alone; the estimate
  keeps a wrong d from adding a drift to it.  It holds d as the
  receiver had it, which the receiver with the other c would not have
  kept: its fit samples u − c·s, which moves with c through what the
  integrator takes up, so over a span in which the fit would have
  reacted, a new entry each 2700 s in a ring of 64, the counterfactual
  assumes a d the other receiver would not have had.

From the law, as `crates/smartclock/src/discipline.rs`'s tests check:
a frequency step Δy moves u by Δy / S and the time interval back to 0;
a drift D, held by the integrator, stands the time interval at
f = 4·G·τ²·(D/S − d/2700), since k·f + d/2700 must climb by D/S a
second; a phase step leaves u where it was; c·s takes up a change of
frequency that follows the current, but for the average's lag; the
aging fit learns a drift and takes the standing error down.  What it
leaves out: `startup_pll` and holdover; the aging fit of any image but
the 58503A 3704, and its HQ; the EFC's clamp and the DAC's dither;
outside a replay, the counter's noise.  The Z3815A, 58503B and Z3805A
1.01.03 ease the aging slope in, which a constant d does not model,
and the Z3817A's G moves with u and its integral's step with the block
("In every image").

## How this is checked

Until October 2026 the tests compared this document with a copy of
its own model.  A review then found four errors: the sign of the
error, poles computed without the ten-second mean, the integrator's
rescaling in start-up, and the aging fit's description.  The tests now
compare it with the Z3805A 3543B's and 58503A 3704's images
(`crates/smartclock/tests/loop_firmware.rs`):

- They read each constant above from its image at the cited address,
  and match it to that receiver's row.
- `tools/firmware/loop_trace.py` runs each image's own `startup_pll`
  and `pll_normal` in an emulator, once per second of synthetic readings.
  The image's soft-float runs as it is; only the routines that reach
  outside the loop are stood in for, and any other call fails the run.
  `make check-loop-trace` regenerates the traces and compares them with
  the fixtures in `crates/smartclock/tests/fixtures/loop/`.
- A replay of the update law as written here must reproduce the traced
  f, I, B, u, constant and stage at every step, about 3000 per image.
  The law it replays is the library's, `smartclock::discipline`
  (`crates/smartclock/src/discipline.rs`), run in single precision as
  the firmware runs it, and the tests hold that module's constants to
  the ones above.
  The run covers the waits, the ramp, the hand-over, the aging slope's
  clamp and blocks with no reading.  Each of the first three errors
  fails it.
- The poles, damping and step response in `loop.html` are recomputed
  from the traced law, taking a change of EFC as acting from the next
  second; the DAC's latency is not modeled.  The page's statement
  that `pll_normal`'s first update after the hand-over moves u by
  about K·f is checked against the traces.
- The page's start-up times, in "What the receiver shows", are summed
  from the ROMs' waits ("The FFOM"'s table), the traced ramp and the
  law's settling, and its FFOM by stage is read from every image's
  `get_ffom`.

The other models' rows are read by hand and not checked this way,
but for their constants: `make check-loop-constants`
(`tools/firmware/loop_constants.py`) reads each τ, startup constant,
G and holdover-recovery threshold that `loop-constants.json` cites,
in every image, from the image, and fails if one differs or if the
"In every image" table here or `docs/pages/loop.html`'s "By model"
table is not what that file gives; `make check-loop-gain`
(`tools/firmware/loop_gain.py`) does the same for "G and the
calibration block" and decodes every EEPROM held afresh.  The page's a, slowest pole and
startup ramp it computes from each τ, as the tests above do.

## The soft-float arithmetic and its constants

A software floating-point library does the arithmetic; the decompiler
shows it only as calls with the operands hidden.  Each routine was
identified by running it in an emulator on known inputs
(`softfloat.md` has the routines and their operands in every image),
and the update above was transcribed from the disassembly of
`0x4850c` to `0x48674`.

The constants, as single-precision floats: `0x41ee0000` is 29.75,
`0x4528c000` 2700, `0x40800000` 4, `0x41200000` 10, `0x302bcc77`
6.25 × 10⁻¹⁰, `0x29824fff` 5.787 × 10⁻¹⁴, and `0x4e6e6b28` 10⁹, which
scales the interval to nanoseconds for the report.  In the aging fit:
`0x40a51800 00000000` is the double 2700, `0xbff00000 00000000` the
double −1, `0x3f400000` 0.75, `0x44800000` 1024, `0xc2020000` −32.5,
`0x42780000` 62, `0x42000000` 32, `0x39e27e0f` 4.32 × 10⁻⁴, and the
integers `0xa8c` 2700, `0x2a3` 675, `0x2a30` 10800.
