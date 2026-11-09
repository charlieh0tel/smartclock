// A smoke test of the Z3816A's FPGA netlist, z3816a_fpga, which
// netlist.py writes from the decoded bitstream (docs/firmware/outputs.md,
// "Netlists"); docs/firmware/z3816a-trace.md reads what it prints.
// Modeled on tools/fpga/z3805a_tb.v.
//
//     make -f tools/fpga/Makefile target/fpga/z3816a_tb
//     vvp -n target/fpga/z3816a_tb +seconds=2
//
// The Z3816A's FPGA differs from the Z3805A's: it has two clock inputs.
// P53 drives the alternate clock buffer ACLK, and the logic block AB
// toggles on ACLK to make the global clock GCLK = ACLK / 2; the interval
// counter, the 1 PPS divider and the read port are all on GCLK, so the
// count's tick is GCLK's period.  For the count to be of 100 ns ticks
// (counter.md, a 10 MHz tick), GCLK must be 10 MHz and so P53 (ACLK)
// 20 MHz.  P7 drives a separate clock net through general routing (not
// the GCLK buffer, as in the Z3805A) that clocks the P7 divider, P7 /
// 6250, one side of a phase-frequency detector out on P38 and P40 whose
// other side is set on GCLK at GCLK / 6144: at 10 MHz on P7 and
// 19.6608 MHz on P53 both run at 1.6 kHz, a PLL's comparison (inferred).
// P7 is driven at 10 MHz here, as its own clock; +p7=NS and +osc=NS make
// the two sides unequal.  Every pin role is read from the netlist, not a
// board.
//
// The register port is pin-for-pin the Z3805A's: P61 /CS, P75 R/W,
// P69/P68/P66 ADDR0/1/2, the eight data bits on P71 P70 P67 P65 P62 P60
// P59 P58 (bit 0 to bit 7), P67 (bit 2) an FPGA output never read as a
// write input, P3 to P6 and P8 to P11 the interpolator's byte, P82 the
// GPS 1 PPS.
//
// Options (as z3805a_tb.v):
// - +seconds=N, the readings to take (2); 0 for the preset alone;
// - +gps=NS, +width=NS: the GPS 1 PPS's rise within each second and width;
// - +sync=KIND: register 3's sync pair, 0 then KIND, on the first arm;
// - +registers: every address's read with the preset on;
// - +bus=NS: the bus cycle (1000);
// - +freerun=NS: no bus traffic, the preset never set: RESET, then NS of
//   free run;
// - +edges: print each P38 and P40 rise, in +freerun;
// - +osc=NS: the P53 (ACLK) period (50, i.e. 20 MHz);
// - +p7=NS: the P7 period (100, i.e. 10 MHz);
// - +byte=HEX: the interpolator's byte on P3 to P11 (5a);
// - +probe: print probe.v's measures of the nets check-readings reads.
//
// It has no +interpolator: the Makefile's default flags pass it, and it
// ignores it.

