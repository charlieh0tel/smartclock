// The 58503B's FPGA netlist, fpga_58503b, driven from a stimulus file,
// for tools/fpga/differential.py to compare with the emulator's LCA
// model (crates/smartclock-emu/src/board/fpga/lca.rs).  It is
// z3805a_stimulus.v with 58503b_tb.v's pins: P45 unused, P14 a pulse on
// alternate seconds and P19 an output.  The file, the bus and what it
// prints are stimulus.vh's:
//
//     iverilog -g2005 -I tools/fpga -o target/fpga/58503b_stimulus \
//         tools/fpga/xc3000.v tools/fpga/probe.v target/fpga/58503b.v \
//         tools/fpga/58503b_stimulus.v
//     vvp -n target/fpga/58503b_stimulus +stimulus=FILE
//
// P7 is a 10 MHz clock, rising at 50 ns and every 100 ns on; the
// interval is AH.X (P81, inverted).

`timescale 1ns / 1ps

`define STIMULUS_INTERVAL fpga.AH_X

module fpga_58503b_stimulus;
`include "stimulus.vh"

    // 10 MHz, its rising edges at 50 ns and every 100 ns on.
    localparam real CLOCK_PERIOD = 100.0;

    reg clock = 1'b0;
    wire pps_p78;
    wire p84, p16, p14, p19, p2, p72, p76, p77, p79, p81;

    fpga_58503b fpga (
        .P2(p2), .P3(pins[0]), .P4(pins[1]), .P5(pins[2]), .P6(pins[3]), .P7(clock),
        .P8(pins[4]), .P9(pins[5]), .P10(pins[6]), .P11(pins[7]),
        .P14(p14), .P16(p16), .P17(1'b1), .P19(p19),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P72(p72), .P75(read_write), .P76(p76), .P77(p77), .P78(pps_p78),
        .P79(p79), .P81(p81), .P82(gps_pps), .P83(pps), .P84(p84),
        .RESET(reset_n)
    );

    always #(CLOCK_PERIOD / 2) clock = ~clock;

    // The pins a case may watch.
    `WATCH(14, p14)
    `WATCH(19, p19)
    `WATCH(72, p72)
    `WATCH(78, pps_p78)
endmodule
