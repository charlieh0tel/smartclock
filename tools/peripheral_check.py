#!/usr/bin/env python3
"""Runs the emulator's peripheral checks on both backends, Unicorn (the
oracle) and mc683xx, and prints a table: check, Unicorn result, mc683xx
result, agree (docs/emulator.md, "The peripheral checks").

Each check exercises one peripheral through the receiver's own firmware:
a script on the host port, its steps at fixed board times, under
`--deterministic` with the synthetic GPS engine from a fixed
`--gps-start`, so that a run is the same each time.  What a check
compares is a value the firmware gives -- an answer, a display, an ES
message, a lamp's changes -- never a time: mc683xx charges the CPU32's
instruction timings and Unicorn about two cycles a byte of code, so
when things happen differs and what happens must not.  Where a value
moves with board time (the ADC's readings warm up with the board) it is
compared within a tolerance, and the table says so.

Nothing is sent that AGENTS.md forbids, even to the emulator: no
:SYSTem:PRESet, :SYSTem:PON, :SYSTem:COMMunicate:*, :DIAGnostic:ERASe
or :SYSTem:LANGuage "INSTALL".  The console is reached by its
documented route, :SYSTem:LANGuage "PFORTH", and left by `halt`
(console.md, "Getting to it", "Leaving it").

Usage: peripheral_check.py [--emu PATH] [--receiver z3805a|58503a|z3801a]...
       [--check NAME]... [--slow] [--work DIR] [--jobs N] [--keep]
       [--details]

Build the emulator first, with both cores:
    cargo build --release -p smartclock-emu --features unicorn
The exit status is 1 if any check disagrees or fails on either backend.
"""

import argparse
import concurrent.futures
import dataclasses
import datetime
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
EMULATOR = os.path.join(ROOT, "target", "release", "smartclock-emu")
CORES = ("unicorn", "mc683xx")
# The synthetic engine's power-up UTC, fixed so that runs repeat.
GPS_START = "2026-10-22T12:00:00Z"
# The checks' answers wait no longer than this, board seconds; an NV
# save (*ESE, *SRE, :GPS:REFerence:ADELay) takes about 12.
ANSWER_WITHIN = 30
# A prompt: `scpi > `, `E-113> ` after an error, `p4th D > ` in the
# console, decimal.
PROMPT = r">\x20"
PROMPT_TEXT = re.compile(r"(?:scpi|E-\d+|p4th [DX]) > |E-\d+> ")
# A script's send step.
SEND = "send "
SCRIPT_RECORD = re.compile(r"smartclock-emu: script cycle=(\d+) line=(\d+) step=(\w+)")
INDICATOR = re.compile(r"smartclock-emu: indicator cycle=(\d+) bank=(\w+) name=(\w+) lit=(\d)")
DISPLAY = re.compile(r"smartclock-emu: display cycle=\d+ lamps=(0x[0-9a-f]+) text=(.*)")
# The board's clock, CPU cycles a second, on both cores once SYNCR is
# written: the indicators' and the script's cycles.
CYCLES_PER_SECOND = 16_777_216
# The ES message as digits, one a byte, as `--sci-capture` has it raw:
# year, day of the year, time, GPS-UTC offset, two digits each but the
# day's three; status bits; status_summary; CR
# (rewritten/z3805a-3543b/timecode.c, es_build, ES_DIGITS_LENGTH).
ES_LENGTH = 16
ES_END = 0x0D
# The ES message's first, about this board time on both cores, seconds,
# a bound.
ES_FROM = 16


@dataclasses.dataclass(frozen=True)
class Receiver:
    """A receiver the checks boot: its image, a bench unit's EEPROM
    dump, and what its board has."""

    name: str
    image: str
    dump: str
    # whether the SCI carries the ES message (the 58503A's and the
    # Z3801A's board files have no `sci` endpoint)
    sci: bool
    # whether it has Option 001's front panel and the PF5 alarm output
    panel: bool
    # whether the host port echoes what it is sent: not the Z3801A's,
    # as its bench unit's NV record and duartb_settings_defaults
    # (0x2f92a) set it (docs/firmware/z3801a-3543.md, "How it differs from the Z3805A")
    echo: bool
    # interpolator_calibration_state and hdac_shadow, RAM (names.tsv)
    calibration: int
    hdac_shadow: int


RECEIVERS = {
    "z3805a": Receiver(
        "z3805a",
        "third_party/firmware/images/z3805a-3543b.bin",
        "third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin",
        sci=True,
        panel=False,
        echo=True,
        calibration=0x101FCE,
        hdac_shadow=0x10314A,
    ),
    "58503a": Receiver(
        "58503a",
        "third_party/firmware/images/58503a-3704.bin",
        "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin",
        sci=False,
        panel=True,
        echo=True,
        calibration=0x1022E0,
        hdac_shadow=0x103470,
    ),
    "z3801a": Receiver(
        "z3801a",
        "third_party/firmware/images/z3801a-3543.bin",
        "third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin",
        sci=False,
        panel=False,
        echo=False,
        calibration=0x101FC6,
        hdac_shadow=0x10311C,
    ),
}