`timescale 1ns / 1ps

module z3816a_tb;
    // GCLK (= ACLK / 2) is the count's clock; its tick is 100 ns.
    localparam real CLOCK_PERIOD = 100.0;
    real osc_period = 50.0;   // P53 / ACLK, 20 MHz, halved by AB to GCLK.
    real p7_period = 100.0;   // P7, 10 MHz (assumed).
    real bus_cycle = 1000.0;
    localparam real SECOND = 1.0e9;
    real gps_offset = 250.0e6 + 37.0;
    real gps_width = 100.0e3;
    // The external interpolator's byte (+byte=HEX to change it).
    reg [7:0] interpolator = 8'h5a;

    reg clock53 = 1'b0;    // P53 / ACLK oscillator, 20 MHz.
    reg clock7 = 1'b0;     // P7, 10 MHz.
    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;

    // The data bus, High where neither side drives it.  Bits 0, 1, 3, 4,
    // 5, 6, 7 reach the FPGA's inout pins; bit 2 (P67) is an FPGA output
    // only, so it is a separate wire read back into the byte.
    tri1 [7:0] data;
    wire p67;
    // The 1 PPS candidates and the other outputs.
    wire pps_p83, pps_p14, pps_p28, pps_p20;
    wire p2, p16, p38, p40, p47, p48, p51, p52, p57, p76, p77, p79, p81;

    assign data = data_drive ? data_out : 8'hzz;

    z3816a_fpga fpga (
        .P2(p2),
        .P3(interpolator[0]), .P4(interpolator[1]), .P5(interpolator[2]),
        .P6(interpolator[3]), .P7(clock7), .P8(interpolator[4]),
        .P9(interpolator[5]), .P10(interpolator[6]), .P11(interpolator[7]),
        .P14(pps_p14), .P16(p16), .P20(pps_p20), .P28(pps_p28),
        .P38(p38), .P40(p40), .P47(p47), .P48(p48), .P51(p51), .P52(p52),
        .P53(clock53), .P57(p57),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(p67),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P75(read_write), .P76(p76), .P77(p77), .P79(p79), .P81(p81),
        .P82(gps_pps), .P83(pps_p83),
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
    probe #(.NAME("P83"), .EACH(0)) probe_p83 (.signal(pps_p83), .reference(pps_p20), .report(probe_report));
    // The sync's nets, printed only in a run with +sync=KIND
    // (z3816a-trace.md, "The sync field"): the pending sync IH.Y, IG.X
    // clearing it, the P7 prescaler's step lines JG.Y and JD.X, the
    // full-sync strobe JH.Y and KD.X, its capture on P7, and P83 after
    // the GPS 1 PPS.
    reg sync_report = 1'b0;
    probe #(.NAME("IH.Y"), .EACH(1)) probe_ih_y (.signal(fpga.IH_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("IG.X&IH.Y"), .EACH(1)) probe_ig_ih (.signal(fpga.IG_X & fpga.IH_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JG.Y"), .EACH(0)) probe_jg_y (.signal(fpga.JG_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JD.X"), .EACH(0)) probe_jd_x (.signal(fpga.JD_X), .reference(1'b0), .report(sync_report));
    probe #(.NAME("JH.Y"), .EACH(1)) probe_jh_y (.signal(fpga.JH_Y), .reference(gps_pps), .report(sync_report));
    probe #(.NAME("KD.X"), .EACH(1)) probe_kd_x (.signal(fpga.KD_X), .reference(gps_pps), .report(sync_report));
    probe #(.NAME("P83/GPS"), .EACH(0)) probe_p83_gps (.signal(pps_p83), .reference(gps_pps), .report(sync_report));

    // The bus byte, with P67 (bit 2) folded in on reads.
    wire [7:0] bus_byte = {data[7:3], p67, data[1:0]};

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
            #(edges * osc_period / 2 - $realtime) clock53 = ~clock53;
        end
    end
    initial begin : p7_clock
        integer edges;
        edges = 0;
        if (!$value$plusargs("p7=%f", p7_period)) p7_period = 100.0;
        forever begin
            edges = edges + 1;
            #(edges * p7_period / 2 - $realtime) clock7 = ~clock7;
        end
    end

    // The firmware's register images (counter.md, "The register port").
    reg [7:0] registers [0:3];

    task bus_write(input [1:0] register, input [7:0] value);
        begin
            address = {1'b0, register};
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

    task fpga_write(input [1:0] register, input [7:0] value);
        begin
            registers[register] = value;
            bus_write(register, value);
        end
    endtask

    // fpga_read: the address into register 1's top five bits, a read of
    // the port, then register 1 back.
    task fpga_read(input [4:0] register, output [7:0] value);
        begin
            bus_write(2'd1, {register, registers[1][2:0]});
            address = 3'd0;
            read_write = 1'b1;
            #(bus_cycle / 4) chip_select_n = 1'b0;
            #(bus_cycle) value = bus_byte;
            chip_select_n = 1'b1;
            #(bus_cycle / 2);
            bus_write(2'd1, registers[1]);
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

    task set_field(input [1:0] register, input [7:0] mask, input [7:0] value);
        fpga_write(register, (registers[register] & ~mask) | (value & mask));
    endtask

    // Each 1 PPS candidate's rising edges and the period between them.
    // Indices 0-3 are the 1 PPS pins; 4-5 are P38/P40, printed only with
    // +edges.
    real last_rise [0:5];
    integer rises [0:5];
    task note_rise(input integer which, input [8*4:1] name);
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
    always @(posedge pps_p20) note_rise(3, "P20");
    always @(posedge p38)
        if ($test$plusargs("edges")) note_rise(4, "P38");
        else rises[4] = rises[4] + 1;
    always @(posedge p40)
        if ($test$plusargs("edges")) note_rise(5, "P40");
        else rises[5] = rises[5] + 1;

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

    integer seconds, second, polls, address_field, sync_kind;
    real freerun_ns;
    reg [7:0] status, interpolated;
    reg [23:0] count;
    initial begin
        if (!$value$plusargs("seconds=%d", seconds)) seconds = 2;
        if (!$value$plusargs("byte=%h", interpolator)) interpolator = 8'h5a;
        if (!$value$plusargs("bus=%f", bus_cycle)) bus_cycle = 1000.0;
        if (!$value$plusargs("sync=%d", sync_kind)) sync_kind = 0;
        rises[0] = 0; rises[1] = 0; rises[2] = 0; rises[3] = 0; rises[4] = 0; rises[5] = 0;
        // +freerun=NS: reset, run NS with no bus traffic: the P38/P40
        // comparison and the 1 PPS pins, register 2 at its reset 0
        // (z3816a-trace.md, "P38 and P40" and "The 1 PPS outputs").
        if ($value$plusargs("freerun=%f", freerun_ns)) begin
            #(10 * CLOCK_PERIOD) reset_n = 1'b1;
            #(freerun_ns);
            $display("freerun %0.0f ns: P38 %0d rises, P40 %0d rises", freerun_ns, rises[4], rises[5]);
            $display("rises: P83 %0d, P14 %0d, P28 %0d, P20 %0d", rises[0], rises[1], rises[2], rises[3]);
            probe_report = 1'b1;
            #1 $finish;
        end
        #(10 * CLOCK_PERIOD) reset_n = 1'b1;
        #(10 * CLOCK_PERIOD);

        // counter_configuration_init's registers (as the Z3805A; the
        // Z3816A's image was not read at this grain -- interval.md).
        fpga_write(0, 8'h40);
        fpga_write(1, 8'h02);
        fpga_write(2, 8'h00);
        fpga_write(3, 8'h00);
        // counter_selftest: the preset set, the count read.
        read_count(count);
        $display("%0.6f s: with the preset the count reads %06x", $realtime / SECOND, count);
        fpga_read(5'h00, status);
        $display("%0.6f s: status %02x", $realtime / SECOND, status);
        if ($test$plusargs("registers"))
            for (address_field = 0; address_field < 32; address_field = address_field + 1) begin
                fpga_read(address_field[4:0], status);
                $display("register %02x reads %02x", address_field, status);
            end

        for (second = 0; second < seconds; second = second + 1) begin
            // counter_arm: preset on, mode 0, start clear, address 0, a
            // sync's pair (+sync=KIND, on the first arm), preset off.
            set_field(0, 8'h40, 8'h40);
            set_field(0, 8'h03, 8'h00);
            set_field(1, 8'h01, 8'h00);
            set_field(1, 8'hf8, 8'h00);
            if (second == 0 && sync_kind != 0) begin
                set_field(3, 8'h03, 8'h00);
                set_field(3, 8'h03, sync_kind[7:0]);
                $display("%0.6f s: sync kind %0d", $realtime / SECOND, sync_kind);
            end
            set_field(0, 8'h40, 8'h00);
            $display("%0.6f s: armed", $realtime / SECOND);
            // counter_wait_ready: status bit 1, then bit 0, polled 9.8 ms.
            status = 8'h00;
            for (polls = 0; polls < 512 && !status[1]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[1]) #(9.8e6);
            end
            $display("%0.6f s: status %02x after %0d polls (bit 1)", $realtime / SECOND, status, polls);
            for (polls = 0; polls < 512 && !status[0]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[0]) #(9.8e6);
            end
            $display("%0.6f s: status %02x, seconds %0d", $realtime / SECOND, status, status[7:4]);
            read_count(count);
            fpga_read_byte(5'h10, interpolated);
            $display("%0.6f s: count %06x, less 0x3c0000 %0d ticks; interpolator %02x (compl %02x)",
                     $realtime / SECOND, count, $signed({1'b0, count}) - 24'h3c0000, interpolated, ~interpolated);
        end
        $display("rises: P83 %0d, P14 %0d, P28 %0d, P20 %0d", rises[0], rises[1], rises[2], rises[3]);
        probe_report = 1'b1;
        sync_report = sync_kind != 0;
        #1 $finish;
    end
    // Optional VCD for waveform figures (tools/fpga/vcd_wave.py): +vcd=FILE.
    initial begin : vcd_dump
        reg [8*256:1] vcd_path;
        if ($value$plusargs("vcd=%s", vcd_path)) begin
            $dumpfile(vcd_path);
            $dumpvars(0, z3816a_tb);
        end
    end

endmodule
