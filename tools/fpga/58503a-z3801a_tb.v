// A smoke test of the 58503A/Z3801A's FPGA netlist, fpga_58503a_z3801a,
// which netlist.py writes from the decoded bitstream
// (docs/firmware/outputs.md, "Netlists"); docs/firmware/58503a-z3801a-trace.md
// reads what it prints.  It is z3805a_tb.v with two pins changed: P45 is
// unused here (one fewer I/O block, so no port), and P14 carries P17's
// input straight through (unregistered), not the divider's 1 PPS.  The
// internal 1 PPS is on P83 and P78 alone, both EI.Y registered on the
// global clock.
//
//     make -f tools/fpga/Makefile target/fpga/58503a-z3801a_tb
//     vvp -n target/fpga/58503a-z3801a_tb +seconds=3
//
// It clocks P7 at 10 MHz, pulses RESET, plays the CPU's register port as
// docs/firmware/counter.md reads the firmware driving it, and gives a
// GPS 1 PPS on P82.  It prints the internal 1 PPS's edges and, each
// second, the arm, the ready bits, the count and the interpolator.  Options:
// - +seconds=N, the readings to take (3); 0 for the preset alone;
// - +gps=NS, +width=NS: the GPS 1 PPS's rise within each second
//   (0.250000037 s) and its width (100 us);
// - +sync=KIND: register 3's sync pair, 0 then KIND, on the first arm;
// - +registers: every address's read with the preset on;
// - +interpolator: interpolator_measure at both references;
// - +bus=NS: the bus cycle (1000);
// - +byte=HEX: the interpolator's byte on P3 to P11 (5a);
// - +probe: print probe.v's measures of the nets check-readings reads.
//
// The pins' roles are read from the netlist, not the board, so every
// one is an inference:
//
// | Pin | Role here | From |
// | --- | --------- | ---- |
// | P7 | 10 MHz | the global clock buffer's input |
// | P61 | /CS, Low selects the port | enables the bus and its write strobe (HL) |
// | P75 | R/W, High reads | drives the bus when High and selected (HL) |
// | P69, P68, P66 | ADDR0, ADDR1, ADDR2 | register 0, 1 and 3's write decode (EL, DL), ADDR2 Low |
// | P71, P70, P67, P65, P62, P60, P59, P58 | DATA bits 0 to 7 | register 0's mode (bits 0, 1) and preset (bit 6), register 1's fields |
// | P3 to P6, P8 to P11 | held at the interpolator's byte, bits 0 to 7 (+byte=HEX) | read only through the read multiplexer (AB to AE) |
// | P82 | GPS 1 PPS, rising | clocks AH, the count's start |
// | P17 | held High | passed to P84 inverted, and to P14 straight through |
// | P83, P78 | the internal 1 PPS | EI.Y, registered on the 10 MHz clock |
// | P14 | P17 passed through, unregistered | not a 1 PPS here (unlike the Z3805A) |