class Script:
    """A host-port script (docs/emulator.md, "Scripts"), built a step at
    a time."""

    def __init__(self):
        self.lines = [f"expect scpi {PROMPT} within 10"]

    def at(self, seconds):
        self.lines.append(f"at {seconds}")
        return self

    def send(self, *commands):
        for command in commands:
            self.lines.append(f"send {command}")
            self.lines.append(f"expect {PROMPT} within {ANSWER_WITHIN}")
        return self

    def key(self, name):
        self.lines.append(f"key {name}")
        return self

    def text(self):
        return "\n".join(self.lines + ["stop", ""])


@dataclasses.dataclass
class Run:
    """One emulator run's results."""

    core: str
    status: int
    transcript: str
    errors: list
    eeprom: bytes
    seconds: float
    # every byte the SCI sent, raw (`--sci-capture`)
    sci: bytes
    # whether the port echoed each line sent, and the lines the script
    # sent, in order
    echo: bool = True
    sent: tuple = ()

    def answers(self, command):
        """Every answer to `command`, in order: the text between its
        echo and the next prompt, stripped; none, and an error's prompt
        after it, as that prompt (`E-230>`).  A port that does not echo
        answers each line sent, after the last's prompt, with its text
        and the next prompt: the sends and the prompts after the
        power-up's paired in order."""
        found = []
        pieces = PROMPT_TEXT.split(self.transcript)
        prompts = PROMPT_TEXT.findall(self.transcript)
        for at, (segment, prompt) in enumerate(zip(pieces[1:], prompts[1:] + [""])):
            if self.echo:
                head, _, rest = segment.partition("\r\n")
            else:
                head, rest = (self.sent[at] if at < len(self.sent) else None), segment
            if head is not None and head.strip() == command:
                rest = rest.strip()
                found.append(rest or prompt.strip() if prompt.startswith("E-") else rest)
        return found

    def answer(self, command):
        answers = self.answers(command)
        return answers[0] if answers else None

    def indicators(self):
        """The lamps' and LEDs' changes: (cycle, bank, name, lit)."""
        return [
            (int(cycle), bank, name, lit == "1")
            for cycle, bank, name, lit in (
                m.groups() for m in map(INDICATOR.match, self.errors) if m
            )
        ]

    def tapped(self, prefix):
        """The lines the tap copied after `prefix`, bytes each."""
        return [unescape(line[len(prefix):]) for line in self.errors if line.startswith(prefix)]

    def resets(self):
        """The resets after power-up each core reports."""
        return [
            line
            for line in self.errors
            if line.startswith("smartclock-emu: reset ") and "cause=PowerOn" not in line
        ]

    def step_cycles(self):
        """The cycle each script line was taken at."""
        return {
            int(line): int(cycle)
            for cycle, line, _ in (m.groups() for m in map(SCRIPT_RECORD.match, self.errors) if m)
        }


def unescape(text):
    """Bytes from Rust's `escape_ascii`: `\\xHH`, `\\n`, `\\r`, `\\t`,
    and a backslash before a quote or a backslash."""
    out = bytearray()
    at = 0
    while at < len(text):
        char = text[at]
        if char != "\\":
            out += char.encode("latin-1")
            at += 1
        elif text[at + 1] == "x":
            out.append(int(text[at + 2:at + 4], 16))
            at += 4
        else:
            out += ESCAPES.get(text[at + 1], text[at + 1]).encode("latin-1")
            at += 2
    return bytes(out)


# `escape_ascii`'s named escapes.
ESCAPES = {"n": "\n", "r": "\r", "t": "\t"}


class Runner:
    """Runs the emulator, each run in a work directory of its own."""

    def __init__(self, emulator, work):
        self.emulator = emulator
        self.work = work

    def run(self, receiver, core, tag, script, stop_after, options=(), eeprom=None):
        """`script` on `receiver` and `core`; its EEPROM's working file
        `eeprom`, kept as it is if it exists, else a fresh one from the
        dump."""
        directory = os.path.join(self.work, f"{receiver.name}-{core}-{tag}")
        os.makedirs(directory, exist_ok=True)
        script_path = os.path.join(directory, "script.txt")
        with open(script_path, "w", encoding="ascii") as file:
            file.write(script.text())
        if eeprom is None:
            eeprom = os.path.join(directory, "eeprom.bin")
            if os.path.exists(eeprom):
                os.remove(eeprom)
        capture = os.path.join(directory, "sci.bin")
        command = [
            self.emulator,
            os.path.join(ROOT, receiver.image),
            "--core", core,
            "--eeprom", eeprom,
            "--eeprom-dump", os.path.join(ROOT, receiver.dump),
            # the SCI where it is served: a board file with an `sci`
            # endpoint, which mc683xx refuses `--sci` without, or
            # Unicorn's board, which always has one
            *(["--sci", "127.0.0.1:0", "--sci-capture", capture]
              if receiver.sci or core == "unicorn" else []),
            "--deterministic",
            "--gps-start", GPS_START,
            "--script", script_path,
            "--stop-after", str(stop_after),
            *options,
        ]
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, check=False)
        seconds = time.monotonic() - started
        with open(os.path.join(directory, "transcript.txt"), "wb") as file:
            file.write(result.stdout)
        with open(os.path.join(directory, "stderr.txt"), "wb") as file:
            file.write(result.stderr)
        with open(eeprom, "rb") as file:
            contents = file.read()
        sci = b""
        if os.path.exists(capture):
            with open(capture, "rb") as file:
                sci = file.read()
        return Run(
            core,
            result.returncode,
            result.stdout.decode("latin-1"),
            result.stderr.decode("latin-1").splitlines(),
            contents,
            seconds,
            sci,
            receiver.echo,
            tuple(line.removeprefix(SEND) for line in script.lines if line.startswith(SEND)),
        )


