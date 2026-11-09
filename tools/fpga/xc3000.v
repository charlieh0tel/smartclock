// Models of the XC3000 family's blocks, for the netlists netlist.py
// writes from netdump.js's JSON.  Written from the data sheet,
// third_party/xilinx/xc3000.pdf ("XC3000 Series Field Programmable Gate
// Arrays", Version 3.1, November 9, 1998), cited by its page numbers.
// They model function, not timing.  Each logic and I/O block's outputs
// and its pad's driver take BLOCK_DELAY, one nominal delay, and routing
// none, so that a flip-flop samples its data before the effects of its
// own clock edge arrive, as the part's delays order them; a clock
// buffer takes none, so that every flip-flop on a clock net sees the
// edge at once.  The data sheet's delays (7-41 on) are not modeled.
//
// A table's bit order and the names of the selections are the
// decoder's, as netdump.js reports them: bit i of a function table is
// its value where its input j is bit j of i, input 0 always A.

`timescale 1ns / 1ps

`define BLOCK_DELAY 1

// A configurable logic block: Figure 5 (7-9) and Figure 6 (7-10).
//
// Its combinational section is two functions of four inputs, F and G.
// Each takes A and three selections: B, QX or QY, then C, QX or QY,
// then D or E (F_INPUT1 to F_INPUT3, G_INPUT1 to G_INPUT3).  In mode FG
// they are independent (Figure 6a); with FG_MUX, modes F and FGM, E
// selects G when High and F when Low, and both outputs carry the result
// (Figure 6b, 6c).
//
// With FG_MUX the one result is both F and G, which netdump.js names F
// in mode F and M in mode FGM, as XACT does; any name but G or DI is f.
//
// Its two flip-flops, QX and QY, take F, G or DI (X_DATA, Y_DATA) and
// share a clock, enable and reset (7-9): K, active on its rising edge, or
// its falling one with K_INVERT; EC, which when Low keeps their state,
// High when not used (USE_EC); RD, an asynchronous reset when High, not
// used without USE_RD; and the global reset, GR, from the chip's RESET.
//
// X is F or QX (X_OUTPUT), Y is G or QY (Y_OUTPUT).
module xc3000_clb #(
    parameter [15:0] F_TABLE = 16'h0000,
    parameter [15:0] G_TABLE = 16'h0000,
    parameter F_INPUT1 = "B",
    parameter F_INPUT2 = "C",
    parameter F_INPUT3 = "D",
    parameter G_INPUT1 = "B",
    parameter G_INPUT2 = "C",
    parameter G_INPUT3 = "D",
    parameter FG_MUX = 0,
    parameter X_DATA = "F",
    parameter Y_DATA = "G",
    parameter X_OUTPUT = "F",
    parameter Y_OUTPUT = "G",
    parameter USE_EC = 0,
    parameter USE_RD = 0,
    parameter K_INVERT = 0
) (
    input A, B, C, D, E, DI, EC, K, RD, GR,
    output X, Y
);
    // Configuration resets the flip-flops (7-9).
    reg QX = 1'b0;
    reg QY = 1'b0;

    // The signal a table input selects; an unset selection reads Low.
    function select;
        input [15:0] name;
        input b, c, d, e, qx, qy;
        select = name == "B" ? b : name == "C" ? c : name == "D" ? d :
                 name == "E" ? e : name == "QX" ? qx : name == "QY" ? qy : 1'b0;
    endfunction

    wire [15:0] f_bits = F_TABLE;
    wire [15:0] g_bits = G_TABLE;
    wire f_table = f_bits[{select(F_INPUT3, B, C, D, E, QX, QY),
                           select(F_INPUT2, B, C, D, E, QX, QY),
                           select(F_INPUT1, B, C, D, E, QX, QY), A}];
    wire g_table = g_bits[{select(G_INPUT3, B, C, D, E, QX, QY),
                           select(G_INPUT2, B, C, D, E, QX, QY),
                           select(G_INPUT1, B, C, D, E, QX, QY), A}];
    wire merged = E ? g_table : f_table;
    wire f = FG_MUX ? merged : f_table;
    wire g = FG_MUX ? merged : g_table;

    wire x_data = X_DATA == "DI" ? DI : X_DATA == "G" ? g : f;
    wire y_data = Y_DATA == "DI" ? DI : Y_DATA == "F" ? f : g;
    wire clock = K_INVERT ? ~K : K;
    wire enable = USE_EC ? EC : 1'b1;
    wire reset = GR | (USE_RD ? RD : 1'b0);
    always @(posedge clock or posedge reset)
        if (reset) begin
            QX <= 1'b0;
            QY <= 1'b0;
        end else if (enable) begin
            QX <= x_data;
            QY <= y_data;
        end

    assign #`BLOCK_DELAY X = X_OUTPUT == "QX" ? QX : f;
    assign #`BLOCK_DELAY Y = Y_OUTPUT == "QY" ? QY : g;
endmodule

// An I/O block: Figure 4 (7-7) and its options (7-8).
//
// Output: O, inverted with OUT_INVERT, direct or through a flip-flop
// (OUT_REGISTERED) on the rising edge of the die-edge clock line on OK,
// or its falling one with OK_INVERT.  The buffer is off without
// OUT_ENABLE; with THREE_STATE it is on while T is Low (High with
// THREE_STATE_INVERT), else always on.  Slew rate is not modeled.
//
// Input: I is the pad; Q its flip-flop (IN_REGISTERED), on the rising
// edge of the clock line on IK (falling with IK_INVERT), or with IN_LATCH
// a latch, transparent while that clock is Low: "A clock line that
// triggers the flip-flop on the rising edge is an active Low Latch Enable
// (Latch transparent) signal" (Figure 4).  PULLUP is the passive pull-up.
// Both storage elements are reset by configuration and GR (7-8).
module xc3000_iob #(
    parameter OUT_ENABLE = 0,
    parameter OUT_REGISTERED = 0,
    parameter OUT_INVERT = 0,
    parameter OK_INVERT = 0,
    parameter THREE_STATE = 0,
    parameter THREE_STATE_INVERT = 0,
    parameter IN_REGISTERED = 0,
    parameter IN_LATCH = 0,
    parameter IK_INVERT = 0,
    parameter PULLUP = 0
) (
    inout PAD,
    input O, T, OK, IK, GR,
    output I, Q
);
    reg out_flip_flop = 1'b0;
    reg in_storage = 1'b0;

    wire out_data = OUT_INVERT ? ~O : O;
    wire out_clock = OK_INVERT ? ~OK : OK;
    always @(posedge out_clock or posedge GR)
        if (GR) out_flip_flop <= 1'b0;
        else out_flip_flop <= out_data;
    wire out = OUT_REGISTERED ? out_flip_flop : out_data;
    wire control = THREE_STATE_INVERT ? ~T : T;
    wire driving = !THREE_STATE || control == 1'b0;

    // An input-only block has no driver at all, so that a synthesis tool
    // does not read the pad as driven.
    generate
        if (OUT_ENABLE) begin : buffer
            assign #`BLOCK_DELAY PAD = driving ? out : 1'bz;
        end
        if (PULLUP) begin : passive
            pullup resistor (PAD);
        end
    endgenerate

    assign #`BLOCK_DELAY I = PAD;
    wire in_clock = IK_INVERT ? ~IK : IK;
    generate
        if (IN_LATCH) begin : latch
            always @*
                if (GR) in_storage = 1'b0;
                else if (!in_clock) in_storage = PAD;
        end else begin : flip_flop
            always @(posedge in_clock or posedge GR)
                if (GR) in_storage <= 1'b0;
                else in_storage <= PAD;
        end
    endgenerate
    assign #`BLOCK_DELAY Q = IN_REGISTERED ? in_storage : 1'b0;
endmodule

// The global and alternate clock buffers, GCLK and ACLK (7-17): a buffer
// from one routing net to the clock nets, the global one also driving
// die-edge clock lines for the I/O blocks.
module xc3000_buffer (
    input I,
    output O
);
    assign O = I;
endmodule

// A 3-state buffer onto a horizontal Longline: drives it while T is Low
// (7-17; Figures 16 and 17, 7-16).
module xc3000_tbuf (
    input I, T,
    output O
);
    assign #`BLOCK_DELAY O = T ? 1'bz : I;
endmodule