`timescale 1ns / 1ps

module fpga_58503a_z3801a_tb;
    // 10 MHz: the count's ticks are 100 ns (counter.md, "The reading").
    localparam real CLOCK_PERIOD = 100.0;
    // A CS5 cycle with 12 wait states at the MC68331's 16.78 MHz is about
    // 15 clocks, 0.9 us; one microsecond (+bus=NS to change it) leaves the
    // FPGA's write strobe, one clock long and a clock after the select is
    // first sampled, room.
    real bus_cycle = 1000.0;
    localparam real SECOND = 1.0e9;
    // Where in each second the GPS 1 PPS rises, and for how long, in ns
    // (+gps=NS, +width=NS to change them).
    real gps_offset = 250.0e6 + 37.0;
    real gps_width = 100.0e3;
    // The external interpolator's byte (+byte=HEX to change it).
    reg [7:0] interpolator = 8'h5a;

    reg clock = 1'b0;
    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;
    // The data bus, High where neither side drives it.  The bus pads have
    // no pull-ups (outputs.md, "Netlists"), and an undriven input is
    // Verilog's x, which the models' tables pass on whatever they read;
    // the results are the same with the bus held Low.  What the board's
    // bus does between cycles is not established.
    tri1 [7:0] data;
    wire pps_p83, pps_p78;
    wire p84, p16, p14, p2, p72, p76, p77, p79, p81;

    assign data = data_drive ? data_out : 8'hzz;

    // P45 is unused in this design (no port); P14 carries P17 straight
    // through, so it is wired to a plain net, not a 1 PPS.
    fpga_58503a_z3801a fpga (
        .P2(p2), .P3(interpolator[0]), .P4(interpolator[1]), .P5(interpolator[2]),
        .P6(interpolator[3]), .P7(clock), .P8(interpolator[4]), .P9(interpolator[5]),
        .P10(interpolator[6]), .P11(interpolator[7]),
        .P14(p14), .P16(p16), .P17(1'b1),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P76(p76), .P77(p77), .P78(pps_p78),
        .P79(p79), .P81(p81), .P82(gps_pps), .P83(pps_p83), .P84(p84),
        .RESET(reset_n)
    );

    // The nets check-readings measures (probe.v; +probe prints them).
    reg probe_report = 1'b0;
    probe #(.NAME("AH.Y"), .EACH(1)) probe_ah_y (.signal(fpga.AH_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("HG.Y"), .EACH(0)) probe_hg_y (.signal(fpga.HG_Y), .reference(pps_p83), .report(probe_report));
    probe #(.NAME("CI.X"), .EACH(0)) probe_ci_x (.signal(fpga.CI_X), .reference(pps_p83), .report(probe_report));
    probe #(.NAME("HH.X"), .EACH(0)) probe_hh_x (.signal(fpga.HH_X), .reference(1'b0), .report(probe_report));
    // The sync's nets, printed only in a run with +sync=KIND
    // (58503a-z3801a-trace.md, "The sync field"): the pending sync EK.X,
    // the step lines GJ.X and GJ.Y, the restart EJ.X, and P83 after the
    // GPS 1 PPS.
    reg sync_report = 1'b0;
    probe #(.NAME("EK.X"), .EACH(1)) probe_ek_x (.signal(fpga.EK_X), .reference(1'b0), .report(sync_report));
    probe #(.NAME("GJ.X"), .EACH(1)) probe_gj_x (.signal(fpga.GJ_X), .reference(1'b0), .report(sync_report));
    probe #(.NAME("GJ.Y"), .EACH(1)) probe_gj_y (.signal(fpga.GJ_Y), .reference(1'b0), .report(sync_report));
    probe #(.NAME("EJ.X"), .EACH(1)) probe_ej_x (.signal(fpga.EJ_X), .reference(gps_pps), .report(sync_report));
    probe #(.NAME("P83/GPS"), .EACH(0)) probe_p83_gps (.signal(pps_p83), .reference(gps_pps), .report(sync_report));

    always #(CLOCK_PERIOD / 2) clock = ~clock;

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
            #(bus_cycle) value = data;
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

    // Each output's rising edges, and the period between them.  Only P83
    // and P78 carry the internal 1 PPS here.
    real last_rise [0:1];
    integer rises [0:1];
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
    always @(posedge pps_p78) note_rise(1, "P78");
    always @(negedge pps_p83)
        if (rises[0] > 0) $display("%0.9f s: P83 falls, %0.1f ns high", $realtime / SECOND, $realtime - last_rise[0]);

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
    reg [7:0] status, interpolated;
    reg [23:0] count;
    initial begin
        if (!$value$plusargs("seconds=%d", seconds)) seconds = 3;
        if (!$value$plusargs("byte=%h", interpolator)) interpolator = 8'h5a;
        if (!$value$plusargs("bus=%f", bus_cycle)) bus_cycle = 1000.0;
        if (!$value$plusargs("sync=%d", sync_kind)) sync_kind = 0;
        rises[0] = 0; rises[1] = 0;
        #(10 * CLOCK_PERIOD) reset_n = 1'b1;
        #(10 * CLOCK_PERIOD);

        // counter_configuration_init's registers: preset, reference high.
        fpga_write(0, 8'h40);
        fpga_write(1, 8'h02);
        fpga_write(2, 8'h00);
        fpga_write(3, 8'h00);
        // counter_selftest: the preset set, the count read.
        read_count(count);
        $display("%0.6f s: with the preset the count reads %06x", $realtime / SECOND, count);
        fpga_read(5'h00, status);
        $display("%0.6f s: status %02x", $realtime / SECOND, status);
        // +registers: every address's read, with the preset on.
        if ($test$plusargs("registers"))
            for (address_field = 0; address_field < 32; address_field = address_field + 1) begin
                fpga_read(address_field[4:0], status);
                $display("register %02x reads %02x", address_field, status);
            end

        // +interpolator: interpolator_prepare, then interpolator_measure
        // at each reference, polling status bit 0 as it does.
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
            // counter_wait_ready: status bit 1, then bit 0, polled every 9.8 ms.
            status = 8'h00;
            for (polls = 0; polls < 512 && !status[1]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[1]) #(9.8e6);
            end
            $display("%0.6f s: status %02x after %0d polls (bit 1 counted)", $realtime / SECOND, status, polls);
            // A failed wait for bit 1 clears `armed`, and the wait for bit 0
            // then returns at once (counter_wait_ready).
            for (polls = 0; polls < 512 && status[1] && !status[0]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[0]) #(9.8e6);
            end
            $display("%0.6f s: status %02x, seconds %0d", $realtime / SECOND, status, status[7:4]);
            read_count(count);
            fpga_read_byte(5'h10, interpolated);
            $display("%0.6f s: count %06x, less the preset %0d ticks; interpolator %02x (complemented %02x)",
                     $realtime / SECOND, count, $signed({1'b0, count}) - 24'h555555, interpolated, ~interpolated);
        end
        $display("rises: P83 %0d, P78 %0d", rises[0], rises[1]);
        probe_report = 1'b1;
        sync_report = sync_kind != 0;
        #1 $finish;
    end
    // Optional VCD for waveform figures (tools/fpga/vcd_wave.py): +vcd=FILE.
    initial begin : vcd_dump
        reg [8*256:1] vcd_path;
        if ($value$plusargs("vcd=%s", vcd_path)) begin
            $dumpfile(vcd_path);
            $dumpvars(0, fpga_58503a_z3801a_tb);
        end
    end

endmodule
