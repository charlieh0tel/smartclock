// A smoke test of the Z3815A's FPGA netlist, z3815a_fpga, which
// netlist.py writes from the decoded bitstream (docs/firmware/outputs.md,
// "Netlists"); docs/firmware/z3815a-trace.md reads what it prints:
//
//     make -f tools/fpga/Makefile target/fpga/z3815a_tb
//     vvp -n target/fpga/z3815a_tb +seconds=3
//
// It is z3805a_tb.v's for the Z3815A, which the firmware drives the same
// way (docs/firmware/counter.md): the same register port, eight
// registers here (counter_load_registers loops to 8), the count read as
// six nibbles at 0x12 to 0x17, the interpolator at 0x10/0x11, the preset
// as register 0 bit 0x40.  Two differences the netlist settles
// (docs/firmware/z3815a-trace.md, "Clocks" and "P38 and P40"):
// - The fabric clock GCLK is half the oscillator on P53: AB, a toggle on
//   the alternate clock buffer ACLK (= P53), feeds GCLK.  This testbench
//   drives P53 at 20 MHz (+osc=NS) so GCLK, and the count's ticks, run
//   at the firmware's 100 ns (1e-7, counter.md), the Z3805A's 10 MHz P7.
//   The divider runs to 9,830,400 (0x960000), so at a true 1 Hz GCLK is
//   9.8304 MHz and P53 19.6608 MHz, the CDMA clock; with GCLK at the
//   testbench's 10 MHz the internal 1 PPS is 0.98304 s and the count
//   drifts against the 1 s GPS (z3815a-trace.md, "The 1 PPS divider").
//   The logic -- the divide ratio, the tap, the gating -- is the same at
//   either clock.
// - A second clock input, P7, clocks the P7 divider, P7 / 6250, one side
//   of a phase-frequency detector out on P38 and P40, whose other side is
//   set on GCLK at GCLK / 6144: at 10 MHz on P7 and 19.6608 MHz on P53
//   both run at 1.6 kHz, a PLL's comparison (inferred).  By default P7 is
//   a testbench divide-by-two of P53, reset as AB is, so it is GCLK's
//   twin: a test choice, under which the detector's two sides cannot be
//   told apart; +p7=NS makes P7 a clock of its own.  P7's and P53's
//   board sources are not established.
//
// It clocks P53 at 20 MHz, pulses RESET, plays the CPU's register port as
// the firmware does, and gives a GPS 1 PPS on P82.  It prints the
// internal 1 PPS's edges and, each second, the arm, the ready bits, the
// count and the interpolator.  Options:
// - +seconds=N, the readings to take (3); 0 for the preset alone;
// - +gps=NS, +width=NS: the GPS 1 PPS's rise within each second
//   (0.250000037 s) and its width (100 us);
// - +sync=KIND: register 3's sync pair, 0 then KIND, on the first arm;
// - +registers: every address's read with the preset on;
// - +interpolator: interpolator_measure at both references;
// - +bus=NS: the bus cycle (1000);
// - +freerun=NS: no bus traffic, the preset never set: RESET, then NS of
//   free run;
// - +edges: print each P38 and P40 rise, in +freerun;
// - +osc=NS: the P53 (ACLK) period (50, i.e. 20 MHz);
// - +p7=NS: P7 a clock of its own, of period NS, not P53 halved;
// - +p35=NS, +p84=NS: P35, P84 a clock of period NS, not held Low; with
//   +p35 the P35 counter's probes and resets print, with +p84 each
//   +registers read the P84 rises before it;
// - +byte=HEX: the interpolator's byte on P3 to P11 (5a);
// - +probe: print probe.v's measures of the nets check-readings reads.
//
// The pins' roles are read from the netlist, not the board, so every one
// is an inference:
//
// | Pin | Role here | From |
// | --- | --------- | ---- |
// | P53 | the oscillator (20 MHz here; 19.6608 MHz implied) | the alternate clock buffer's input (ACLK); AB halves it into GCLK |
// | P7 | the P7 divider's clock (10 MHz implied) | clocks the P7 divider and LE, P38's side of the phase-frequency detector |
// | P61 | /CS, Low selects the port | enables the bus and its write strobe |
// | P75 | R/W, High reads | drives the bus when High and selected |
// | P69, P68, P66 | ADDR0, ADDR1, ADDR2 | the register write/read address, three bits for eight registers |
// | P71, P70, P67, P65, P62, P60, P59, P58 | DATA bits 0 to 7 | the register port's byte |
// | P3 to P6, P8 to P11 | the external interpolator's byte | read through the read multiplexer at 0x10/0x11 |
// | P82 | GPS 1 PPS, rising | through BG, clocks BH (the start's capture) |
// | P14, P28 | an active-Low pulse on alternate seconds | IB.Y, from FD's toggle and FC.Y |
// | P83 | the internal 1 PPS, registered on GCLK | FC.X |
// | P81 | the start's interval the interpolator measures | BH.X, inverted |
// | P35 | a clock of the P35 counter (+p35=NS; held Low without) | clocks LB, whose LB.Y clocks the counter |
// | P84 | a clock of the P84 counter (+p84=NS; held Low without) | clocks FH and FI, read in bus bits 4 to 7 at odd addresses |
// The other driven inputs (P23, P27, P50, P78) are held at a fixed level.
// P78 and P84 are start sources BG selects (register 2's bits 2 and 3);
// this testbench writes 0, P82 (z3815a-trace.md, "Clocks").