@dataclasses.dataclass
class Result:
    """A check's outcome on one core: what it found, and whether that
    is what the docs expect of the firmware."""

    value: object
    ok: bool
    detail: str = ""


def judge(value, ok, detail=""):
    return Result(value, bool(ok), detail)


# ---------------------------------------------------------------------
# The runs each check reads.

def session_script(receiver):
    """The fast session most checks read: the boot's answers at 10 s,
    the time code at 30, 40 and 70 s, the GPS, ADC, EFC and loop
    answers from 72 s, and on the 58503A its keys from 80 s."""
    script = (
        Script()
        .at(10)
        .send("*IDN?", ":SYSTem:ERRor?", ":DIAGnostic:TEST:RESult?")
        .at(30).send(":PTIMe:TCODe?")
        .at(40).send(":PTIMe:TCODe?")
        .at(70).send(":PTIMe:TCODe?")
        .at(72)
        .send(
            ":GPS:SATellite:TRACking?",
            ":GPS:SATellite:TRACking:COUNt?",
            ":GPS:SATellite:VISible:PREDicted?",
            ":GPS:POSition?",
            ":PTIMe:LEAPsecond:ACCumulated?",
            ":PTIMe:LEAPsecond:STATe?",
            ":DIAGnostic:TEMPerature?",
            ":DIAGnostic:ROSCillator:CURRent?",
            ":DIAGnostic:ROSCillator:EFControl:ABSolute?",
            ":SYNChronization:TINTerval?",
            ":SYNChronization:STATe?",
            ":LED:GPSLock?",
            ":SYSTem:STATus?",
        )
    )
    if receiver.panel:
        for seconds, key in PANEL_KEYS:
            script.at(seconds).key(key)
    return script.at(SESSION_END)


# The 58503A's keys, each at its board time: Shift then Alt is the
# alarm's clear (58503a-3704.md, "The front panel").
PANEL_KEYS = [(80, "sat"), (82, "time"), (84, "lat"), (86, "long"), (88, "shift"), (89, "alt"), (92, "status")]
SESSION_END = 96


def console_script(receiver):
    """The pForth console, at 30 s: arithmetic, the interpolator's
    calibration record and the hex DAC's shadow read with `c@`, the ADC's
    averages and the EFC, then `halt` back to SCPI (console.md)."""
    return (
        Script()
        .at(30)
        .send(
            ':SYSTem:LANGuage "PFORTH"',
            "1 2 + .",
            ": rb over + swap do i c@ u. loop ;",
            f"{receiver.calibration} 9 rb",
            f"{receiver.hdac_shadow} 6 rb",
            "pr_adc_avg",
            "pr_efc",
            "halt",
            ":SYSTem:LANGuage?",
        )
    )


# A safe, documented setting the NV register keeps: the antenna delay
# (097-59551-02 5-17), as the status screen shows it.
ANTENNA_DELAY = "150E-9"


def eeprom_scripts():
    """The setting written, then, on a second boot from the same working
    file, read back."""
    first = (
        Script()
        .at(10)
        .send(":GPS:REFerence:ADELay?", f":GPS:REFerence:ADELay {ANTENNA_DELAY}", ":GPS:REFerence:ADELay?")
        .at(40)
    )
    second = Script().at(10).send(":GPS:REFerence:ADELay?")
    return first, second


def watchdog_script():
    """`4 crash` in the console: the fatal routine, from the console's
    user-mode task a privilege violation, no handler, the interrupt mask
    raised, `hmon` starved and the watchdog's reset (restart.md, "The
    watchdog"); then the boot again and its answers."""
    script = Script().at(20).send(':SYSTem:LANGuage "PFORTH"')
    script.lines.append("send 4 crash")
    script.lines.append(f"expect scpi {PROMPT} within 40")
    return script.at(60).send("*IDN?", ":SYSTem:ERRor?", ":DIAGnostic:LOG:READ:ALL?")


def alarm_script():
    """The 58503A's alarm, *STB? AND *SRE (58503a-3704.md, "The alarm
    outputs"; 097-59551-02 5-42): the command error's ESR bit enabled
    into ESB, ESB into the alarm, a command error raising it, *CLS
    clearing it; then the defaults put back."""
    return (
        Script()
        .at(10)
        .send("*ESE 32", "*SRE 32", ":NOSuch?", "*STB?")
        .at(40).send(":LED:ALARm?", "*CLS")
        .at(45).send(":LED:ALARm?", "*ESE 0", "*SRE 136")
    )


