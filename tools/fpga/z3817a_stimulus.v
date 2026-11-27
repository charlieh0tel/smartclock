// The Z3817A's FPGA netlist, z3817a_fpga, driven from a stimulus file,
// for tools/fpga/differential.py to compare with the emulator's model
// (crates/smartclock-emu/src/board/fpga/z3817a.rs):
//
//     iverilog -g2005 -o target/fpga/z3817a_stimulus tools/fpga/xc3000.v \
//         tools/fpga/probe.v target/fpga/z3817a.v tools/fpga/z3817a_stimulus.v
//     vvp -n target/fpga/z3817a_stimulus +stimulus=FILE
//
// The file is z3805a_stimulus.v's, with the Z3817A's inputs: its lines
// are a time in picoseconds, the lines in order, and an operation:
//
// | Line | Does |
// | ---- | ---- |
// | `T reset` | RESET, Low from the start, goes High |
// | `T write R V` | a bus write of V, hex, to register R, 0 to 4 |
// | `T read` | a bus read of the port; prints `read T V` |
// | `T gps W` | the 1 PPS on P82 rises, and falls W ps later |
// | `T p84 W` | the 1 PPS on P84 rises, and falls W ps later |
// | `T end` | the simulation ends |
//
// A bus cycle is z3805a_stimulus.v's.  Each rise of P78, the internal
// 1 PPS, prints `pps T`, and each change of the status's bits 2 and 3,
// FI.X and GJ.X, once RESET is High, `flag T NAME V`; with +edges P16's
// and P72's changes print so too, and with +p19 P19's, one each 3.2 us.
// The pins' roles are those of z3817a_tb.v, read from the netlist.

`timescale 1ns / 1ps

module z3817a_stimulus;
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
    reg p82 = 1'b0;
    reg p84 = 1'b0;
    // The data bus, High where neither side drives it (z3805a_stimulus.v).
    tri1 [7:0] data;
    wire pps_p78, p16, p19, p72;

    assign data = data_drive ? data_out : 8'hzz;

    z3817a_fpga fpga (
        .P7(clock), .P16(p16), .P19(p19),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P78(pps_p78), .P82(p82), .P84(p84),
        .RESET(reset_n)
    );

    always #(CLOCK_PERIOD / 2) clock = ~clock;

    always @(posedge pps_p78) $display("pps %0.0f", $realtime * PS);

    // The status's flags, and the outputs with +edges and +p19, each edge
    // once RESET is High.
    reg edges = 1'b0, p19_edges = 1'b0;
    initial begin
        edges = $test$plusargs("edges");
        p19_edges = $test$plusargs("p19");
    end
    always @(fpga.FI_X) if (reset_n) $display("flag %0.0f FI.X %b", $realtime * PS, fpga.FI_X);
    always @(fpga.GJ_X) if (reset_n) $display("flag %0.0f GJ.X %b", $realtime * PS, fpga.GJ_X);
    always @(p16) if (reset_n && edges) $display("flag %0.0f P16 %b", $realtime * PS, p16);
    always @(p72) if (reset_n && edges) $display("flag %0.0f P72 %b", $realtime * PS, p72);
    always @(p19) if (reset_n && p19_edges) $display("flag %0.0f P19 %b", $realtime * PS, p19);

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

    // Waits until `at`, picoseconds; a time already past is an error.
    task wait_until(input real at);
        begin
            if (at < $realtime * PS) begin
                $display("error: %0.0f ps is past", at);
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

    // Each 1 PPS input on its own, so that a pulse never waits on a bus
    // cycle or the other input.
    initial begin : gps
        integer pps_file, pps_scanned;
        real pps_at, pps_width;
        reg [8*8:1] pps_operation;
        reg [8*64:1] rest;
        if (!$value$plusargs("stimulus=%s", filename)) begin
            $display("error: no +stimulus=FILE");
            #1 $finish;
        end
        pps_file = $fopen(filename, "r");
        while ($fscanf(pps_file, "%f %s", pps_at, pps_operation) == 2) begin
            if (pps_operation == "gps") begin
                pps_scanned = $fscanf(pps_file, "%f", pps_width);
                wait_until(pps_at);
                p82 = 1'b1;
                #(pps_width / PS) p82 = 1'b0;
            end else
                pps_scanned = $fgets(rest, pps_file);
        end
    end

    initial begin : p84_input
        integer pps_file, pps_scanned;
        real pps_at, pps_width;
        reg [8*8:1] pps_operation;
        reg [8*64:1] rest;
        if ($value$plusargs("stimulus=%s", filename)) begin
            pps_file = $fopen(filename, "r");
            while ($fscanf(pps_file, "%f %s", pps_at, pps_operation) == 2) begin
                if (pps_operation == "p84") begin
                    pps_scanned = $fscanf(pps_file, "%f", pps_width);
                    wait_until(pps_at);
                    p84 = 1'b1;
                    #(pps_width / PS) p84 = 1'b0;
                end else
                    pps_scanned = $fgets(rest, pps_file);
            end
        end
    end

    initial begin
        if (!$value$plusargs("stimulus=%s", filename)) $finish;
        file = $fopen(filename, "r");
        if (file == 0) begin
            $display("error: cannot open %0s", filename);
            #1 $finish;
        end
        while ($fscanf(file, "%f %s", at, operation) == 2) begin
            if (operation == "gps" || operation == "p84") begin
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
                end else if (operation == "end") begin
                    #1 $finish;
                end
                else begin
                    $display("error: unknown operation %0s", operation);
                    #1 $finish;
                end
            end
        end
        $display("error: no end");
        #1 $finish;
    end
endmodule
