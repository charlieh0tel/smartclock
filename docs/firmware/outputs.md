# The pulse, time-tag and output commands

The 59551A of `097-59551-02` adds to the 58503A a programmable pulse
output, time-tag inputs and an IRIG-B output.  What the firmware does
with the commands for them, read in 58503A 3704 unless another image
is named.

## Present and stubbed in every image

`:SOURce:PULSe`, `:SENSe:DATA` and `:SENSe:TST1` to `TST4`,
`:OUTPut:PIN1` to `PIN8` and `:SOURce:PTIMe:PPS:EDGE` are in all seven
command trees (`../scpi/`), the Z380x's included, so their presence
says nothing about a unit's hardware.

- The programmable pulse stores its settings and drives nothing.  The
  `PERiod` and `CONTinuous:STATe` setters (`0x2da0a`, `0x2da70`) post
  records whose cells are `0x10223e` (period), `0x10223c` (state) and
  `0x10224e` (start); the record's function `0x47faa` gathers them
  and calls `0x47fa8`, a bare `rts`.  The same empty routine is in
  3633 (`0x47e82`), the 58503B (`0x4d126`), the Z3815A (`0x4be5e`), the
  Z3816A (`0x4646a`) and the Z3801A (`0x42b46`).
- `:PULSe:REFerence:EDGE`, `:PTIMe:PPS:EDGE` and `:SENSe:TSTn:EDGE`
  store nothing: their queries share the stub `0x28d70`, and their
  setters call `0x2bcfa(3, value)`, which for some values posts
  message 3 of the table at `0x2bd2a`, `Undefined header`, and
  otherwise returns.
- `:OUTPut:PINn:DELay` and `:FREQuency` are stubs: the setters
  (`0x2bf42`, `0x2bfae`) are `moveq #$e0,d0; rts`, and the queries go
  to the same stub.
- `:SENSe:DATA?` (`0x2eb8a`) reads a buffer through `0x2e84a`; where
  that buffer is filled was not traced.

## What the 58503A drives

- The FPGA at `0x304000` (chip select 5) is the interval counter and
  its interpolator.  Its write routine is `0x40784`, with five byte
  shadows at `0x1022bb` to `0x1022bf`; the field table at `0x413ac`
  has seven fields, none a period or a divider, and every caller is
  the interpolator module (`0x40000` to `0x41000`), the loop's
  interval reads (`0x44fba` to `0x45266`) or a pForth word
  (`0x1b2c4`).
- The latch at `0x300000` (chip select 1, shadow `0x100e0e`): a 3-bit
  field the self-test and health code set (`0x186ac`, `0x23b7e`), and
  bits 6 and 7 the front-panel code sets (`0x53750`).
- Bits 4 to 2 of PORTGP, which 3633's DUART initialization writes
  (`0x1d5cc`, with 0) and no 3704 code does (`revisions.md`).  What
  those pins drive was not traced.
- Nothing sets the rate of the 1 PPS.

So no 58503A image drives a second pulse channel, a pulse period or a
time-tag input.  None has a two-second pulse mode either: no `PP2S`,
`ESEC` or option word selects one, and no setting reaches the
hardware.  The Z3815A alone names a PP2S, as a signal its health
monitor watches (bit 0x104 of the table at `0x5964a`; `PP2S Fail`
from `0x3429e`), not one it makes.

Not traced: what drives the alarm output, and the time-code worker's
output path (`0x13d28`).