# The loop's run: the oscillator this far off, and how long, board
# seconds; its queries every LOOP_EVERY from LOOP_EVERY.
LOOP_OFFSET = "1e-8"
LOOP_SECONDS = 2000
LOOP_EVERY = 60
# How far the two cores' last TI, seconds, and EFC, counts, may be
# apart: when the firmware's loop acts, a second earlier or later on
# one core, moves the phase it pulls in from by 10 ns a second at 1e-8
# (docs/emulator.md, "The peripheral checks"), and the pull-in is not
# done by LOOP_SECONDS.  The EFC's: 2e-11 of the firmware's G,
# 6.25e-13 a count.
LOOP_TI_APART = 5e-9
LOOP_EFC_APART = 32
# How far apart the two cores' first LOCK may be, seconds: a read.  The
# firmware leaves its power-up stages on whole seconds and its blocks,
# which the CPU's speed moves; the Z3805A locks a read apart.
LOOP_LOCK_APART = LOOP_EVERY
# The oscillator's EFC on frequency: 1e-8 / 6.25e-13 counts below the
# middle, 524288 (structures.md, "The EFC's gain").
LOOP_EFC_ON_FREQUENCY = 524_288 - 16_000


def loop_script(receiver):
    script = Script()
    for seconds in range(LOOP_EVERY, LOOP_SECONDS + 1, LOOP_EVERY):
        script.at(seconds).send(
            ":SYNChronization:STATe?",
            ":SYNChronization:TINTerval?",
            ":DIAGnostic:ROSCillator:EFControl:ABSolute?",
        )
    return script.send(
        ':SYSTem:LANGuage "PFORTH"',
        ": rb over + swap do i c@ u. loop ;",
        f"{receiver.calibration} 9 rb",
        f"{receiver.hdac_shadow} 6 rb",
        "halt",
    )


# ---------------------------------------------------------------------
# The checks: each reads its runs, one core's, and gives a Result.

def number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def check_host(runs):
    session, console = runs["session"], runs["console"]
    values = {
        "*IDN?": session.answer("*IDN?"),
        ":SYST:ERR?": session.answer(":SYSTem:ERRor?"),
        "1 2 + .": console.answer("1 2 + ."),
        "LANG? after halt": console.answer(":SYSTem:LANGuage?"),
    }
    ok = (
        values["*IDN?"] is not None
        and values["*IDN?"].startswith("HEWLETT-PACKARD,")
        and values[":SYST:ERR?"] == '+0,"No error"'
        and values["1 2 + ."] == "3"
        and values["LANG? after halt"] == '"PRIMARY"'
    )
    return judge(values, ok)


def gps_messages(run, direction):
    """What the tap shows of the GPS link one way, `<` the firmware's
    commands, `>` the engine's answers: each message's `@@Xx`, in order
    of first appearance, and how many distinct messages, bytes and
    all."""
    order = []
    distinct = set()
    for message in run.tapped(f"smartclock-emu: gps {direction} "):
        if message.startswith(b"@@"):
            distinct.add(message)
            name = message[:4].decode("latin-1")
            if name not in order:
                order.append(name)
    return " ".join(order), len(distinct)


STATUS_CLOCK = re.compile(r"\d\d:\d\d:\d\d")


def status_screen(run):
    """The status screen, its clock masked: the query's board time
    differs between cores by the answers before it."""
    text = run.answer(":SYSTem:STATus?")
    return STATUS_CLOCK.sub("hh:mm:ss", text) if text else None


def check_gps(runs):
    session = runs["session"]
    tracked = session.answer(":GPS:SATellite:TRACking?")
    count = session.answer(":GPS:SATellite:TRACking:COUNt?")
    screen = status_screen(session) or ""
    tracking = re.search(r"Tracking: (\d+)", screen)
    commands, distinct_commands = gps_messages(session, "<")
    answers, _ = gps_messages(session, ">")
    values = {
        "commands": commands,
        "distinct commands": distinct_commands,
        "answers": answers,
        "tracked": tracked,
        "count": count,
        "visible": session.answer(":GPS:SATellite:VISible:PREDicted?"),
        "position": session.answer(":GPS:POSition?"),
        "status tracking": tracking.group(1) if tracking else None,
    }
    # the sky's six above the mask (docs/emulator.md, "The synthetic GPS
    # engine")
    ok = count == "+6" and values["status tracking"] == "6" and "@@Ba" in commands and "@@Ba" in answers
    return judge(values, ok)


def es_messages(run):
    """The ES messages the SCI sent, from its raw capture, decoded:
    (year, day, seconds of the day, offset, status, summary)."""
    stream = run.sci
    # the capture starts at power-up, so at a message's start
    messages = []
    for at in range(0, len(stream) - ES_LENGTH + 1, ES_LENGTH):
        d = stream[at:at + ES_LENGTH]
        if any(digit > 9 for digit in d[:13]) or d[15] != ES_END:
            return messages, f"not an ES message at byte {at}: {d.hex(' ')}"
        year = d[0] * 10 + d[1]
        day = d[2] * 100 + d[3] * 10 + d[4]
        seconds = (d[5] * 10 + d[6]) * 3600 + (d[7] * 10 + d[8]) * 60 + d[9] * 10 + d[10]
        messages.append((year, day, seconds, d[11] * 10 + d[12], d[13], d[14]))
    return messages, ""


