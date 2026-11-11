// A smoke test of the Z3817A's FPGA netlist, z3817a_fpga, which
// netlist.py writes from the decoded bitstream (docs/firmware/outputs.md,
// "Netlists"); docs/firmware/z3817a-trace.md reads what it prints.
// Modeled on tools/fpga/z3805a_tb.v.
//
//     make -f tools/fpga/Makefile target/fpga/z3817a_tb
//     vvp -n target/fpga/z3817a_tb +seconds=3
//
// The Z3817A's design has no interpolator inputs and a 32-bit count, and
// takes its 1 PPS on P82 or P84, as register 4's bit 0 selects.  The
// 3752 firmware writes register 4 through one routine, `0x4538c`, whose
// two callers pass 0, P82; it reads the count as four bytes, `0x18`,
// `0x16`, `0x14` and `0x12` (`0x456fc`), and the high nibble of `0x01`
// (`0x4539e`).  It clocks P7 at 10 MHz, pulses RESET, plays the register
// port as that firmware drives it, and gives a 1 PPS on P82 and, with
// +p84, on P84.  It prints the internal 1 PPS's edges and, each second,
// the arm, the ready bits and the count.  Options:
// - +seconds=N, the readings to take (3); 0 for the preset alone;
// - +gps=NS, +width=NS: the P82 1 PPS's rise within each second
//   (0.250000037 s) and its width (100 us);
// - +p84=NS: a 1 PPS on P84 too, NS into each second, +width wide;
// - +select=1: register 4's bit 0 set, P84 the count's start;
// - +sync=KIND: register 3's sync pair, 0 then KIND, on the first arm;
// - +registers: every address's read with the preset on;
// - +bus=NS: the bus cycle (1000);
// - +probe: print probe.v's measures of the nets check-readings reads.
// It has no +interpolator: the Makefile's default flags pass it, and it
// ignores it.
//
// The pins' roles are read from the netlist, not the board, so every
// one is an inference:
//
// | Pin | Role here | From |
// | --- | --------- | ---- |
// | P7 | 10 MHz | the global clock buffer's input |
// | P61 | /CS, Low selects the port | enables the bus and its write strobe (GL) |
// | P75 | R/W, High reads | drives the bus when High and selected (GL) |
// | P69, P68, P66 | ADDR0, ADDR1, ADDR2 | register 0, 1, 3 and 4's write decode (FL, EL) |
// | P71, P70, P67, P65, P62, P60, P59, P58 | DATA bits 0 to 7 | the registers' fields, as the Z3805A's |
// | P82 | a 1 PPS, rising | AG.X, the count's start, with register 4's bit 0 clear; passed to P16 |
// | P84 | a 1 PPS, rising | AG.X with register 4's bit 0 set; clocks HG and HH |
// | P78 | the internal 1 PPS | FI.Y, registered on the 10 MHz clock |
// | P16, P19, P72 | outputs | P82 passed through; EE.Y; GJ.Y |

