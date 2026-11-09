// The Z3805A's FPGA netlist, z3805a_fpga, driven from a stimulus file,
// such as tools/fpga/z3805a.sync_3_late.stim:
//
//     iverilog -g2005 -o target/fpga/z3805a_stimulus tools/fpga/xc3000.v \
//         target/fpga/z3805a.v tools/fpga/z3805a_stimulus.v
//     vvp -n target/fpga/z3805a_stimulus +stimulus=FILE
//
// Each line of FILE is a time in picoseconds, the lines in order, and an
// operation:
//
// | Line | Does |
// | ---- | ---- |
// | `T reset` | RESET, Low from the start, goes High |
// | `T write R V` | a bus write of V, hex, to register R |
// | `T read` | a bus read of the port; prints `read T V` |
// | `T gps W` | the GPS 1 PPS on P82 rises, and falls W ps later |
// | `T pins V` | the interpolator's byte on P3 to P11, hex |
// | `T end` | the simulation ends |
//
// A bus cycle is `z3805a_tb.v`'s: the select Low for BUS_CYCLE, a write's
// data held a quarter of one after; a read is sampled as the select
// rises.  Each rise of P83, the internal 1 PPS, prints `pps T`, and each
// pulse of AH.Y, the start's interval the interpolator measures,
// `interval T W`: its rise and its width, picoseconds.  The
// pins' roles are those of `z3805a_tb.v`, read from the netlist.  With
// +probe it prints probe.v's measures of the divider's and the sync's
// nets at the end.

`timescale 1ns / 1ps

module z3805a_stimulus;
    // 10 MHz, its rising edges at 50 ns and every 100 ns on.
    localparam real CLOCK_PERIOD = 100.0;
    localparam real BUS_CYCLE = 1000.0;
    // Picoseconds a nanosecond, the stimulus's unit.
    localparam real PS = 1000.0;

    reg clock = 1'b0;
    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;
    reg [7:0] pins = 8'h00;
    // The data bus, High where neither side drives it.  The design's pads
    // have no pull-ups (outputs.md, "Netlists"), and an undriven input is
    // Verilog's x, which the models' tables pass on whatever they read;
    // the results are the same with the bus held Low.  What the board's
    // bus does between cycles is not established.
    tri1 [7:0] data;
    wire pps_p83, pps_p78, pps_p45, pps_p14;
    wire p84, p16, p2, p72, p76, p77, p79, p81;

    assign data = data_drive ? data_out : 8'hzz;

    z3805a_fpga fpga (
        .P2(p2), .P3(pins[0]), .P4(pins[1]), .P5(pins[2]), .P6(pins[3]), .P7(clock),
        .P8(pins[4]), .P9(pins[5]), .P10(pins[6]), .P11(pins[7]),
        .P14(pps_p14), .P16(p16), .P17(1'b1), .P45(pps_p45),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P76(p76), .P77(p77), .P78(pps_p78),
        .P79(p79), .P81(p81), .P82(gps_pps), .P83(pps_p83), .P84(p84),
        .RESET(reset_n)
    );

    // The nets check-readings measures (probe.v; +probe prints them).
    reg probe_report = 1'b0;
    probe #(.NAME("GI.X"), .EACH(0)) probe_gi_x (.signal(fpga.GI_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("EJ.X"), .EACH(0)) probe_ej_x (.signal(fpga.EJ_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("GJ.X"), .EACH(0)) probe_gj_x (.signal(fpga.GJ_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("GJ.Y"), .EACH(0)) probe_gj_y (.signal(fpga.GJ_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("EK.X"), .EACH(0)) probe_ek_x (.signal(fpga.EK_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("HG.Y"), .EACH(0)) probe_hg_y (.signal(fpga.HG_Y), .reference(pps_p83), .report(probe_report));

    always #(CLOCK_PERIOD / 2) clock = ~clock;

    always @(posedge pps_p83) $display("pps %0.0f", $realtime * PS);

    // AH.Y, the start's interval the interpolator measures (P81, inverted):
    // its rise and its width.
    real interval_rise = 0.0;
    always @(posedge fpga.AH_Y) interval_rise = $realtime;
    always @(negedge fpga.AH_Y)
        $display("interval %0.0f %0.0f", interval_rise * PS, ($realtime - interval_rise) * PS);

    task bus_write(input [1:0] register, input [7:0] value);
        begin
            address = {1'b0, register};
            read_write = 1'b0;
            data_out = value;
            data_drive = 1'b1;
            chip_select_n = 1'b0;
            #(BUS_CYCLE) chip_select_n = 1'b1;
            #(BUS_CYCLE / 4) data_drive = 1'b0;
            read_write = 1'b1;
        end
    endtask

    task bus_read(output [7:0] value);
        begin
            address = 3'd0;
            read_write = 1'b1;
            chip_select_n = 1'b0;
            #(BUS_CYCLE) value = data;
            chip_select_n = 1'b1;
        end
    endtask

    // Waits until `at`, picoseconds; a time already past is an error.
    task wait_until(input real at);
        begin
            if (at < $realtime * PS) begin
                $display("error: %0.0f ps is past", at);
                probe_report = 1'b1;
                #1 $finish;
            end
            #((at - $realtime * PS) / PS);
        end
    endtask

    reg [8*256:1] filename;
    integer file, scanned, register;
    real at, width;
    reg [8*8:1] operation;
    reg [7:0] value;

    // The GPS 1 PPS on its own, so that a pulse never waits on a bus cycle.
    initial begin : gps
        integer gps_file;
        real gps_at, gps_width;
        reg [8*8:1] gps_operation;
        reg [8*64:1] rest;
        if (!$value$plusargs("stimulus=%s", filename)) begin
            $display("error: no +stimulus=FILE");
            probe_report = 1'b1;
            #1 $finish;
        end
        gps_file = $fopen(filename, "r");
        while ($fscanf(gps_file, "%f %s", gps_at, gps_operation) == 2) begin
            if (gps_operation == "gps") begin
                scanned = $fscanf(gps_file, "%f", gps_width);
                wait_until(gps_at);
                gps_pps = 1'b1;
                #(gps_width / PS) gps_pps = 1'b0;
            end else
                scanned = $fgets(rest, gps_file);
        end
    end

    initial begin
        if (!$value$plusargs("stimulus=%s", filename)) $finish;
        file = $fopen(filename, "r");
        if (file == 0) begin
            $display("error: cannot open %0s", filename);
            probe_report = 1'b1;
            #1 $finish;
        end
        while ($fscanf(file, "%f %s", at, operation) == 2) begin
            if (operation == "gps") begin
                scanned = $fscanf(file, "%f", width);
            end else begin
                wait_until(at);
                if (operation == "reset") reset_n = 1'b1;
                else if (operation == "write") begin
                    scanned = $fscanf(file, "%d %h", register, value);
                    bus_write(register[1:0], value);
                end else if (operation == "read") begin
                    bus_read(value);
                    $display("read %0.0f %02x", at, value);
                end else if (operation == "pins") begin
                    scanned = $fscanf(file, "%h", value);
                    pins = value;
                end else if (operation == "end") begin
                    probe_report = 1'b1;
                    #1 $finish;
                end
                else begin
                    $display("error: unknown operation %0s", operation);
                    probe_report = 1'b1;
                    #1 $finish;
                end
            end
        end
        $display("error: no end");
        probe_report = 1'b1;
        #1 $finish;
    end
endmodule