def check_sci(runs):
    messages, problem = es_messages(runs["session"])
    if not messages:
        return judge("no ES message", False, problem)
    steps = sorted({b[2] - a[2] for a, b in zip(messages, messages[1:])})
    values = {
        "messages": len(messages),
        "steps": steps,
        # what the message says but its time, which follows the
        # firmware's clock (the time check)
        "fields": sorted({(m[3], m[4], m[5]) for m in messages}),
        "first": messages[0][:2],
    }
    # sent on even seconds of the firmware's clock (timecode.c,
    # timecode_update), so 2 s apart, from about 10 s of board time
    ok = not problem and steps == [2] and len(messages) >= (SESSION_END - ES_FROM) / 2
    return judge(values, ok, problem)


def timecode_seconds(code):
    """A time code's seconds: T1's GPS seconds or T2's calendar (097-z3801-01,
    "Detailed Description of the Two Time Code Formats")."""
    if code is None:
        return None
    if code.startswith("T1#H"):
        return int(code[4:12], 16)
    if code.startswith("T2"):
        digits = code[2:16]
        moment = datetime.datetime.strptime(digits, "%Y%m%d%H%M%S")
        return int(moment.replace(tzinfo=datetime.timezone.utc).timestamp())
    return None


def check_time(runs):
    session = runs["session"]
    codes = session.answers(":PTIMe:TCODe?")
    seconds = [timecode_seconds(code) for code in codes]
    steps = [b - a for a, b in zip(seconds, seconds[1:])] if None not in seconds else None
    values = {
        "advance over 10 s, 30 s": steps,
        "flags": sorted({code[-7:-2] for code in codes}),
        "leap accumulated": session.answer(":PTIMe:LEAPsecond:ACCumulated?"),
        "leap state": session.answer(":PTIMe:LEAPsecond:STATe?"),
    }
    # each code answers at the clock's next second (timecode.c), so one
    # second a second of board time
    ok = steps == [10, 30]
    return judge(values, ok, f"codes {codes}")


# The ADC's readings: each quantity's plausible range (monitors.c's
# bounds, docs/emulator.md "What is modeled", ADC) and the most two
# backends may differ by: the readings warm with board time and carry
# noise, and the cores sample them at different times.
ADC_LIMITS = {
    "Temperature": ((20.0, 40.0), 0.3),
    "5V": ((4.75, 5.25), 0.05),
    "+15V": ((14.25, 15.75), 0.1),
    "-15V": ((-15.75, -14.25), 0.1),
    "Oven": ((-5.0, 5.5), 0.1),
    "Double oven": ((-5.0, 5.5), 0.1),
    "Oven current": ((50.0, 300.0), 2.0),
}


# The 58503A's names for the Z3805A's channels in pr_adc_avg; it has
# no double oven.
ADC_ALIASES = {"15V": "+15V", "Primary oven current": "Oven current"}


def adc_readings(runs):
    session, console = runs["session"], runs["console"]
    readings = {}
    for line in (console.answer("pr_adc_avg") or "").splitlines():
        name, _, value = line.partition(":")
        name = ADC_ALIASES.get(name.strip(), name.strip())
        if name in ADC_LIMITS:
            readings[name] = number(value)
    readings["TEMP?"] = number(session.answer(":DIAGnostic:TEMPerature?"))
    readings["ROSC:CURR?"] = number(session.answer(":DIAGnostic:ROSCillator:CURRent?"))
    return readings


def adc_limits():
    """Each reading's plausible range and tolerance, the queries' as
    their channels'."""
    limits = dict(ADC_LIMITS)
    limits["TEMP?"] = ADC_LIMITS["Temperature"]
    limits["ROSC:CURR?"] = ADC_LIMITS["Oven current"]
    return limits


# The readings every receiver gives.
ADC_REQUIRED = {"Temperature", "5V", "+15V", "-15V", "Oven", "Oven current", "TEMP?", "ROSC:CURR?"}


def check_adc(runs):
    readings = adc_readings(runs)
    limits = adc_limits()
    bad = sorted(
        name
        for name, value in readings.items()
        if value is None or not limits[name][0][0] <= value <= limits[name][0][1]
    )
    missing = sorted(ADC_REQUIRED - readings.keys())
    screen = status_screen(runs["session"]) or ""
    health = re.search(r"HEALTH MONITOR \.+ \[ OK \]", screen) is not None
    ok = not bad and not missing and health
    return judge(readings, ok, f"out of range {bad}, missing {missing}, health {health}")


def within(a, b, limits):
    """Two cores' ADC readings agree within each quantity's tolerance."""
    for name, value in a.items():
        tolerance = limits.get(name)
        other = b.get(name)
        if value is None or other is None or tolerance is None or abs(value - other) > tolerance:
            return False
    return a.keys() == b.keys()


