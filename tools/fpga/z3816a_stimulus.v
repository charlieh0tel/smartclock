// The Z3816A's FPGA netlist, z3816a_fpga, driven from a stimulus file,
// for tools/fpga/differential.py to compare with the emulator's LCA
// model (crates/smartclock-emu/src/board/fpga/lca.rs).  It is
// z3815a_stimulus.v with z3816a_tb.v's pins: the same two clocks, P53
// at 20 MHz and P7 a 10 MHz of its own, and no further inputs.  The
// file, the bus and what it prints are stimulus.vh's:
//
//     iverilog -g2005 -I tools/fpga -o target/fpga/z3816a_stimulus \
//         tools/fpga/xc3000.v tools/fpga/probe.v target/fpga/z3816a.v \
//         tools/fpga/z3816a_stimulus.v
//     vvp -n target/fpga/z3816a_stimulus +stimulus=FILE
//
// The interval is BH.X (P81, inverted).  P67, bus bit 2, is an output
// the design never reads (z3816a_tb.v), wired to the bus as the others
// are, so that a read samples it.

`timescale 1ns / 1ps

`define STIMULUS_INTERVAL fpga.BH_X

module z3816a_stimulus;
`include "stimulus.vh"

    localparam real OSC_PERIOD = 50.0;
    localparam real P7_PERIOD = 100.0;

    reg osc = 1'b0;
    reg clock7 = 1'b0;
    wire pps_p14, pps_p28, pps_p20;
    wire p2, p16, p38, p40, p47, p48, p51, p52, p57, p76, p77, p79, p81;

    z3816a_fpga fpga (
        .P2(p2), .P3(pins[0]), .P4(pins[1]), .P5(pins[2]), .P6(pins[3]), .P7(clock7),
        .P8(pins[4]), .P9(pins[5]), .P10(pins[6]), .P11(pins[7]),
        .P14(pps_p14), .P16(p16), .P20(pps_p20), .P28(pps_p28),
        .P38(p38), .P40(p40), .P47(p47), .P48(p48), .P51(p51), .P52(p52),
        .P53(osc), .P57(p57),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P75(read_write), .P76(p76), .P77(p77), .P79(p79), .P81(p81),
        .P82(gps_pps), .P83(pps),
        .RESET(reset_n)
    );

    always #(OSC_PERIOD / 2) osc = ~osc;
    always #(P7_PERIOD / 2) clock7 = ~clock7;

    // The pins a case may watch.
    `WATCH(14, pps_p14)
    `WATCH(20, pps_p20)
    `WATCH(28, pps_p28)
    `WATCH(38, p38)
    `WATCH(40, p40)
    `WATCH(51, p51)
    `WATCH(57, p57)
endmodule