`timescale 1ns / 1ps

module z3815a_tb;
    // The oscillator on P53, 20 MHz here: GCLK and the count's ticks are
    // then 100 ns, the firmware's model (counter.md).  See the header for
    // the hardware's 9.8304 MHz.  +osc=NS, +p7=NS change them.
    real osc_period = 50.0;
    real bus_cycle = 1000.0;
    localparam real SECOND = 1.0e9;
    real gps_offset = 250.0e6 + 37.0;
    real gps_width = 100.0e3;
    // The external interpolator's byte (+byte=HEX to change it).
    reg [7:0] interpolator = 8'h5a;

    reg osc = 1'b0;
    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;
    // The data bus, High where neither side drives it, as z3805a_tb.v's.
    tri1 [7:0] data;
    // P7, the P7 divider's clock: by default p7_twin, a divide-by-two of P53
    // reset as AB is, so it matches GCLK (= AB.Y, the same divide-by-two on
    // ACLK); with +p7=NS, p7_own, a clock of its own.
    reg p7_twin = 1'b0;
    wire p7_own;
    plusarg_clock #(.PLUSARG("p7")) p7_clock (.out(p7_own));
    wire count_clock = p7_clock.period > 0.0 ? p7_own : p7_twin;
    // P35 and P84, held Low unless +p35=NS or +p84=NS clocks them.
    wire p35, p84;
    plusarg_clock #(.PLUSARG("p35")) p35_clock (.out(p35));
    plusarg_clock #(.PLUSARG("p84")) p84_clock (.out(p84));
    wire pps_p83, pps_p14, pps_p28;
    wire p2, p16, p19, p20, p21, p24, p25, p26, p29, p30, p34, p36, p38;
    wire p40, p47, p48, p51, p56, p57, p76, p77, p79, p81;

    assign data = data_drive ? data_out : 8'hzz;

    z3815a_fpga fpga (
        .P2(p2), .P3(interpolator[0]), .P4(interpolator[1]), .P5(interpolator[2]),
        .P6(interpolator[3]), .P7(count_clock), .P8(interpolator[4]), .P9(interpolator[5]),
        .P10(interpolator[6]), .P11(interpolator[7]),
        .P14(pps_p14), .P16(p16), .P19(p19), .P20(p20), .P21(p21),
        .P23(1'b0), .P24(p24), .P25(p25), .P26(p26), .P27(1'b0), .P28(pps_p28),
        .P29(p29), .P30(p30), .P34(p34), .P35(p35), .P36(p36), .P38(p38),
        .P40(p40), .P47(p47), .P48(p48), .P50(1'b0), .P51(p51), .P53(osc),
        .P56(p56), .P57(p57),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P75(read_write), .P76(p76), .P77(p77), .P78(1'b0), .P79(p79),
        .P81(p81), .P82(gps_pps), .P83(pps_p83), .P84(p84),
        .RESET(reset_n)
    );

    // The nets check-readings measures (probe.v; +probe prints them).
    reg probe_report = 1'b0;
    probe #(.NAME("AB.Y"), .EACH(0)) probe_ab_y (.signal(fpga.AB_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("BH.X"), .EACH(1)) probe_bh_x (.signal(fpga.BH_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("KE.Y"), .EACH(0)) probe_ke_y (.signal(fpga.KE_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("FE.Y"), .EACH(0)) probe_fe_y (.signal(fpga.FE_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("EE.Y"), .EACH(0)) probe_ee_y (.signal(fpga.EE_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("CC.Y"), .EACH(0)) probe_cc_y (.signal(fpga.CC_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("CA.Y"), .EACH(0)) probe_ca_y (.signal(fpga.CA_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("BA.Y"), .EACH(0)) probe_ba_y (.signal(fpga.BA_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("BB.Y"), .EACH(0)) probe_bb_y (.signal(fpga.BB_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("KI.X"), .EACH(0)) probe_ki_x (.signal(fpga.KI_X), .reference(pps_p83), .report(probe_report));
    probe #(.NAME("CI.X"), .EACH(0)) probe_ci_x (.signal(fpga.CI_X), .reference(pps_p83), .report(probe_report));
    probe #(.NAME("JI.Y"), .EACH(0)) probe_ji_y (.signal(fpga.JI_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("LE.X"), .EACH(0)) probe_le_x (.signal(fpga.LE_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("LD.Y"), .EACH(0)) probe_ld_y (.signal(fpga.LD_Y), .reference(1'b0), .report(probe_report));
    // The 1 PPS pins' pulses, each way, and P83 after P20.
    probe #(.NAME("P14"), .EACH(1)) probe_p14 (.signal(pps_p14), .reference(1'b0), .report(probe_report));
    probe #(.NAME("~P14"), .EACH(1)) probe_p14_low (.signal(~pps_p14), .reference(1'b0), .report(probe_report));
    probe #(.NAME("P28"), .EACH(1)) probe_p28 (.signal(pps_p28), .reference(1'b0), .report(probe_report));
    probe #(.NAME("~P28"), .EACH(1)) probe_p28_low (.signal(~pps_p28), .reference(1'b0), .report(probe_report));
    probe #(.NAME("P83"), .EACH(0)) probe_p83 (.signal(pps_p83), .reference(p20), .report(probe_report));
    // The sync's nets, printed only in a run with +sync=KIND
    // (z3815a-trace.md, "The sync field"): the pending sync IH.Y, IG.X
    // clearing it, the P7 prescaler's step lines JG.Y and JD.X, the
    // full-sync strobe JH.Y and GE.X, its capture on P7, and P83 after
    // the GPS 1 PPS.
    reg sync_report = 1'b0;
    probe #(.NAME("IH.Y"), .EACH(1)) probe_ih_y (.signal(fpga.IH_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("IG.X&IH.Y"), .EACH(1)) probe_ig_ih (.signal(fpga.IG_X & fpga.IH_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JG.Y"), .EACH(0)) probe_jg_y (.signal(fpga.JG_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JD.X"), .EACH(0)) probe_jd_x (.signal(fpga.JD_X), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JH.Y"), .EACH(1)) probe_jh_y (.signal(fpga.JH_Y), .reference(gps_pps), .report(sync_report));
    probe #(.NAME("GE.X"), .EACH(1)) probe_ge_x (.signal(fpga.GE_X), .reference(gps_pps), .report(sync_report));
    probe #(.NAME("P83/GPS"), .EACH(0)) probe_p83_gps (.signal(pps_p83), .reference(gps_pps), .report(sync_report));
    // The P35 counter's nets, printed only in a run with +p35=NS
    // (z3815a-trace.md, "The P35 counter"): LB.Y on P34, the /5's
    // terminal KA.Y, FC.X (the internal 1 PPS) and the reset JA.Y it
    // opens, the 512 carry's pulse on P26, the decade's top bit on P21,
    // GCLK / 12 (GC.X) and the XNOR on P29; and the pins that are
    // another's complement, which as one equal to it never rise.
    reg p35_report = 1'b0;
    probe #(.NAME("P34"), .EACH(0)) probe_p34 (.signal(p34), .reference(1'b0), .report(p35_report));
    probe #(.NAME("KA.Y"), .EACH(0)) probe_ka_y (.signal(fpga.KA_Y), .reference(1'b0), .report(p35_report));
    probe #(.NAME("FC.X"), .EACH(0)) probe_fc_x (.signal(fpga.FC_X), .reference(1'b0), .report(p35_report));
    probe #(.NAME("JA.Y"), .EACH(1)) probe_ja_y (.signal(fpga.JA_Y), .reference(fpga.FC_X), .report(p35_report));
    probe #(.NAME("P26"), .EACH(0)) probe_p26 (.signal(p26), .reference(1'b0), .report(p35_report));
    probe #(.NAME("P21"), .EACH(0)) probe_p21 (.signal(p21), .reference(fpga.FC_X), .report(p35_report));
    probe #(.NAME("GC.X"), .EACH(0)) probe_gc_x (.signal(fpga.GC_X), .reference(fpga.FC_X), .report(p35_report));
    probe #(.NAME("P29"), .EACH(0)) probe_p29 (.signal(p29), .reference(1'b0), .report(p35_report));
    probe #(.NAME("P19=P21"), .EACH(0)) probe_p19_p21 (.signal(p19 === p21), .reference(1'b0), .report(p35_report));
    probe #(.NAME("P30=P29"), .EACH(0)) probe_p30_p29 (.signal(p30 === p29), .reference(1'b0), .report(p35_report));
    probe #(.NAME("P36=P35"), .EACH(0)) probe_p36_p35 (.signal(p36 === p35), .reference(1'b0), .report(p35_report));
    // The P35 counter's state as JA.Y resets it, read from its flip-flops
    // (z3815a-trace.md, "The P35 counter"): thirteen binary bits, then a
    // decade; and LB.Y's rises since the last reset, counted here.
    wire [12:0] p35_binary = {fpga.KC.QX, fpga.KC.QY, fpga.LC.QY, fpga.LC.QX, fpga.HC.QY,
                              fpga.IC.QX, fpga.IC.QY, fpga.JC.QY, fpga.JC.QX, fpga.IA.QY,
                              fpga.IA.QX, fpga.HD.QY, fpga.KA.QX};
    wire [3:0] p35_decade = {fpga.FA.QX, fpga.FA.QY, fpga.GA.QY, fpga.GA.QX};
    integer lb_rises = 0;
    always @(posedge p34) lb_rises = lb_rises + 1;
    always @(posedge fpga.JA_Y)
        if (p35_clock.period > 0.0) begin
            $display("%0.9f s: JA.Y resets the P35 counter at decade %0d, binary %0d; LB.Y rose %0d times since the last",
                     $realtime / SECOND, p35_decade, p35_binary, lb_rises);
            lb_rises = 0;
        end
    // P84's rises, for the reads that return its counter (+p84=NS).
    integer p84_rises = 0, p84_read = 0;
    always @(posedge p84) p84_rises = p84_rises + 1;

    // Each clock's edges are at whole multiples of its half period, each
    // rounded to the 1 ps time precision on its own: a half period the
    // precision cannot hold (the pll run's P53) then puts each edge within
    // half a picosecond of its time, where a delay of the half period,
    // rounded once and repeated, would drift.
    initial begin : oscillator
        integer edges;
        edges = 0;
        if (!$value$plusargs("osc=%f", osc_period)) osc_period = 50.0;
        forever begin
            edges = edges + 1;
            #(edges * osc_period / 2 - $realtime) osc = ~osc;
        end
    end
    // AB resets its flip-flop while RESET is Low (GR High) and toggles it
    // on each rising P53 edge after; p7_twin does the same, so it is
    // GCLK's twin on P7.
    always @(posedge osc or negedge reset_n)
        if (p7_clock.period == 0.0) p7_twin <= reset_n ? ~p7_twin : 1'b0;

    reg [7:0] registers [0:7];

    // A bus cycle, as z3805a_tb.v's but with a three-bit address.
    task bus_write(input [2:0] register, input [7:0] value);
        begin
            address = register;
            read_write = 1'b0;
            data_out = value;
            data_drive = 1'b1;
            #(bus_cycle / 4) chip_select_n = 1'b0;
            #(bus_cycle) chip_select_n = 1'b1;
            #(bus_cycle / 4) data_drive = 1'b0;
            read_write = 1'b1;
            #(bus_cycle / 2);
        end
    endtask

    task fpga_write(input [2:0] register, input [7:0] value);
        begin
            registers[register] = value;
            bus_write(register, value);
        end
    endtask

    // fpga_read: the address into register 1's top five bits, a read of
    // the port, then register 1 back (counter.md, "The register port").
    task fpga_read(input [4:0] register, output [7:0] value);
        begin
            bus_write(3'd1, {register, registers[1][2:0]});
            address = 3'd0;
            read_write = 1'b1;
            #(bus_cycle / 4) chip_select_n = 1'b0;
            #(bus_cycle) value = data;
            p84_read = p84_rises;
            chip_select_n = 1'b1;
            #(bus_cycle / 2);
            bus_write(3'd1, registers[1]);
        end
    endtask

    // fpga_read_byte: reg + 1's low nibble high, then reg's.
    task fpga_read_byte(input [4:0] register, output [7:0] value);
        reg [7:0] high, low;
        begin
            fpga_read(register + 5'd1, high);
            fpga_read(register, low);
            value = {high[3:0], low[3:0]};
        end
    endtask

    task read_count(output [23:0] count);
        reg [7:0] high, middle, low;
        begin
            fpga_read_byte(5'h16, high);
            fpga_read_byte(5'h14, middle);
            fpga_read_byte(5'h12, low);
            count = {high, middle, low};
        end
    endtask

    task set_field(input [2:0] register, input [7:0] mask, input [7:0] value);
        fpga_write(register, (registers[register] & ~mask) | (value & mask));
    endtask

    // Each output's rising edges, and the period between them.  Indices
    // 0-2 are the 1 PPS pins; 3-4 are P38/P40, printed only with +edges.
    real last_rise [0:4];
    integer rises [0:4];
    task note_rise(input integer which, input [8*3:1] name);
        begin
            if (rises[which] > 0)
                $display("%0.9f s: %0s rises, %0.1f ns after the last", $realtime / SECOND, name,
                         $realtime - last_rise[which]);
            else
                $display("%0.9f s: %0s rises", $realtime / SECOND, name);
            last_rise[which] = $realtime;
            rises[which] = rises[which] + 1;
        end
    endtask
    always @(posedge pps_p83) note_rise(0, "P83");
    always @(posedge pps_p14) note_rise(1, "P14");
    always @(posedge pps_p28) note_rise(2, "P28");
    always @(posedge p38)
        if ($test$plusargs("edges")) note_rise(3, "P38");
        else rises[3] = rises[3] + 1;
    always @(posedge p40)
        if ($test$plusargs("edges")) note_rise(4, "P40");
        else rises[4] = rises[4] + 1;

    // The GPS 1 PPS, gps_offset into every second.
    initial begin
        if (!$value$plusargs("gps=%f", gps_offset)) gps_offset = 250.0e6 + 37.0;
        if (!$value$plusargs("width=%f", gps_width)) gps_width = 100.0e3;
        #(gps_offset);
        forever begin
            gps_pps = 1'b1;
            #(gps_width) gps_pps = 1'b0;
            #(SECOND - gps_width);
        end
    end

    integer seconds, second, polls, address_field, sync_kind, i;
    real freerun_ns;
    reg [7:0] status, interpolated;
    reg [23:0] count;
    initial begin
        if (!$value$plusargs("seconds=%d", seconds)) seconds = 3;
        if (!$value$plusargs("byte=%h", interpolator)) interpolator = 8'h5a;
        if (!$value$plusargs("bus=%f", bus_cycle)) bus_cycle = 1000.0;
        if (!$value$plusargs("sync=%d", sync_kind)) sync_kind = 0;
        rises[0] = 0; rises[1] = 0; rises[2] = 0; rises[3] = 0; rises[4] = 0;
        // +freerun=NS: reset, run NS with no bus traffic: the P38/P40
        // comparison and the 1 PPS pins, register 2 at its reset 0
        // (z3815a-trace.md, "P38 and P40" and "The 1 PPS outputs").
        if ($value$plusargs("freerun=%f", freerun_ns)) begin
            #(10 * osc_period) reset_n = 1'b1;
            #(freerun_ns);
            $display("freerun %0.0f ns: P38 %0d rises, P40 %0d rises", freerun_ns, rises[3], rises[4]);
            $display("rises: P83 %0d, P14 %0d, P28 %0d", rises[0], rises[1], rises[2]);
            probe_report = 1'b1;
            p35_report = p35_clock.period > 0.0;
            #1 $finish;
        end
        #(10 * osc_period) reset_n = 1'b1;
        #(10 * osc_period);

        // counter_configuration_init's eight registers (fpga_fields_to_registers):
        // preset on, reference high, the rest 0.
        fpga_write(0, 8'h40);
        fpga_write(1, 8'h02);
        for (i = 2; i < 8; i = i + 1) fpga_write(i[2:0], 8'h00);
        // counter_selftest: the preset set, the count read.
        read_count(count);
        $display("%0.6f s: with the preset the count reads %06x", $realtime / SECOND, count);
        fpga_read(5'h00, status);
        $display("%0.6f s: status %02x", $realtime / SECOND, status);
        if ($test$plusargs("registers"))
            for (address_field = 0; address_field < 32; address_field = address_field + 1) begin
                fpga_read(address_field[4:0], status);
                if (p84_clock.period > 0.0)
                    $display("register %02x reads %02x; P84 rose %0d times, %0d mod 16", address_field,
                             status, p84_read, p84_read % 16);
                else
                    $display("register %02x reads %02x", address_field, status);
            end

        if ($test$plusargs("interpolator")) begin
            set_field(0, 8'h40, 8'h40);
            set_field(0, 8'h03, 8'h03);
            set_field(1, 8'h01, 8'h00);
            set_field(1, 8'hf8, 8'h00);
            for (address_field = 0; address_field < 2; address_field = address_field + 1) begin
                set_field(1, 8'h02, address_field ? 8'h02 : 8'h00);
                set_field(0, 8'h40, 8'h40);
                set_field(1, 8'h01, 8'h01);
                set_field(0, 8'h40, 8'h00);
                set_field(1, 8'h01, 8'h00);
                fpga_read(5'h00, status);
                $display("%0.6f s: reference %0d measured: status %02x", $realtime / SECOND,
                         address_field, status);
            end
        end

        for (second = 0; second < seconds; second = second + 1) begin
            // counter_arm: preset on, mode 0, start clear, address 0,
            // reference high, a sync's pair (+sync=KIND, on the first arm),
            // preset off.
            set_field(0, 8'h40, 8'h40);
            set_field(0, 8'h03, 8'h00);
            set_field(1, 8'h01, 8'h00);
            set_field(1, 8'hf8, 8'h00);
            set_field(1, 8'h02, 8'h02);
            if (second == 0 && sync_kind != 0) begin
                set_field(3, 8'h03, 8'h00);
                set_field(3, 8'h03, sync_kind[7:0]);
                $display("%0.6f s: sync kind %0d", $realtime / SECOND, sync_kind);
            end
            set_field(0, 8'h40, 8'h00);
            $display("%0.6f s: armed", $realtime / SECOND);
            // counter_wait_ready: status bit 1, then bit 0, polled every 9.8 ms.
            status = 8'h00;
            for (polls = 0; polls < 512 && !status[1]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[1]) #(9.8e6);
            end
            $display("%0.6f s: status %02x after %0d polls (bit 1 counted)", $realtime / SECOND, status, polls);
            for (polls = 0; polls < 512 && !status[0]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[0]) #(9.8e6);
            end
            $display("%0.6f s: status %02x, seconds %0d", $realtime / SECOND, status, status[7:4]);
            read_count(count);
            fpga_read_byte(5'h10, interpolated);
            $display("%0.6f s: count %06x, less the preset %0d ticks; interpolator %02x (complemented %02x)",
                     $realtime / SECOND, count, $signed({1'b0, count}) - 24'h555555, interpolated, ~interpolated);
        end
        $display("rises: P83 %0d, P14 %0d, P28 %0d", rises[0], rises[1], rises[2]);
        probe_report = 1'b1;
        sync_report = sync_kind != 0;
        p35_report = p35_clock.period > 0.0;
        #1 $finish;
    end
    // Optional VCD for waveform figures (tools/fpga/vcd_wave.py): +vcd=FILE.
    initial begin : vcd_dump
        reg [8*256:1] vcd_path;
        if ($value$plusargs("vcd=%s", vcd_path)) begin
            $dumpfile(vcd_path);
            $dumpvars(0, z3815a_tb);
        end
    end

endmodule

// A clock on an input pin, of the period +PLUSARG=NS gives, its edges at
// whole multiples of its half period as the testbench's oscillator's are;
// held Low without the plusarg.  `period` is 0 while it is held.
module plusarg_clock #(
    parameter PLUSARG = "clock"
) (
    output reg out
);
    real period = 0.0;

    initial begin : run
        integer edges;
        out = 1'b0;
        edges = 0;
        if (!$value$plusargs({PLUSARG, "=%f"}, period)) period = 0.0;
        if (period > 0.0)
            forever begin
                edges = edges + 1;
                #(edges * period / 2 - $realtime) out = ~out;
            end
    end
endmodule