def adc_tolerances():
    return {name: spread for name, (_, spread) in adc_limits().items()}


def bytes_answer(text):
    return [int(word) for word in (text or "").split()] if text else None


def check_dacs(runs):
    session, console = runs["session"], runs["console"]
    values = {
        "EFC:ABS?": session.answer(":DIAGnostic:ROSCillator:EFControl:ABSolute?"),
        "pr_efc": console.answer("pr_efc"),
        "hdac_shadow": bytes_answer(console.answer(f"{runs['receiver'].hdac_shadow} 6 rb")),
    }
    ok = values["EFC:ABS?"] is not None and values["hdac_shadow"] is not None and len(values["hdac_shadow"]) == 6
    return judge(values, ok)


def check_fpga(runs):
    session, console = runs["session"], runs["console"]
    result = session.answer(":DIAGnostic:TEST:RESult?")
    values = {
        "TEST:RES?": result,
        "SYNC:TINT?": session.answer(":SYNChronization:TINTerval?"),
        "calibration": bytes_answer(console.answer(f"{runs['receiver'].calibration} 9 rb")),
    }
    # every power-up test passes with the engine, the counter's and the
    # interpolator's among them (docs/emulator.md, "What the boot shows")
    ok = result is not None and result.split(",")[0] == "+0"
    return judge(values, ok)


def check_indicators(runs):
    changes = runs["session"].indicators()
    window = [(bank, name, lit) for cycle, bank, name, lit in changes if cycle < 60 * CYCLES_PER_SECOND]
    heartbeat = [cycle for cycle, bank, name, _ in changes if name == "heartbeat" and cycle > 0]
    period = None
    if len(heartbeat) > 2:
        period = round((heartbeat[-1] - heartbeat[1]) / (len(heartbeat) - 2) / CYCLES_PER_SECOND, 2)
    values = {
        "changes to 60 s": len(window),
        "sequence": window,
        "heartbeat toggle, s": period,
    }
    lamps = {name for bank, name, lit in window if bank == "lamp" and lit}
    ok = bool(window) and period is not None and "gps_lock" in lamps
    return judge(values, ok)


def panel_displays(run):
    """The display's records after each key step, by key."""
    keyed = {}
    current = None
    for line in run.errors:
        record = SCRIPT_RECORD.match(line)
        if record and record.group(3) == "key":
            current = int(record.group(2))
            keyed[current] = []
        elif record and current is not None:
            if record.group(3) in ("at", "stop"):
                current = None
        display = DISPLAY.match(line)
        if display and current is not None:
            text = (display.group(1), display.group(2))
            if not keyed[current] or keyed[current][-1] != text:
                keyed[current].append(text)
    return [keyed[line] for line in sorted(keyed)]


# The front panel's idle line through warm-up, and the Status key's lamp,
# which blinks over it (docs/emulator.md, "What the boot shows").
PANEL_IDLE = "OCXO\\x20WARM\\x20UP"
STATUS_LAMP = "0x20"


def check_keys(runs):
    """What each key put on the display: its first record that is not
    the idle line's blink, and the texts its window showed.  When the
    display goes back to the idle line, and how often the Status lamp
    blinked, are times."""
    displays = panel_displays(runs["session"])
    values = {}
    for (_, key), shown in zip(PANEL_KEYS, displays):
        first = next((record for record in shown if record[1] != PANEL_IDLE or record[0] != STATUS_LAMP), None)
        texts = sorted({text for _, text in shown if text != PANEL_IDLE})
        values[key] = (first, texts)
    ok = len(displays) == len(PANEL_KEYS) and any("TRACKED" in text for _, texts in values.values() for text in texts)
    return judge(values, ok)


def check_alarm(runs):
    alarm = runs["alarm"]
    outputs = [(bank, lit) for _, bank, name, lit in alarm.indicators() if bank == "output"]
    values = {
        "*STB? after error": alarm.answer("*STB?"),
        ":LED:ALARm?": alarm.answers(":LED:ALARm?"),
        "PF5 (lit = low)": [lit for _, lit in outputs],
    }
    ok = values[":LED:ALARm?"] == ["1", "0"] and values["PF5 (lit = low)"] == [True, False]
    return judge(values, ok)


def check_eeprom(runs):
    first, second = runs["eeprom-1"], runs["eeprom-2"]
    values = {
        "before": first.answers(":GPS:REFerence:ADELay?")[:1],
        "set": first.answers(":GPS:REFerence:ADELay?")[1:],
        "after reboot": second.answer(":GPS:REFerence:ADELay?"),
    }
    after = number(values["after reboot"])
    ok = after is not None and abs(after - float(ANTENNA_DELAY)) < 1e-12 and number(values["before"][0] if values["before"] else None) != after
    return judge(values, ok)


