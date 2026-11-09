// A probe on one net of a netlist under a testbench, for the figures the
// FPGA docs quote from a simulation (make -f tools/fpga/Makefile
// check-readings).  With +probe, it prints, when the testbench raises
// `report`, the net's rises and falls, the shortest and longest period
// between rises, the shortest and longest time High, and, where
// `reference` is connected, the shortest and longest delay from a rise
// of `reference` to the net's next rise, and the time of its last rise.
// EACH=1 also prints every High pulse as it ends.  Times are in ns; a
// figure never seen prints as -1.
//
// The models give every block one nominal delay (xc3000.v), so a
// combinational net whose inputs change on the same clock edge by paths
// of different depth can pulse for a delay or two: a hazard of the
// model, not a pulse of the design.  The measures are of the net with
// every pulse, High or Low, shorter than GLITCH removed, and where there
// were any the line ends with how many and the time of the first.

`timescale 1ns / 1ps

module probe #(
    parameter NAME = "net",
    parameter EACH = 0,
    // Five block delays: a model hazard is one or two, and the shortest
    // pulse of the designs a half-period of the 20 MHz clock.
    parameter real GLITCH = 5.0
) (
    input signal,
    input reference,
    input report
);
    // The net, its pulses under GLITCH removed: an inertial delay, so each
    // of its edges is GLITCH after the net's.
    wire filtered;
    assign #(GLITCH) filtered = signal;

    // The filtered net's edges so far, and the extremes of each measure.
    integer rises = 0, falls = 0;
    real last_rise = 0.0, last_reference = 0.0;
    reg referenced = 1'b0;
    real period_min = -1.0, period_max = -1.0;
    real high_min = -1.0, high_max = -1.0;
    real delay_min = -1.0, delay_max = -1.0;
    // The pulses removed, and when the first began.
    integer glitches = 0;
    real first_glitch = -1.0;
    real raw_rise = -1.0, raw_fall = -1.0;

    task widen(inout real low, inout real high, input real value);
        begin
            if (low < 0.0 || value < low) low = value;
            if (high < 0.0 || value > high) high = value;
        end
    endtask

    task removed(input real start);
        begin
            if (glitches == 0) first_glitch = start;
            glitches = glitches + 1;
        end
    endtask

    always @(posedge reference) begin
        last_reference = $realtime;
        referenced = 1'b1;
    end

    always @(posedge signal) begin
        if (raw_fall >= 0.0 && raw_rise >= 0.0 && $realtime - raw_fall < GLITCH) removed(raw_fall);
        raw_rise = $realtime;
    end

    always @(negedge signal) begin
        if (raw_rise >= 0.0 && $realtime - raw_rise < GLITCH) removed(raw_rise);
        raw_fall = $realtime;
    end

    always @(posedge filtered) begin
        if (rises > 0) widen(period_min, period_max, $realtime - last_rise);
        if (referenced) begin
            widen(delay_min, delay_max, $realtime - GLITCH - last_reference);
            referenced = 1'b0;
        end
        last_rise = $realtime;
        rises = rises + 1;
    end

    always @(negedge filtered) begin
        falls = falls + 1;
        if (rises > 0) begin
            widen(high_min, high_max, $realtime - last_rise);
            if (EACH && $test$plusargs("probe"))
                $display("probe %0s high %0.1f ns from %0.9f s", NAME, $realtime - last_rise,
                         (last_rise - GLITCH) / 1.0e9);
        end
    end

    always @(posedge report)
        if ($test$plusargs("probe")) begin
            if (glitches > 0)
                $display("probe %0s: %0d rises, %0d falls; period %0.1f to %0.1f ns; high %0.1f to %0.1f ns; after reference %0.1f to %0.1f ns; last rise %0.9f s; %0d pulses under %0.1f ns removed, the first at %0.9f s",
                         NAME, rises, falls, period_min, period_max, high_min, high_max, delay_min,
                         delay_max, rises > 0 ? (last_rise - GLITCH) / 1.0e9 : 0.0, glitches, GLITCH,
                         first_glitch / 1.0e9);
            else
                $display("probe %0s: %0d rises, %0d falls; period %0.1f to %0.1f ns; high %0.1f to %0.1f ns; after reference %0.1f to %0.1f ns; last rise %0.9f s",
                         NAME, rises, falls, period_min, period_max, high_min, high_max, delay_min,
                         delay_max, rises > 0 ? (last_rise - GLITCH) / 1.0e9 : 0.0);
        end
endmodule
