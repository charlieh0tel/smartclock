// The Z3815A's FPGA netlist, z3815a_fpga, driven from a stimulus file,
// for tools/fpga/differential.py to compare with the emulator's LCA
// model (crates/smartclock-emu/src/board/fpga/lca.rs).  The file, the bus
// and what it prints are stimulus.vh's; the Z3815A decodes eight
// register addresses, 0 to 7, though only 0 to 3 land anywhere
// (z3815a-trace.md, "Register port"):
//
//     iverilog -g2005 -I tools/fpga -o target/fpga/z3815a_stimulus \
//         tools/fpga/xc3000.v tools/fpga/probe.v target/fpga/z3815a.v \
//         tools/fpga/z3815a_stimulus.v
//     vvp -n target/fpga/z3815a_stimulus +stimulus=FILE
//
// The clocks are two, as z3816a_tb.v's: P53 at 20 MHz, rising at 25 ns
// and every 50 ns on, whose halving by AB is GCLK, the fabric's 10 MHz
// and the firmware's tick; and P7, the P7 divider's, a 10 MHz of its
// own, rising at 50 ns and every 100 ns on, so that its edges fall
// between ACLK's rises, on its falls, and the phase-frequency detector's
// two sides run apart.  The divider runs to 9,830,400, so the hardware
// oscillator is 9.8304 MHz (P53 = 19.6608 MHz) for a true 1 Hz; the
// logic is the same at either clock (z3815a-trace.md, "The 1 PPS
// divider").  The interval is BH.X (P81, inverted).  The pins' roles
// are z3815a_tb.v's, read from the netlist; the other inputs (P23, P27,
// P35, P50, P78, P84) are held Low.

`timescale 1ns / 1ps

`define STIMULUS_INTERVAL fpga.BH_X

module z3815a_stimulus;
`include "stimulus.vh"

    localparam real OSC_PERIOD = 50.0;
    localparam real P7_PERIOD = 100.0;

    reg osc = 1'b0;
    reg clock7 = 1'b0;
    wire pps_p14, pps_p28;
    wire p2, p16, p19, p20, p21, p24, p25, p26, p29, p30, p34, p36, p38;
    wire p40, p47, p48, p51, p56, p57, p76, p77, p79, p81;

    z3815a_fpga fpga (
        .P2(p2), .P3(pins[0]), .P4(pins[1]), .P5(pins[2]), .P6(pins[3]), .P7(clock7),
        .P8(pins[4]), .P9(pins[5]), .P10(pins[6]), .P11(pins[7]),
        .P14(pps_p14), .P16(p16), .P19(p19), .P20(p20), .P21(p21),
        .P23(1'b0), .P24(p24), .P25(p25), .P26(p26), .P27(1'b0), .P28(pps_p28),
        .P29(p29), .P30(p30), .P34(p34), .P35(1'b0), .P36(p36), .P38(p38),
        .P40(p40), .P47(p47), .P48(p48), .P50(1'b0), .P51(p51), .P53(osc),
        .P56(p56), .P57(p57),
        .P58(data[7]), .P59(data[6]), .P60(data[5]), .P61(chip_select_n),
        .P62(data[4]), .P65(data[3]), .P66(address[2]), .P67(data[2]),
        .P68(address[1]), .P69(address[0]), .P70(data[1]), .P71(data[0]),
        .P75(read_write), .P76(p76), .P77(p77), .P78(1'b0), .P79(p79),
        .P81(p81), .P82(gps_pps), .P83(pps), .P84(1'b0),
        .RESET(reset_n)
    );

    always #(OSC_PERIOD / 2) osc = ~osc;
    always #(P7_PERIOD / 2) clock7 = ~clock7;

    // The pins a case may watch.
    `WATCH(14, pps_p14)
    `WATCH(19, p19)
    `WATCH(20, p20)
    `WATCH(21, p21)
    `WATCH(24, p24)
    `WATCH(25, p25)
    `WATCH(26, p26)
    `WATCH(28, pps_p28)
    `WATCH(29, p29)
    `WATCH(30, p30)
    `WATCH(34, p34)
    `WATCH(38, p38)
    `WATCH(40, p40)
    `WATCH(51, p51)
    `WATCH(57, p57)
endmodule