# A log entry: number, date, time, text (z3805a: `Log 045:20070207.00:00:27:  Re-boot`),
# or number, GPS seconds in hex, text (z3801a: `Log 037:H4510849B:  Re-boot`;
# docs/firmware/z3801a-3543.md, "The event log").
LOG_ENTRY = re.compile(r"Log \d+:(?:\d{8}\.(\d\d:\d\d:\d\d)|H([0-9A-F]{8})):\s+(.*)")
SECONDS_PER_DAY = 86400
# What a restart logs: the Z3805A a warm start's `Re-boot`, the 58503A
# `Power on`.
RESTARTS = ("Re-boot", "Power on")
POWER_UP_TIME = "00:00:00"


def log_entries(text):
    """The event log's entries in `text`: each its time of day,
    `hh:mm:ss`, the GPS seconds' too (GPS time starts at a midnight),
    and its text."""
    entries = []
    for time_of_day, gps_seconds, entry in LOG_ENTRY.findall(text):
        if gps_seconds:
            seconds = int(gps_seconds, 16) % SECONDS_PER_DAY
            time_of_day = f"{seconds // 3600:02}:{seconds // 60 % 60:02}:{seconds % 60:02}"
        entries.append((time_of_day, entry))
    return entries


def check_watchdog(runs):
    run = runs["watchdog"]
    entries = log_entries(run.answer(":DIAGnostic:LOG:READ:ALL?") or "")
    restart = next((entry for entry in reversed(entries) if entry[1].strip() in RESTARTS), None)
    values = {
        "resets": len(run.resets()),
        "*IDN? after": run.answer("*IDN?"),
        ":SYST:ERR? after": run.answer(":SYSTem:ERRor?"),
        # its time is the firmware's clock's, a time; logged after the
        # crash, not at power-up, is not
        "restart logged": restart[1].strip() if restart and restart[0] != POWER_UP_TIME else None,
    }
    ok = values["resets"] == 1 and values["*IDN? after"] is not None and values["restart logged"]
    return judge(values, ok)


def check_loop(runs):
    run = runs["loop"]
    states = run.answers(":SYNChronization:STATe?")
    intervals = [number(text) for text in run.answers(":SYNChronization:TINTerval?")]
    efc = [number(text) for text in run.answers(":DIAGnostic:ROSCillator:EFControl:ABSolute?")]
    locked = next((LOOP_EVERY * (index + 1) for index, state in enumerate(states) if state == "LOCK"), None)
    after_lock = [value for value in intervals if value is not None]
    peak = max((abs(value) for value in after_lock), default=None)
    values = {
        "first LOCK, s": locked,
        "last state": states[-1] if states else None,
        "TI first, peak, last": [after_lock[0], peak, after_lock[-1]] if after_lock else None,
        # the pull-in: the TI's last under half its largest
        "TI shrank": bool(after_lock) and abs(after_lock[-1]) < peak / 2,
        "EFC last": efc[-1] if efc else None,
        "calibration": bytes_answer(run.answer(f"{runs['receiver'].calibration} 9 rb")),
    }
    # the EFC on its way to on frequency, from the EEPROM's 749199 or
    # 721944
    ok = (
        locked is not None
        and values["TI shrank"]
        and values["EFC last"] is not None
        and abs(values["EFC last"] - LOOP_EFC_ON_FREQUENCY) < 100
    )
    return judge(values, ok, f"TI {intervals}")


@dataclasses.dataclass(frozen=True)
class Check:
    """A check: its name, the runs it reads, the receivers it applies
    to, how its two cores' values are compared, whether it is slow."""

    name: str
    function: object
    runs: tuple
    applies: object = lambda receiver: True
    compare: object = None
    slow: bool = False


CHECKS = [
    Check("host", check_host, ("session", "console")),
    Check("gps", check_gps, ("session",)),
    Check("sci", check_sci, ("session",), applies=lambda r: r.sci),
    Check("eeprom", check_eeprom, ("eeprom-1", "eeprom-2")),
    Check("time", check_time, ("session",)),
    Check("adc", check_adc, ("session", "console"), compare=lambda a, b: within(a, b, adc_tolerances())),
    Check("dacs", check_dacs, ("session", "console")),
    Check("fpga", check_fpga, ("session", "console")),
    Check("indicators", check_indicators, ("session",)),
    Check("keys", check_keys, ("session",), applies=lambda r: r.panel),
    Check("alarm", check_alarm, ("alarm",), applies=lambda r: r.panel),
    Check("watchdog", check_watchdog, ("watchdog",)),
    Check("loop", check_loop, ("loop",), slow=True),
]


def compare_values(check, a, b):
    """Whether two cores' values agree: equal, or as the check says."""
    if check.compare is not None:
        return check.compare(a, b)
    if check.name == "indicators":
        # the heartbeat's period is a time, compared to a percent
        same = {k: v for k, v in a.items() if k != "heartbeat toggle, s"} == {
            k: v for k, v in b.items() if k != "heartbeat toggle, s"
        }
        pa, pb = a.get("heartbeat toggle, s"), b.get("heartbeat toggle, s")
        return same and pa is not None and pb is not None and abs(pa - pb) <= 0.01 * pa
    if check.name == "sci":
        # the message count moves with when the first one went
        return {k: v for k, v in a.items() if k != "messages"} == {
            k: v for k, v in b.items() if k != "messages"
        } and abs(a["messages"] - b["messages"]) <= 1
    if check.name == "loop":
        keys = ("last state", "TI shrank", "calibration")
        ti = (a["TI first, peak, last"] or [None])[-1], (b["TI first, peak, last"] or [None])[-1]
        efc = a["EFC last"], b["EFC last"]
        locked = a["first LOCK, s"], b["first LOCK, s"]
        return (
            all(a.get(k) == b.get(k) for k in keys)
            and None not in ti + efc + locked
            and abs(locked[0] - locked[1]) <= LOOP_LOCK_APART
            and abs(ti[0] - ti[1]) <= LOOP_TI_APART
            and abs(efc[0] - efc[1]) <= LOOP_EFC_APART
        )
    return a == b