`timescale 1ns / 1ps

module z3817a_tb;
    // 10 MHz: the count's ticks are 100 ns, the scale the 3752 firmware
    // converts the count at (0x45868).
    localparam real CLOCK_PERIOD = 100.0;
    real bus_cycle = 1000.0;
    localparam real SECOND = 1.0e9;
    real gps_offset = 250.0e6 + 37.0;
    real gps_width = 100.0e3;
    // P84's 1 PPS, NS into each second; negative for none (+p84=NS).
    real p84_offset = -1.0;

    reg clock = 1'b0;
    reg reset_n = 1'b0;
    reg chip_select_n = 1'b1;
    reg read_write = 1'b1;
    reg [2:0] address = 3'd0;
    reg [7:0] data_out = 8'h00;
    reg data_drive = 1'b0;
    reg gps_pps = 1'b0;
    reg p84_pps = 1'b0;
    // The data bus, High where neither side drives it (z3805a_tb.v).
    tri1 [7:0] data;
    wire pps_p78, p16, p19, p72;

    assign data = data_drive ? data_out : 8'hzz;

    z3817a_fpga fpga (
        .P7(clock), .P16(p16), .P19(p19),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P78(pps_p78), .P82(gps_pps), .P84(p84_pps),
        .RESET(reset_n)
    );

    // The nets check-readings measures (probe.v; +probe prints them).
    reg probe_report = 1'b0;
    probe #(.NAME("AG.X"), .EACH(0)) probe_ag_x (.signal(fpga.AG_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("AH.Y"), .EACH(1)) probe_ah_y (.signal(fpga.AH_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("AI.Y"), .EACH(1)) probe_ai_y (.signal(fpga.AI_Y), .reference(gps_pps), .report(probe_report));
    probe #(.NAME("JB.X"), .EACH(0)) probe_jb_x (.signal(fpga.JB_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("ID.Y"), .EACH(0)) probe_id_y (.signal(fpga.ID_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("LD.X"), .EACH(0)) probe_ld_x (.signal(fpga.LD_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("HD.Y"), .EACH(0)) probe_hd_y (.signal(fpga.HD_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("HF.Y"), .EACH(0)) probe_hf_y (.signal(fpga.HF_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("GE.X"), .EACH(0)) probe_ge_x (.signal(fpga.GE_X), .reference(pps_p78), .report(probe_report));
    probe #(.NAME("GH.Y"), .EACH(0)) probe_gh_y (.signal(fpga.GH_Y), .reference(pps_p78), .report(probe_report));
    probe #(.NAME("CI.X"), .EACH(0)) probe_ci_x (.signal(fpga.CI_X), .reference(pps_p78), .report(probe_report));
    probe #(.NAME("P19"), .EACH(0)) probe_p19 (.signal(p19), .reference(1'b0), .report(probe_report));
    probe #(.NAME("P72"), .EACH(0)) probe_p72 (.signal(p72), .reference(1'b0), .report(probe_report));

    always #(CLOCK_PERIOD / 2) clock = ~clock;

    // Optional VCD for waveform figures (tools/fpga/vcd_wave.py): +vcd=FILE
    // dumps the whole bench, pins and internal nets, from time zero.
    initial begin : vcd_dump
        reg [8*256:1] vcd_path;
        if ($value$plusargs("vcd=%s", vcd_path)) begin
            $dumpfile(vcd_path);
            if ($test$plusargs("vcdslim"))
                $dumpvars(0, gps_pps, p84_pps, status, pps_p78);
            else
                $dumpvars(0, z3817a_tb);
        end
    end

    // The firmware's register images: 0 to 4, at 0x1024f7.
    reg [7:0] registers [0:4];

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

    // fpga_read (0x451dc): the address into register 1's top five bits, a
    // read of the port, then register 1 back.
    task fpga_read(input [4:0] register, output [7:0] value);
        begin
            bus_write(3'd1, {register, registers[1][2:0]});
            address = 3'd0;
            read_write = 1'b1;
            #(bus_cycle / 4) chip_select_n = 1'b0;
            #(bus_cycle) value = data;
            chip_select_n = 1'b1;
            #(bus_cycle / 2);
            bus_write(3'd1, registers[1]);
        end
    endtask

    // The byte read (0x4569c): reg + 1's low nibble high, then reg's.
    task fpga_read_byte(input [4:0] register, output [7:0] value);
        reg [7:0] high, low;
        begin
            fpga_read(register + 5'd1, high);
            fpga_read(register, low);
            value = {high[3:0], low[3:0]};
        end
    endtask

    // The count (0x456fc): the bytes at 0x18, 0x16, 0x14 and 0x12.
    task read_count(output [31:0] count);
        reg [7:0] b3, b2, b1, b0;
        begin
            fpga_read_byte(5'h18, b3);
            fpga_read_byte(5'h16, b2);
            fpga_read_byte(5'h14, b1);
            fpga_read_byte(5'h12, b0);
            count = {b3, b2, b1, b0};
        end
    endtask

    task set_field(input [2:0] register, input [7:0] mask, input [7:0] value);
        fpga_write(register, (registers[register] & ~mask) | (value & mask));
    endtask

    real last_rise;
    integer rises;
    always @(posedge pps_p78) begin
        if (rises > 0)
            $display("%0.9f s: P78 rises, %0.1f ns after the last", $realtime / SECOND, $realtime - last_rise);
        else
            $display("%0.9f s: P78 rises", $realtime / SECOND);
        last_rise = $realtime;
        rises = rises + 1;
    end
    always @(negedge pps_p78)
        if (rises > 0) $display("%0.9f s: P78 falls, %0.1f ns high", $realtime / SECOND, $realtime - last_rise);

    // The P82 1 PPS, gps_offset into every second.
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

    // P84's, with +p84=NS.
    initial begin
        if ($value$plusargs("p84=%f", p84_offset)) begin
            #(p84_offset);
            forever begin
                p84_pps = 1'b1;
                #(gps_width) p84_pps = 1'b0;
                #(SECOND - gps_width);
            end
        end
    end

    integer seconds, second, polls, address_field, sync_kind, select;
    reg [7:0] status, nibble;
    reg [31:0] count;
    initial begin
        if (!$value$plusargs("seconds=%d", seconds)) seconds = 3;
        if (!$value$plusargs("bus=%f", bus_cycle)) bus_cycle = 1000.0;
        if (!$value$plusargs("sync=%d", sync_kind)) sync_kind = 0;
        if (!$value$plusargs("select=%d", select)) select = 0;
        rises = 0;
        #(10 * CLOCK_PERIOD) reset_n = 1'b1;
        #(10 * CLOCK_PERIOD);

        // The registers at start: the preset set, the rest 0; register 4's
        // bit 0, the start's select, as +select gives it.
        fpga_write(0, 8'h40);
        fpga_write(1, 8'h00);
        fpga_write(2, 8'h00);
        fpga_write(3, 8'h00);
        fpga_write(4, select ? 8'h01 : 8'h00);
        read_count(count);
        $display("%0.6f s: with the preset the count reads %08x", $realtime / SECOND, count);
        fpga_read(5'h00, status);
        $display("%0.6f s: status %02x", $realtime / SECOND, status);
        if ($test$plusargs("registers"))
            for (address_field = 0; address_field < 32; address_field = address_field + 1) begin
                fpga_read(address_field[4:0], status);
                $display("register %02x reads %02x", address_field, status);
            end

        for (second = 0; second < seconds; second = second + 1) begin
            // The arm (0x457c0): preset on, address 0, a sync's pair
            // (+sync=KIND, on the first arm), preset off.
            set_field(0, 8'h40, 8'h40);
            set_field(1, 8'hf8, 8'h00);
            if (second == 0 && sync_kind != 0) begin
                set_field(3, 8'h03, 8'h00);
                set_field(3, 8'h03, sync_kind[7:0]);
                $display("%0.6f s: sync kind %0d", $realtime / SECOND, sync_kind);
            end
            set_field(0, 8'h40, 8'h00);
            $display("%0.6f s: armed", $realtime / SECOND);
            // The wait (0x45410): status bit 1, then bit 0, every 10 ms.
            status = 8'h00;
            for (polls = 0; polls < 512 && !status[1]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[1]) #(10.0e6);
            end
            $display("%0.6f s: status %02x after %0d polls (bit 1)", $realtime / SECOND, status, polls);
            for (polls = 0; polls < 512 && status[1] && !status[0]; polls = polls + 1) begin
                fpga_read(5'h00, status);
                if (!status[0]) #(10.0e6);
            end
            $display("%0.6f s: status %02x, seconds %0d", $realtime / SECOND, status, status[7:4]);
            read_count(count);
            fpga_read(5'h01, nibble);
            $display("%0.6f s: count %08x, less the preset %0d ticks; 0x01's high nibble %0d",
                     $realtime / SECOND, count, $signed({1'b0, count}) - 33'sh55555555, nibble[7:4]);
        end
        $display("rises: P78 %0d", rises);
        probe_report = 1'b1;
        #1 $finish;
    end
endmodule
