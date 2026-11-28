| The relay `smartclock-cli passthrough` compiles into the pForth
| console's dictionary: a byte relay between the host's port and the GPS
| engine's, DUART channel A.  Assembled with GNU as for the CPU32;
| `make check-relay` holds relay.bin, which the CLI embeds, to it.
|
| Forth call:  <first> <second> relay    (second 0: none; never returns)
|
| A setting is MR1 << 24 | CSR << 16 | SCCR1-high << 8 | SCBR: the DUART
| channels' MR1 and clock select (ACR as the firmware leaves it, 0xa0,
| baud-rate set 2; MC68681 Table 4-5), and the SCI's PE/M byte and baud
| divisor (MC68331UM, SCCR0 and SCCR1).  9600 8N1 = 0x13bb0037,
| 19200 8E1 = 0x03cc061b.
|
| Masks every interrupt (IPL 7) and every DUART interrupt.  Host ports:
| DUART channel B if its transmitter is enabled, the SCI if SCCR1 has TE
| and RE.  Applies the first setting to channel A and to each host port,
| then sends '!' to each host port, again every RESEND_LOOPS passes,
| until a clean 0x00 arrives on one, which latches it: the other's bytes
| are dropped and output goes to the latched port alone.  Host bytes go
| to the engine and engine bytes to the host, each through a 32-byte
| ring.  A break from the host's port: with a second setting left, at
| the break's end the rings are drained, the setting is applied to both
| sides and 'E' is sent at the new setting; with none, 'R' is sent and
| SWSR is no longer serviced, so the SIM's watchdog resets the unit.
| SWSR is serviced once a pass otherwise.  trap #7 returns in supervisor
| mode, as it does to the firmware's `enter_supervisor`
| (docs/firmware/README.md).
	.equ DUART, 0x200000
	.equ SRA, 0x03
	.equ CRA, 0x05
	.equ TBA, 0x07			| RBA on read
	.equ IMR, 0x0b			| ISR on read
	.equ CRB, 0x15
	.equ SRB, 0x13
	.equ TBB, 0x17			| RBB on read
	.equ QSM, 0xfffc00
	.equ SCCR0, 0x08
	.equ SCCR1, 0x0a
	.equ SCSR, 0x0c
	.equ SCDR, 0x0e
	.equ SWSR, 0xfffa27
	.equ WAIT_LOOPS, 0x8000		| bounds the start's waits
	.equ RESEND_LOOPS, 0x6000	| '!' again until the host latches
	.equ RING_MASK, 0x1f		| each ring 32 bytes
	.equ RING, 32
	.equ MR2_1STOP, 0x07
	.equ SCCR1_TE_RE, 0x000c
	.equ SR_RXRDY, 0
	.equ SR_TXRDY, 2
	.equ SR_TXEMT, 3
	.equ ISR_BREAK_B, 6
	.equ SCSR_TDRE_HIGH, 0		| bit 8, in SCSR's high byte
	.equ SCSR_TC, 7
	.equ SCSR_RDRF, 6
	.equ SCSR_IDLE, 4
	.equ SCSR_FE, 1
| bits of d7's low byte; its high word counts down to the next '!'
	.equ PORT_B, 0
	.equ PORT_SCI, 1
	.equ LATCHED, 7

| a0 DUART, a2 QSM, a1 host-to-engine ring, a3 engine-to-host ring,
| d1/d2 its head and tail, d5/d4 the other's, d6 the second setting,
| d7 state, d0 d3 a4 scratch

	.text
relay:
	move.l (%a6)+,%d6		| second setting
	move.l (%a6)+,%d5		| first setting
	trap #7				| supervisor
	ori.w #0x0700,%sr		| IPL 7
	lea DUART,%a0
	lea QSM,%a2
	clr.b IMR(%a0)			| no DUART interrupts
	moveq #0,%d7
	moveq #SR_TXRDY,%d0		| DUART B in use if TxRDY comes
	lea SRB(%a0),%a4
	bsr.w waitbit
	beq.s 1f
	bset #PORT_B,%d7
	moveq #SR_TXEMT,%d0		| the console's last byte out
	bsr.w waitbit
1:	move.w SCCR1(%a2),%d0		| the SCI in use if TE and RE
	and.w #SCCR1_TE_RE,%d0
	cmp.w #SCCR1_TE_RE,%d0
	bne.s 2f
	bset #PORT_SCI,%d7
	moveq #SCSR_TC,%d0
	lea SCSR+1(%a2),%a4
	bsr.w waitbit