# The runs, by name: the script, how long, extra options.
def plan(receiver, slow):
    runs = {
        "session": (session_script(receiver), SESSION_END + 30, ("--tap", "--indicators")),
        "console": (console_script(receiver), 120, ()),
        "watchdog": (watchdog_script(), 150, ()),
    }
    if receiver.panel:
        runs["alarm"] = (alarm_script(), 120, ("--indicators",))
    if slow:
        runs["loop"] = (loop_script(receiver), LOOP_SECONDS + 120, ("--ocxo-offset", LOOP_OFFSET))
    return runs


def run_receiver(runner, receiver, core, slow, wanted):
    """Every run the wanted checks read, on one core."""
    needed = {name for check in CHECKS if check.name in wanted for name in check.runs}
    runs = {"receiver": receiver}
    for name, (script, stop, options) in plan(receiver, slow).items():
        if name in needed:
            runs[name] = runner.run(receiver, core, name, script, stop, options)
    if {"eeprom-1", "eeprom-2"} & needed:
        first, second = eeprom_scripts()
        eeprom = os.path.join(runner.work, f"{receiver.name}-{core}-eeprom.bin")
        if os.path.exists(eeprom):
            os.remove(eeprom)
        runs["eeprom-1"] = runner.run(receiver, core, "eeprom-1", first, 60, (), eeprom)
        runs["eeprom-2"] = runner.run(receiver, core, "eeprom-2", second, 40, (), eeprom)
    return runs


def failed_runs(runs, check):
    return [
        f"{name} exit {runs[name].status}: {runs[name].errors[-1] if runs[name].errors else ''}"
        for name in check.runs
        if runs[name].status != 0
    ]


def short(value, width):
    text = value if isinstance(value, str) else repr(value)
    return text if len(text) <= width else text[: width - 3] + "..."


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--emu", default=EMULATOR)
    parser.add_argument("--receiver", action="append", choices=sorted(RECEIVERS))
    parser.add_argument("--check", action="append", choices=[check.name for check in CHECKS])
    parser.add_argument("--slow", action="store_true", help="run the slow checks too (loop: some minutes)")
    parser.add_argument("--work", help="where the runs' files go; a temporary directory by default")
    parser.add_argument("--keep", action="store_true", help="keep the temporary directory")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--details", action="store_true", help="print each value whole")
    args = parser.parse_args()
    if not os.access(args.emu, os.X_OK):
        sys.exit(f"{args.emu}: no emulator; build it with --features unicorn")
    receivers = [RECEIVERS[name] for name in (args.receiver or sorted(RECEIVERS, reverse=True))]
    wanted = set(args.check or [check.name for check in CHECKS if args.slow or not check.slow])
    slow = any(check.slow for check in CHECKS if check.name in wanted)
    work = args.work or tempfile.mkdtemp(prefix="peripheral-check-")
    runner = Runner(args.emu, work)
    jobs = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for receiver in receivers:
            for core in CORES:
                jobs[(receiver.name, core)] = pool.submit(run_receiver, runner, receiver, core, slow, wanted)
    failures = 0
    for receiver in receivers:
        print(f"\n{receiver.name} ({receiver.image})")
        print(f"{'check':<11} {'unicorn':<34} {'mc683xx':<34} agree")
        results = {core: jobs[(receiver.name, core)].result() for core in CORES}
        for check in CHECKS:
            if check.name not in wanted or not check.applies(receiver):
                continue
            outcome = {}
            for core in CORES:
                broken = failed_runs(results[core], check)
                outcome[core] = (
                    Result(None, False, "; ".join(broken)) if broken else check.function(results[core])
                )
            unicorn, mc683xx = outcome["unicorn"], outcome["mc683xx"]
            agree = unicorn.ok and mc683xx.ok and compare_values(check, unicorn.value, mc683xx.value)
            failures += not agree
            mark = lambda r: ("pass " if r.ok else "FAIL ") + short(r.value, 28)
            print(f"{check.name:<11} {mark(unicorn):<34} {mark(mc683xx):<34} {'yes' if agree else 'NO'}")
            if args.details or not agree:
                for core in CORES:
                    print(f"    {core}: {outcome[core].value!r}")
                    if outcome[core].detail:
                        print(f"    {core} detail: {outcome[core].detail}")
    print(f"\nruns in {work}")
    if not (args.keep or args.work):
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
