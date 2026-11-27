// The body every <image>_stimulus.v shares: an FPGA netlist driven from
// a stimulus file, for tools/fpga/differential.py to compare with the
// emulator's models.  An image's stimulus module includes it at its top,
// with STIMULUS_INTERVAL defined as the hierarchical name of the net the
// interpolator measures, then instantiates its netlist on the registers
// and wires below, drives its own clocks and names with WATCH the pins a
// case may watch:
//
//     iverilog -g2005 -I tools/fpga -o target/fpga/z3805a_stimulus \
//         tools/fpga/xc3000.v tools/fpga/probe.v target/fpga/z3805a.v \
//         tools/fpga/z3805a_stimulus.v
//     vvp -n target/fpga/z3805a_stimulus +stimulus=FILE
//
// Each line of FILE is a time in picoseconds, the lines in order, and an
// operation:
//
// | Line | Does |
// | ---- | ---- |
// | `T reset` | RESET, Low from the start, goes High |
// | `T write R V` | a bus write of V, hex, to register R (0 to 7) |
// | `T read` | a bus read of the port; prints `read T V` |
// | `T gps W` | the GPS 1 PPS on P82 rises, and falls W ps later |
// | `T pins V` | the interpolator's byte on P3 to P11, hex |
// | `T watch P` | each change of pin P from now on prints `edge P T L` |
// | `T end` | the simulation ends |
//
// A bus cycle is `z3805a_tb.v`'s: the select Low for BUS_CYCLE, a write's
// data held a quarter of one after; a read is sampled as the select
// rises.  Each rise of `pps`, the internal 1 PPS on P83, prints `pps T`,
// and each pulse of STIMULUS_INTERVAL, the start's interval the
// interpolator measures, `interval T W`: its rise and its width,
// picoseconds.  probe_report rises at the end, for the image's probes.

    localparam real BUS_CYCLE = 1000.0;
    // Picoseconds a nanosecond, the stimulus's unit.
    localparam real PS = 1000.0;

    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;
    reg [7:0] pins = 8'h00;
    // The data bus, High where neither side drives it.  The designs' pads
    // have no pull-ups (outputs.md, "Netlists"), and an undriven input is
    // Verilog's x, which the models' tables pass on whatever they read;
    // the results are the same with the bus held Low.  What the board's
    // bus does between cycles is not established.
    tri1 [7:0] data;
    // The internal 1 PPS, P83 in every design.
    wire pps;
    reg probe_report = 1'b0;
    // The pins watched, by number.
    reg [84:1] watched = 84'd0;

// `WATCH(P, signal)`: the image's pin P, `signal`, printed as it changes
// once a case watches it.
`define WATCH(pin, signal) \
    always @(signal) if (watched[pin]) $display("edge %0d %0.0f %0d", pin, $realtime * PS, signal);

    assign data = data_drive ? data_out : 8'hzz;

    always @(posedge pps) $display("pps %0.0f", $realtime * PS);

    // The start's interval: its rise and its width.
    real interval_rise = 0.0;
    always @(posedge `STIMULUS_INTERVAL) interval_rise = $realtime;
    always @(negedge `STIMULUS_INTERVAL)
        $display("interval %0.0f %0.0f", interval_rise * PS, ($realtime - interval_rise) * PS);

    task bus_write(input [2:0] register, input [7:0] value);
        begin
            address = register;
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

    // The simulation's end, the probes reporting first.
    task finish_run;
        begin
            probe_report = 1'b1;
            #1 $finish;
        end
    endtask

    // Waits until `at`, picoseconds; a time already past is an error.
    task wait_until(input real at);
        begin
            if (at < $realtime * PS) begin
                $display("error: %0.0f ps is past", at);
                finish_run;
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
            finish_run;
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
            finish_run;
        end
        while ($fscanf(file, "%f %s", at, operation) == 2) begin
            if (operation == "gps") begin
                scanned = $fscanf(file, "%f", width);
            end else begin
                wait_until(at);
                if (operation == "reset") reset_n = 1'b1;
                else if (operation == "write") begin
                    scanned = $fscanf(file, "%d %h", register, value);
                    bus_write(register[2:0], value);
                end else if (operation == "read") begin
                    bus_read(value);
                    $display("read %0.0f %02x", at, value);
                end else if (operation == "pins") begin
                    scanned = $fscanf(file, "%h", value);
                    pins = value;
                end else if (operation == "watch") begin
                    scanned = $fscanf(file, "%d", register);
                    watched[register] = 1'b1;
                end else if (operation == "end") finish_run;
                else begin
                    $display("error: unknown operation %0s", operation);
                    finish_run;
                end
            end
        end
        $display("error: no end");
        finish_run;
    end