2:	moveq #SR_TXEMT,%d0		| channel A's last byte out
	lea SRA(%a0),%a4
	bsr.w waitbit
	move.l %d5,%d0
	bsr.w apply
	lea buf(%pc),%a1
	lea RING(%a1),%a3
	moveq #0,%d1
	moveq #0,%d2
	moveq #0,%d4
	moveq #0,%d5
	moveq #0x21,%d0			| '!'
	bsr.w say
	swap %d7
	move.w #RESEND_LOOPS,%d7
	swap %d7
loop:	bsr.w kick
	btst #LATCHED,%d7
	bne.s 4f
	swap %d7
	subq.w #1,%d7
	bne.s 3f
	move.w #RESEND_LOOPS,%d7
	swap %d7
	moveq #0x21,%d0			| '!' again
	bsr.w say
	bra.s 4f
3:	swap %d7
4:	btst #PORT_B,%d7		| host on DUART B
	beq.s 6f
	move.b SRB(%a0),%d3
	btst #SR_RXRDY,%d3
	beq.s 6f
	tst.b %d3
	bmi.w brkb			| received break
	bsr.w full
	beq.s 6f			| ring full: leave it in the FIFO
	move.b TBB(%a0),%d0
	moveq #PORT_B,%d3
	bsr.w got
6:	btst #PORT_SCI,%d7		| host on the SCI
	beq.s 7f
	btst #SCSR_RDRF,SCSR+1(%a2)
	beq.s 7f
	bsr.w full
	beq.s 7f
	move.w SCSR(%a2),%d3
	move.w SCDR(%a2),%d0
	btst #SCSR_FE,%d3
	beq.s 61f
	tst.b %d0
	beq.w brksci			| framing error and 0: a break
61:	moveq #PORT_SCI,%d3
	bsr.w got
7:	bsr.w toengine
	btst #SR_RXRDY,SRA(%a0)		| engine into its ring
	beq.s 8f
	move.b %d5,%d0
	addq.b #1,%d0
	and.b #RING_MASK,%d0
	cmp.b %d0,%d4
	beq.s 8f			| ring full: leave it in the FIFO
	move.b TBA(%a0),(%a3,%d5.w)
	addq.b #1,%d5
	and.b #RING_MASK,%d5
8:	bsr.w tohost
	bra.w loop

| one byte from the host ring to the engine if it can take one;
| Z set when the ring is empty
toengine:
	cmp.b %d1,%d2
	beq.s 1f
	btst #SR_TXRDY,SRA(%a0)
	beq.s 2f
	move.b (%a1,%d2.w),TBA(%a0)
	addq.b #1,%d2
	and.b #RING_MASK,%d2
2:	andi.b #0xfb,%ccr		| clear Z: not empty
1:	rts

| one byte from the engine ring to the host if every port in use can
| take one; Z set when the ring is empty
tohost:	cmp.b %d5,%d4
	beq.s 1f
	btst #PORT_B,%d7
	beq.s 2f
	btst #SR_TXRDY,SRB(%a0)
	beq.s 3f
2:	btst #PORT_SCI,%d7
	beq.s 4f
	btst #SCSR_TDRE_HIGH,SCSR(%a2)
	beq.s 3f
4:	move.b (%a3,%d4.w),%d0
	addq.b #1,%d4
	and.b #RING_MASK,%d4
	bsr.w say
3:	andi.b #0xfb,%ccr		| clear Z: not empty
1:	rts

| Z set when the host-to-engine ring is full
full:	move.b %d1,%d0
	addq.b #1,%d0
	and.b #RING_MASK,%d0
	cmp.b %d0,%d2
	rts

| d0 = byte from host port d3: latch on a clean 0x00, then into the ring
got:	btst #LATCHED,%d7
	bne.s 1f
	tst.b %d0
	bne.s 2f			| not latched: only 0x00 latches
	bsr.w latch
	moveq #0,%d0
1:	move.b %d0,(%a1,%d1.w)
	addq.b #1,%d1
	and.b #RING_MASK,%d1
2:	rts

