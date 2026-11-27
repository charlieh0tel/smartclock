// The Z3805A's FPGA netlist, z3805a_fpga, driven from a stimulus file,
// for tools/fpga/differential.py to compare with the emulator's models
// (crates/smartclock-emu/src/board/fpga/z3805a.rs and lca.rs).  The file,
// the bus and what it prints are stimulus.vh's:
//
//     iverilog -g2005 -I tools/fpga -o target/fpga/z3805a_stimulus tools/fpga/xc3000.v \
//         tools/fpga/probe.v target/fpga/z3805a.v tools/fpga/z3805a_stimulus.v
//     vvp -n target/fpga/z3805a_stimulus +stimulus=FILE
//
// P7 is a 10 MHz clock, rising at 50 ns and every 100 ns on; the
// interval is AH.Y (P81, inverted).  The pins' roles are those of
// `z3805a_tb.v`, read from the netlist.  With +probe it prints probe.v's
// measures of the divider's and the sync's nets at the end.

`timescale 1ns / 1ps

`define STIMULUS_INTERVAL fpga.AH_Y

module z3805a_stimulus;
`include "stimulus.vh"

    // 10 MHz, its rising edges at 50 ns and every 100 ns on.
    localparam real CLOCK_PERIOD = 100.0;

    reg clock = 1'b0;
    wire pps_p78, pps_p45, pps_p14;
    wire p84, p16, p2, p72, p76, p77, p79, p81;

    z3805a_fpga fpga (
        .P2(p2), .P3(pins[0]), .P4(pins[1]), .P5(pins[2]), .P6(pins[3]), .P7(clock),
        .P8(pins[4]), .P9(pins[5]), .P10(pins[6]), .P11(pins[7]),
        .P14(pps_p14), .P16(p16), .P17(1'b1), .P45(pps_p45),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P76(p76), .P77(p77), .P78(pps_p78),
        .P79(p79), .P81(p81), .P82(gps_pps), .P83(pps), .P84(p84),
        .RESET(reset_n)
    );

    // The nets check-readings measures (probe.v; +probe prints them).
    probe #(.NAME("GI.X"), .EACH(0)) probe_gi_x (.signal(fpga.GI_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("EJ.X"), .EACH(0)) probe_ej_x (.signal(fpga.EJ_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("GJ.X"), .EACH(0)) probe_gj_x (.signal(fpga.GJ_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("GJ.Y"), .EACH(0)) probe_gj_y (.signal(fpga.GJ_Y), .reference(1'b0), .report(probe_report));
    probe #(.NAME("EK.X"), .EACH(0)) probe_ek_x (.signal(fpga.EK_X), .reference(1'b0), .report(probe_report));
    probe #(.NAME("HG.Y"), .EACH(0)) probe_hg_y (.signal(fpga.HG_Y), .reference(pps), .report(probe_report));

    always #(CLOCK_PERIOD / 2) clock = ~clock;
endmodule