| latch host port d3 (keeps d7's high word)
latch:	move.b %d7,%d0
	and.b #0xfc,%d0			| clear PORT_B and PORT_SCI
	bset %d3,%d0
	bset #LATCHED,%d0
	move.b %d0,%d7
	rts

kick:	move.b #0x55,SWSR
	move.b #0xaa,SWSR
	rts

| waits up to WAIT_LOOPS passes for bit d0 of the byte at a4; Z clear if
| it came
waitbit:
	move.l #WAIT_LOOPS,%d3
1:	bsr.w kick
	btst %d0,(%a4)
	bne.s 2f
	subq.l #1,%d3
	bne.s 1b
2:	rts

| d0 = a setting: channel A, and each host port in use
apply:	move.l %d0,%d3
	swap %d3			| MR1 << 8 | CSR
	lea CRA(%a0),%a4
	bsr.w mode
	btst #PORT_B,%d7
	beq.s 1f
	lea CRB(%a0),%a4
	bsr.w mode
1:	btst #PORT_SCI,%d7
	beq.s 2f
	move.w %d0,%d3
	and.w #0x00ff,%d3
	move.w %d3,SCCR0(%a2)		| SCBR
	move.w %d0,%d3
	and.w #0xff00,%d3
	or.w #SCCR1_TE_RE,%d3
	move.w %d3,SCCR1(%a2)		| framing, no interrupts
	move.w SCSR(%a2),%d3		| clear its flags
	move.w SCDR(%a2),%d3
2:	rts

| d3.w = MR1 << 8 | CSR, a4 = the channel's CR: reset, MR1, MR2, CSR, enable
mode:	move.b #0x20,(%a4)
	move.b #0x30,(%a4)
	move.b #0x10,(%a4)
	move.w %d3,-(%sp)
	move.b (%sp)+,-4(%a4)		| MR1, the word's high byte
	move.b #MR2_1STOP,-4(%a4)
	move.b %d3,-2(%a4)		| CSR
	move.b #0x05,(%a4)
	rts

| d0 = byte to every host port in use
say:	btst #PORT_B,%d7
	beq.s 2f
1:	btst #SR_TXRDY,SRB(%a0)
	beq.s 1b
	move.b %d0,TBB(%a0)
2:	btst #PORT_SCI,%d7
	beq.s 4f
3:	btst #SCSR_TDRE_HIGH,SCSR(%a2)
	beq.s 3b
	move.w %d0,SCDR(%a2)
4:	rts

brkb:	move.b TBB(%a0),%d0		| pop the break's 0x00
	move.b #0x50,CRB(%a0)		| reset break change
1:	bsr.w kick			| wait for the break's end
	btst #ISR_BREAK_B,IMR(%a0)
	beq.s 1b
	move.b #0x50,CRB(%a0)
	move.b #0x40,CRB(%a0)		| reset error status
	moveq #PORT_B,%d3
	bra.s brk
brksci:
1:	bsr.w kick			| wait for the line to idle
	btst #SCSR_IDLE,SCSR+1(%a2)
	beq.s 1b
	move.w SCSR(%a2),%d0
	move.w SCDR(%a2),%d0
	moveq #PORT_SCI,%d3
brk:	btst #LATCHED,%d7		| a break latches its port too
	bne.s 1f
	bsr.w latch
1:	btst %d3,%d7
	beq.w loop			| not the host's port: ignored
	tst.l %d6
	beq.s ended
2:	bsr.w kick			| drain both rings
	bsr.w toengine
	sne %d0
	move.b %d0,-(%sp)
	bsr.w tohost
	sne %d0
	or.b (%sp)+,%d0
	bne.s 2b
3:	btst #SR_TXEMT,SRA(%a0)		| both sides idle
	beq.s 3b
	btst #PORT_B,%d7
	beq.s 4f
	btst #SR_TXEMT,SRB(%a0)
	beq.s 3b
4:	btst #PORT_SCI,%d7
	beq.s 5f
	btst #SCSR_TC,SCSR+1(%a2)
	beq.s 3b
5:	move.l %d6,%d0
	moveq #0,%d6
	bsr.w apply
	moveq #0x45,%d0			| 'E', at the new setting
	bsr.w say
	bra.w loop

ended:	moveq #0x52,%d0			| 'R'
	bsr.w say
	moveq #SR_TXEMT,%d0		| let it out, still servicing SWSR
	lea SRB(%a0),%a4
	btst #PORT_B,%d7
	bne.s 1f
	moveq #SCSR_TC,%d0
	lea SCSR+1(%a2),%a4
1:	bsr.w waitbit
	bra.s .				| no more SWSR: the watchdog resets
	.even
| the rings, in the dictionary space the definition allots after the code
buf:
