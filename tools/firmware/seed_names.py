"""Seeds an image's names.tsv and modules.tsv from a base image's,
through the matches tools/firmware/image_diff.py finds between the two
images, by the rules the names file's header gives; or checks that they
are what the matches give: the 58503A 3704's and the Z3801A 3543's from the Z3805A 3543B's
(PORTS).

    python3 -I tools/firmware/seed_names.py BASE_FOLDER FOLDER [--write] [--counts PATH]

BASE_FOLDER is the base image's analysis folder,
third_party/firmware/analysis/z3805a-3543b, whose names, modules and
handlers are read, with its export's sidecar in decompiled/ and its
image in images/; FOLDER the seeded image's, whose handlers are read,
with its image, and which PORTS must name with BASE_FOLDER as its base.
Without --write, exits nonzero with the difference when either file is
not what the matches give.  --counts writes the rows carried and not,
by kind, region and status, and the labels dropped for sharing an
address.  Needs m68k-linux-gnu binutils and gcc.
"""

import argparse
import collections
import dataclasses
import difflib
import importlib.util
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
spec = importlib.util.spec_from_file_location('image_diff', ROOT / 'tools/firmware/image_diff.py')
image_diff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image_diff)

# a changed pairing at this similarity or more is carried under its name
THRESHOLD = 0.7
# from here to THRESHOLD it is a lead, named with the port's suffix; below, no row
LEAD = 0.5
# a ROM table placed by its bytes alone and shorter than this is not carried
SMALLEST_BY_BYTES = 8
# the statuses carried as they stand
CARRIED = ('identical', 'relocated')
# the first line of the base file's body, after its header
BODY_START = '# float library'

# The 58503A 3704's port from the Z3805A 3543B (PORT_58503A_3704).
DOCUMENT_58503A = 'docs/firmware/58503a-3704.md'
# the pairings docs/firmware/58503a-3704.md corrects: base function's
# (region, address) to the 58503A address and why; a lead, suffixed and
# unconfirmed, unless CONFIRMED has it
CORRECTIONS_58503A = {
    ('primary', 0x30c60): (0x30b24, "%s: 0x30b24 writes 0x400080, as the Z3805A's does" % DOCUMENT_58503A),
    ('primary', 0x30dea): (0x30c6c, '%s: 0x30c6c is its counterpart' % DOCUMENT_58503A),
    ('primary', 0x4f682): (0x50716, "%s: the message table's @@Bb decoder (0x51178)" % DOCUMENT_58503A),
    ('primary', 0x4f85c): (0x50924, "%s: the message table's @@Bn decoder (0x51178)" % DOCUMENT_58503A),
    ('primary', 0x4f87e): (0x50968, "%s: the message table's @@Cj decoder (0x51178)" % DOCUMENT_58503A),
    ('primary', 0x18d70): (0x18ed6, "%s: the self-test table's GPS entry (0x18814)" % DOCUMENT_58503A),
    ('primary', 0x185e2): (0x18746, "%s: the self-test table's PROC entry (0x1881c)" % DOCUMENT_58503A),
    ('primary', 0x185ec): (0x18750, "%s: the self-test table's RAM entry (0x18824)" % DOCUMENT_58503A),
    ('primary', 0x185f6): (0x1875a, "%s: the self-test table's EEPROM entry (0x1882c)" % DOCUMENT_58503A),
    ('primary', 0x17f2c): (0x18046, "%s: the self-test table's supplies entry, POWER: and a channel's name "
                                    "(0x1815c)" % DOCUMENT_58503A),
    ('primary', 0x18546): (0x186da, "%s: the supplies' test, from the health monitor's channels, which "
                                    "selftest_power and selftest_power_with_lamp call" % DOCUMENT_58503A),
    ('primary', 0x18404): (0x18574, "%s: system_init's call (0x12f0a); it clears selftest.lamp_testing "
                                    "too, then falls into the Z3805A's three stores" % DOCUMENT_58503A),
    ('primary', 0x2458c): (0x238b0, "%s: pps_monitors_reset's call; it clears 0x101ffe too, then falls "
                                    "into the Z3805A's stores" % DOCUMENT_58503A),
    ('primary', 0x2470c): (0x23a3c, "%s: pps_monitors_reset's call; it clears 0x101ffc too, then falls "
                                    "into the Z3805A's stores" % DOCUMENT_58503A),
    ('primary', 0x23c2a): (0x23dc2, "%s: system_init's calls (0x12e9c, 0x12eca); the monitors' fields "
                                    "cleared through 0x21cfc" % DOCUMENT_58503A),
    ('primary', 0x12f2c): (0x130da, "%s: the SCPI task's call for :SYSTem:LANGuage (0x294b4); it deletes "
                                    "fpnl and blanks the display before trap #11" % DOCUMENT_58503A),
    ('primary', 0x13fa6): (0x140d0, "%s: timecode_update's call (0x13ffa); it sends through the routine "
                                    "tcode_request stores (0x103a86)" % DOCUMENT_58503A),
    ('primary', 0x29ada): (0x29246, "%s: the SCPI handlers' 154 calls with their call's input" % DOCUMENT_58503A),
    ('primary', 0x29b82): (0x28db4, "%s: the port layer's call for port 2 (0x291ea); it returns the "
                                    "console port's object, 0x29090" % DOCUMENT_58503A),
    ('primary', 0x2a04a): (0x294cc, "%s: scpi_task_create's task, the port layer's SCPI task on port 2 "
                                    "(0x293c2)" % DOCUMENT_58503A),
    ('primary', 0x45af0): (0x45cbc, "%s: scpi_action_temporary_hold's action (0x413de)" % DOCUMENT_58503A),
    ('primary', 0x4be04): (0x4c80e, "%s: gpsm_task's script lookup by request code, codes 0x52 to 0x65"
                           % DOCUMENT_58503A),
    ('installer', 0x10746a): (0x10739c, "%s: the installer's port layer's call for port 2 "
                                        "(installer::0x107762)" % DOCUMENT_58503A),
    ('installer', 0x107902): (0x1079ec, "%s: the installer's SCPI task, the port layer's on port 2 "
                                        "(installer::0x10791c)" % DOCUMENT_58503A),
}
# the corrections the image settles, carried under the base's name with
# its prototype and convention: the Oncore decoders, which image_diff
# pairs by their shape, a call of oncore_decode_fields, where the
# 58503A's message table names each by its message; selftest_gps_with_lamp,
# which image_diff pairs by its bytes with the power test's (0x18efa),
# the same but for the word it tests, where the 58503A's self-test table
# names it, and the table's PROC, RAM and EEPROM results and supplies
# entry likewise; the routines image_diff pairs at another's last
# instructions or finds missing, where the 58503A's callers name them
CONFIRMED_58503A = {('primary', 0x4f682), ('primary', 0x4f85c), ('primary', 0x4f87e), ('primary', 0x18d70),
             ('primary', 0x185e2), ('primary', 0x185ec), ('primary', 0x185f6), ('primary', 0x17f2c),
             ('primary', 0x18546), ('primary', 0x18404), ('primary', 0x2458c), ('primary', 0x2470c),
             ('primary', 0x23c2a), ('primary', 0x12f2c), ('primary', 0x13fa6), ('primary', 0x29ada),
             ('primary', 0x29b82), ('primary', 0x2a04a), ('primary', 0x45af0), ('primary', 0x4be04),
             ('installer', 0x10746a), ('installer', 0x107902)}
# the correction whose function image_diff pairs elsewhere, as changed
CORRECTED_FROM_CHANGED_58503A = ('primary', 0x30c60)

# functions whose bytes match at more than one 58503A address, pinned
# to one the matches do not pick: base function's (region, address) to
# the 58503A address image_diff gives, which is asserted, the address
# pinned and why
PINNED_58503A = {
    ('primary', 0x2f53a): (
        0x2f1fc, 0x2f1b8,
        'its 2 bytes, an rts, match more than one; pinned to 0x2f1b8, which status_report and '
        "status_happening call (0x2bcec, 0x2f2fc), as the Z3805A's call 0x2f53a"),
    ('primary', 0x45b6e): (
        0x45cde, 0x45d42,
        'its 2 bytes, an rts, match more than one; pinned to 0x45d42, the done hook of its descriptor, '
        "nv_param_field_b8 (0x34044 + 0x1a), as the Z3805A's descriptor holds 0x45b6e"),
    ('primary', 0x42b18): (
        0x42bfa, 0x47fa6,
        "its 2 bytes, an rts, match more than one; pinned to 0x47fa6, which output_state_done calls, as "
        "the Z3805A's calls 0x42b18"),
    ('primary', 0x42b1a): (
        0x42c46, 0x47fa8,
        "its 2 bytes, an rts, match more than one; pinned to 0x47fa8, which pulse_settings_done calls, as "
        "the Z3805A's calls 0x42b1a"),
    ('primary', 0x29e34): (
        0x290b0, 0x290ac,
        "its 4 bytes match more than one; pinned to 0x290ac, which scpi_interpret calls three times, as the "
        "Z3805A's calls 0x29e34"),
    ('primary', 0x29e38): (
        0x290ac, 0x290b0,
        "its 4 bytes match more than one; pinned to 0x290b0, which scpi_interpret calls once, at 0x28848, "
        "where the Z3805A's calls 0x29e38"),
    ('installer', 0x1076ec): (
        0x107678, 0x107674,
        "its 4 bytes match more than one; pinned to installer::0x107674, which installer_scpi_interpret "
        "calls three times, as scpi_interpret calls scpi_message_done"),
    ('installer', 0x1076f0): (
        0x107742, 0x107678,
        "its 4 bytes match more than one; pinned to installer::0x107678, which installer_scpi_interpret "
        "calls once, as scpi_interpret calls scpi_input_closed"),
    ('primary', 0x1d570): (
        0x1d4d0, 0x1d3d0,
        'its 2 bytes, an rts, match more than one; pinned to 0x1d3d0, which duart_init and '
        "duartb_reconfigure store as duartb.echo (0x1dab8, 0x1dbb8), as the Z3805A's store 0x1d570"),
}
# functions whose match is another routine's last instructions, with no
# counterpart in the 58503A, which get no row: base function's (region,
# address) to the 58503A address image_diff gives, which is asserted;
# the header gives why
DROPPED_FUNCTIONS_58503A = {
    # qspi_fpga_byte_write's last instruction; the 58503A has no SCI
    ('primary', 0x1f6c8): 0x1f328,
}

# labels whose references inside them outvote those at their start,
# placed by their start's all the same: name to the 58503A address
# expected and the evidence the note gives after its matching, whose
# fields are filled from the votes: exact, the references at its start;
# voters, the functions they are in; inside, the references inside it;
# offsets[N], how many of those put a field N bytes from the Z3805A's
# offset
PLACED_BY_START_58503A = {
    'nv_settings': (
        0x10219c,
        "by the {exact} references at its start, {voters}'s; the 58503A's record grew by 6 bytes: of the "
        "{inside} references inside it {offsets[6]} put its fields 6 bytes further in than the Z3805A's "
        "offsets, {offsets[8]} 8 bytes, {offsets[5]} 5 bytes and {offsets[0]} where they are, and "
        "duartb_settings follows 0xd0 bytes on, where the Z3805A's 0xc5 bytes and sci_settings come first"),
    'duartb': (
        0x103412,
        "by the {exact} references at its start, at the even address after duartb_ring's end; "
        "loopback_send_next follows 0x56 bytes on in both: its fields are rearranged and its span kept; of "
        "the {inside} references inside it {offsets[-2]} put its fields 2 bytes lower than the Z3805A's "
        "offsets, {offsets[0]} where they are and the rest at other offsets"),
    'installer_duartb': (
        0x10af0c,
        "by the {exact} references at its start, at the even address after installer_duartb_ring's end; "
        "installer_task_spoo follows 0x5e bytes on in both: its fields are rearranged and its span kept, as "
        "duartb's are; of the {inside} references inside it {offsets[-2]} put them 2 bytes lower, "
        "{offsets[0]} where they are"),
}
LAYOUT_58503A = "the fields' offsets here are still the Z3805A's, which the types wave corrects"
# functions a bare import's analysis finds called that image_diff
# neither matches nor finds new: listed with the new functions, their
# extent and summary as image_diff gives a new one's, and asserted
# listed nowhere else, so that a change in the matches revisits this
CALLED_UNLISTED_58503A = (
    ('primary', 0x21c88), ('primary', 0x30da4), ('primary', 0x43a7c), ('primary', 0x4a1fa),
    ('primary', 0x52732), ('primary', 0x5285a), ('primary', 0x52886), ('installer', 0x103a20),
)
# how image_diff's summary says they were found
CALLED_UNLISTED_HOW = "called, by Ghidra's analysis"
# the comment above an image's installer records' row, by its model
INSTALLER_RECORDS = "# the %s's installer records"
# a new function's row: its kind, which SmartClockSetup.java makes an
# unnamed function at, and its source
ENTRY = 'entry'
ENTRY_SOURCE = 'image_diff'
# an installer copy's note: its primary's name, where the copy is
# relative to it, the Z3805A's, and what it says of the bytes
INSTALLER_COPY = re.compile(
    r"the installer's copy of (\w+), at 0x([0-9a-f]+) plus 0x([0-9a-f]+)(?: in this image)?(,? byte for byte(?! but))?")
# labels the rules place that are dropped all the same: name to the
# 58503A address the rules give, which the header explains
DROPPED_58503A = {'installer_duarta_ring': 0x10af1a}


# the header's paragraphs on the 58503A's exceptions, which Port's
# fields name
CORRECTIONS_TEXT_58503A = "#   docs/firmware/58503a-3704.md corrects one pairing: 0x30b24 is\n#   save_calibration_block's counterpart, 0x30c6c save_parameter_record's.\n#   It corrects four more, which the image settles, and carries them\n#   under their names: the Oncore decoders oncore_visibility_message,\n#   oncore_traim_message and oncore_receiver_id_message, which image_diff\n#   pairs by their shape, are 0x50716, 0x50924 and 0x50968, the message\n#   table's @@Bb, @@Bn and @@Cj decoders (0x51178); and\n#   selftest_gps_with_lamp, which image_diff pairs by its bytes with the\n#   power test's, 0x18efa, is 0x18ed6, the self-test table's GPS entry\n#   (0x18814), which tests a long.  The table also names\n#   selftest_cpu_result, selftest_ram_result, selftest_eeprom_result and\n#   selftest_power, 0x18746, 0x18750, 0x1875a and 0x18046, where\n#   image_diff pairs the first with the EEPROM's and finds the others\n#   missing; supplies_test is 0x186da, which both call.  Routines\n#   image_diff pairs at another's last instructions or finds missing\n#   are carried where the 58503A's callers name them: selftest_init,\n#   gps_pps_monitor_reset and internal_pps_monitor_reset at 0x18574,\n#   0x238b0 and 0x23a3c, one store ahead of the Z3805A's (image_diff\n#   takes the stores after it); enter_installer at 0x130da, which\n#   deletes fpnl first (image_diff takes its trap #11); return_zero at\n#   0x29246, the handlers' (image_diff takes an uncalled tail at\n#   0x28d98); scpi_console_port at 0x28db4 and the installer's at\n#   installer::0x10739c, the port layer's for port 2 (image_diff takes\n#   the tails of the session starts); monitors_reset at 0x23dc2,\n#   timecode_schedule at 0x140d0, scpi_task at 0x294cc and the\n#   installer's at installer::0x1079ec, temporary_hold_set at 0x45cbc\n#   and gps_script_lookup at 0x4c80e, each by its caller or table.\n#   sci_echo_none, an rts image_diff finds at qspi_fpga_byte_write's end\n#   (0x1f328), has no row: the 58503A has no SCI.\n"
LABELS_TEXT_58503A = ", but for nv_settings, which grew, and duartb and\n#   installer_duartb, whose fields are rearranged within the same span:\n#   they are placed by their start's references, their notes giving\n#   the evidence.\n#   installer_duarta_ring is dropped too, though placed by two\n#   references, both from one changed piece: at 0x10af1a it would\n#   overlap installer_duartb and run past installer_task_spoo.  The\n#   58503A's installer keeps no channel A ring (installer::0x103a16).\n#   The functions whose few bytes match at more than one address are\n#   pinned where the 58503A's callers name them: status_event_hook, a\n#   bare rts, at 0x2f1b8, which status_report and status_happening call,\n#   where image_diff takes 0x2f1fc; duartb_echo_none at 0x1d3d0, which\n#   duart_init and duartb_reconfigure store as duartb.echo, for\n#   0x1d4d0; nv_param_field_b8_done at 0x45d42, its descriptor's done\n#   hook, for 0x45cde; output_state_program and pulse_program at\n#   0x47fa6 and 0x47fa8, which output_state_done and\n#   pulse_settings_done call, for 0x42bfa and 0x42c46; and\n#   scpi_message_done and scpi_input_closed, which image_diff swaps, at\n#   0x290ac and 0x290b0, by scpi_interpret's calls, and the installer's\n#   at installer::0x107674 and 0x107678, for 0x107678 and 0x107742.\n"
TABLES_TEXT_58503A = " (the health monitor's channel table, 0x2238a,\n#   among them)"
HAND_ROWS_TEXT_58503A = "  The types\n#   wave's hand rows place the RAM\n#   labels the rules leave unplaced where the 58503A's code shows them,\n#   type tables the 58503A sizes otherwise, and give changed functions\n#   the locals whose frames the 58503A's code bears out, each note\n#   saying where."


def place_key(fields):
    """Where a row stands, which a hand row takes: its address and kind,
    and for a local or call row its name, since a function may have
    several."""
    return tuple(fields[:3]) if fields[1:2] in (['local'], ['call']) else tuple(fields[:2])


HEADER = """\
# Names for the %(image)s image, for SmartClockSetup.java, seeded from
# the %(base)s's (third_party/firmware/analysis/%(base_folder)s/names.tsv) by the
# matches tools/firmware/image_diff.py finds between the two images
# (docs/firmware/%(document_name)s; PLAN.md, "a second image is ported by
# copying the first's work").  Each carried row keeps the %(base_model)s's name,
# source, note, prototype and convention, its note ending in how it was
# matched.  A note's addresses, and an address its source gives, are the
# %(base_model)s's unless the note says otherwise.  tools/firmware/seed_names.py
# writes this file and modules.tsv by these rules, make seed-names;
# make check-image-diff fails while they are not what the matches give,
# so a rule or an exception is changed there, not a row here.
#
# - A function matched identical or relocated (the same bytes, or the
#   same with every outward address masked), or inside one such, is
#   carried; so are the call rows inside one, and its local rows, the
#   %(base_model)s's stack offsets kept (masking leaves frame offsets as they
#   are).  An installer copy's note gives its primary's %(model)s address,
#   where its primary is carried, and its offset from it, "in this
#   image"; a copy said to be byte for byte is checked so.
# - A function matched changed at a similarity (image_diff's difflib
#   ratio of the instructions, numbers masked) of %(threshold).2f or more is
#   carried, noted "changed", for the next wave to review.  From %(lead).2f
#   to %(threshold).2f the %(base_model)s's name suffixed %(suffix)s marks the pairing as a
#   lead, without its prototype or convention; below %(lead).2f there is no
#   row, and the pairing is listed under "Leads" at the end.
%(corrections)s# - A ROM table (a typed label in the image) matched identical or
#   relocated is carried, but not one placed by its bytes alone and
#   under %(smallest)d bytes long.  A RAM or I/O label is placed by the matched
#   code's references: an instruction aligned between a %(base_model)s function
#   and its counterpart (identical, relocated, or changed at %(threshold).2f or
#   more) that names an address in the label, at its start if any does,
#   gives the %(model)s's address; it is placed where at least two such
#   references agree, or one in an identical function, and more agree
#   than on any other address.  Labels placed at one address are all
#   dropped, and so is one whose references inside it outvote those at
#   its start%(labels)s# - Missing functions, and tables or labels not placed so, have no row;
#   the functions image_diff finds new are listed, unnamed, at the end,
#   with the %(called)d a bare import's analysis finds called that image_diff
#   neither matches nor finds.  image_diff rejects a start that is text,
#   a table of pointers%(tables)s, inside a ROM table it places or inside another
#   function's instructions, so that none of those is listed.%(not_code)s
# - A row whose note says "%(hand)s" is kept
#   as it is, in place of the row the matches give at its address and
#   kind (and name, for a local or call row), or, with none there, after
#   the carried rows; the rest are the seeding's, which make
#   check-image-diff checks.  A hand row may take a name the matches give
#   another place only where a hand row takes that place too.%(hand_rows)s
#
"""
MODULES_HEADER = """\
# The %(image)s firmware's C functions by module, seeded from the
# %(base)s's (third_party/firmware/analysis/%(base_folder)s/modules.tsv): each
# %(base_model)s row re-keyed to its function's %(model)s address through
# tools/firmware/image_diff.py's matches, as names.tsv is (identical,
# relocated, or changed at a similarity of %(threshold).2f or more under its own
# name; from %(lead).2f a lead, named as names.tsv names it, with the suffix
# %(suffix)s where names.tsv has a row for it).  A %(base_model)s function missing
# from the %(model)s, or paired below %(lead).2f, has no row, and the %(model)s's
# new functions have none yet.  docs/firmware/modules.md gives the
# layers, what each module holds and the calls that cross layers the
# wrong way; docs/firmware/%(document_name)s what differs.
# tools/firmware/seed_names.py writes this file, as it does names.tsv.
#
# Tab-separated: the function's address, its name, its module path
# (LAYER/MODULE or LAYER/MODULE/PART), and a reason when the name does not
# make the module plain.  The layers, lowest first, are platform, devices,
# services and applications; a lower layer does not call a higher one but
# for the calls modules.md lists.
#
# The assembly routines, the pForth kernel and the installer's copies
# have no row.  Rows go in the %(base_model)s's order: by module, layer by
# layer, and within a module by subject.  A row whose reason ends
# "%(hand)s" is kept as it is, after
# the seeded rows, for a function the seeding gives no row; it may not
# take an address the seeding gives.
"""
MODULES_HAND_HEADER = """\
#
# The %(model)s's own functions, which no %(base_model)s row re-keys: rows written
# by hand, by module as above.
"""

# Each image by its folder's name: its model and revision, as a
# document names it.
IMAGE_NAMES = {'z3805a-3543b': ('Z3805A', '3543B'), '58503a-3704': ('58503A', '3704'),
               'z3801a-3543': ('Z3801A', '3543')}


@dataclasses.dataclass(frozen=True)
class Port:
    """An image seeded from a base image's names: its folder's and the
    base's names; the document that describes the port; the corrections
    to image_diff's pairings that document makes, base function's (region,
    address) to the image's address and why, each a lead, suffixed and
    unconfirmed, unless confirmed has it, and corrected_from_changed the
    one whose function image_diff pairs elsewhere, as changed; pinned,
    base function's (region, address) to the address image_diff gives,
    which is asserted, the address pinned and why, for functions whose
    bytes match at more than one address; dropped_functions, base
    function's (region, address) to the address image_diff gives, which
    is asserted, for a match that is another routine's last instructions
    with no counterpart; placed_by_start, a label whose references
    inside it outvote those at its start, placed by its start's all the
    same: its name to the address expected and the evidence its note
    gives (Seeding.place_labels' fields); layout, what a label placed so
    says of its fields; called_unlisted, the functions a bare import's
    analysis finds called that image_diff neither matches nor finds
    new, listed with the new functions and asserted listed nowhere
    else; not_code, the starts image_diff finds new that are data,
    (region, address), asserted listed new and left out; dropped, labels the rules place that are dropped all the
    same, name to the address the rules give; and the header's
    paragraphs that set out those exceptions: corrections, after the
    rule for changed functions, labels, after the rule for labels from
    "its start", tables, the tables of pointers image_diff rejects,
    hand_rows, what the hand rows do, and not_code, after the rule for
    starts image_diff rejects, the not_code starts."""

    folder: str
    base_folder: str
    corrections: dict = dataclasses.field(default_factory=dict)
    confirmed: frozenset = frozenset()
    corrected_from_changed: tuple | None = None
    pinned: dict = dataclasses.field(default_factory=dict)
    dropped_functions: dict = dataclasses.field(default_factory=dict)
    placed_by_start: dict = dataclasses.field(default_factory=dict)
    layout: str = ''
    called_unlisted: tuple = ()
    not_code: frozenset = frozenset()
    dropped: dict = dataclasses.field(default_factory=dict)
    corrections_text: str = ''
    labels_text: str = '.\n'
    tables_text: str = ''
    hand_rows_text: str = ''
    not_code_text: str = ''

    @property
    def image(self):
        return ' '.join(IMAGE_NAMES[self.folder])

    @property
    def model(self):
        return IMAGE_NAMES[self.folder][0]

    @property
    def base(self):
        return ' '.join(IMAGE_NAMES[self.base_folder])

    @property
    def base_model(self):
        return IMAGE_NAMES[self.base_folder][0]

    @property
    def suffix(self):
        """A lead's name: the base's name with this suffix."""
        return '_' + self.model.lower()

    @property
    def origin(self):
        """How a carried row's note says where it came from."""
        return 'from the %s, by image_diff' % self.base

    @property
    def document(self):
        return 'docs/firmware/%s.md' % self.folder

    @property
    def hand(self):
        """The mark a row written by hand carries in its note, in place
        of origin: the seeding keeps it as it is, in place of the row the
        matches give at its address, or else among the hand rows after
        the carried ones, so that check-seed-names checks only the rest."""
        return 'written by hand for the %s' % self.image

    def text(self, template):
        """A header, its fields this port's."""
        return template % {
            'threshold': THRESHOLD, 'lead': LEAD, 'suffix': self.suffix, 'smallest': SMALLEST_BY_BYTES,
            'called': len(self.called_unlisted), 'hand': self.hand, 'image': self.image, 'model': self.model,
            'base': self.base, 'base_model': self.base_model, 'base_folder': self.base_folder,
            'document_name': os.path.basename(self.document), 'corrections': self.corrections_text,
            'labels': self.labels_text, 'tables': self.tables_text, 'hand_rows': self.hand_rows_text,
            'not_code': self.not_code_text}


PORT_58503A_3704 = Port(
    folder='58503a-3704', base_folder='z3805a-3543b', corrections=CORRECTIONS_58503A,
    confirmed=frozenset(CONFIRMED_58503A), corrected_from_changed=CORRECTED_FROM_CHANGED_58503A,
    pinned=PINNED_58503A, dropped_functions=DROPPED_FUNCTIONS_58503A, placed_by_start=PLACED_BY_START_58503A,
    layout=LAYOUT_58503A, called_unlisted=CALLED_UNLISTED_58503A, dropped=DROPPED_58503A,
    corrections_text=CORRECTIONS_TEXT_58503A, labels_text=LABELS_TEXT_58503A, tables_text=TABLES_TEXT_58503A,
    hand_rows_text=HAND_ROWS_TEXT_58503A)
# The Z3801A 3543's port from the Z3805A 3543B, its sibling
# (docs/firmware/z3801a-3543.md, "The base").
# copyright_text's bytes are also in the installer's records, at 0x14b2,
# where image_diff places it; the Z3801A's own is at 0x12ef3, after its
# shorter revision_text, which a hand row places.
DROPPED_Z3801A = {'copyright_text': 0x14b2}
LABELS_TEXT_Z3801A = '''.
#   copyright_text is dropped too, though placed by its bytes: at
#   0x14b2 they are inside the installer's records, which hold the same
#   text; a hand row places it at 0x12ef3, after revision_text, where
#   the Z3805A's follows its own.
'''
PORT_Z3801A_3543 = Port(folder='z3801a-3543', base_folder='z3805a-3543b', dropped=DROPPED_Z3801A,
                        labels_text=LABELS_TEXT_Z3801A)
# Each seeded image's port, by its folder's name.
PORTS = {port.folder: port for port in (PORT_58503A_3704, PORT_Z3801A_3543)}


class Offsets(collections.Counter):
    """References by how far they put a field from the base's offset,
    indexed from a format string's offsets[N], whose N is text."""

    def __getitem__(self, key):
        return super().__getitem__(int(key))


def show(region, address):
    return ('installer::0x%x' if region == 'installer' else '0x%x') % address


def note_with(note, text):
    return (note + ' ' if note else '') + '(' + text + ')'


def label_sizes(base_folder, types):
    """Each label type's size, as m68k gcc lays it out from the base's
    smartclock.h."""
    types = sorted(types)
    with tempfile.TemporaryDirectory() as scratch:
        source = os.path.join(scratch, 'sizes.c')
        with open(source, 'w') as c:
            c.write('#include "third_party/firmware/rewritten/%s/types/smartclock.h"\n'
                    '#include "third_party/firmware/include/mc68331.h"\n'
                    '#include "third_party/firmware/include/mc68681.h"\n' % os.path.basename(base_folder))
            for n, t in enumerate(types):
                c.write('const unsigned long size_%d = sizeof(%s);\n' % (n, t))
        asm = subprocess.run(['m68k-linux-gnu-gcc', '-mcpu=cpu32', '-S', '-w', '-I', str(ROOT), '-o', '-', source],
                             capture_output=True, text=True, check=True).stdout
    sizes, current = {}, None
    for line in asm.splitlines():
        m = re.match(r'^size_(\d+):', line)
        if m:
            current = int(m.group(1))
            continue
        m = re.match(r'^\s+\.long\s+(\d+)', line)
        if m and current is not None:
            sizes[types[current]] = int(m.group(1))
            current = None
    return sizes


class Seeding:
    """The matches of both regions and what the rules make of them:
    code, the base functions and pieces by (region, address); labels,
    the base's typed labels likewise; label_target, each carried label's
    address in the image, note text and count key."""

    def __init__(self, base_folder, folder):
        self.base_folder, self.folder = base_folder, folder
        base_name = os.path.basename(base_folder)
        self.port = PORTS.get(os.path.basename(folder))
        if self.port is None or self.port.base_folder != base_name:
            sys.exit('%s: no port from %s in seed_names.PORTS' % (folder, base_name))
        base_image = (FIRMWARE / 'images' / (base_name + '.bin')).read_bytes()
        other_image = (FIRMWARE / 'images' / (os.path.basename(folder) + '.bin')).read_bytes()
        self.other_image = other_image
        image_diff.ADDRESS_RANGES[:] = [(image_diff.LOWEST_ADDRESS, max(len(base_image), len(other_image))),
                                        (image_diff.RAM, image_diff.RAM + image_diff.RAM_SIZE)]
        items, starts = image_diff.load_base(str(FIRMWARE / 'decompiled' / base_name / (base_name + '.conventions.jsonl')),
                                             os.path.join(base_folder, 'names.tsv'),
                                             os.path.join(base_folder, 'modules.tsv'))
        ours, theirs = image_diff.installer_records(base_image), image_diff.installer_records(other_image)
        regions = [('primary', image_diff.Memory(base_image, 0, ours[1]), image_diff.Memory(other_image, 0, theirs[1]),
                    image_diff.handler_pairs([os.path.join(base_folder, 'handlers.txt'),
                                              os.path.join(folder, 'handlers.txt')])),
                   ('installer', image_diff.Memory(image_diff.installer_image(ours[0]), image_diff.RAM),
                    image_diff.Memory(image_diff.installer_image(theirs[0]), image_diff.RAM), ([], {}))]
        self.matchers, self.results = {}, {}
        for region, base, other, pairs in regions:
            matcher = image_diff.Matcher(base, other, items[region], starts[region], pairs)
            self.results[region] = matcher.run()
            self.matchers[region] = matcher
        # where each image's installer overlay ends: plain RAM from there
        self.overlay_ends = (max(a + len(b) for a, b in ours[0]), max(a + len(b) for a, b in theirs[0]))
        self.code, self.labels = {}, {}
        self.by_region = collections.defaultdict(list)
        for region, found in self.results.items():
            for item in found:
                if item.kind in ('function', 'piece') and item.address >= 0:
                    self.code[region, item.address] = item
                    self.by_region[region].append(item)
                elif item.kind in ('label', 'global'):
                    self.labels[region, item.address] = item
        self.lines = open(os.path.join(base_folder, 'names.tsv')).read().split('\n')
        self.body_start = next(i for i, l in enumerate(self.lines) if l.startswith(BODY_START))
        self.sizes = label_sizes(base_folder, {
            row.split('\t')[5] for row in self.lines if '\tlabel\t' in row and len(row.split('\t')) > 5})
        self.notes = []
        self.place_labels()

    def containing(self, region, address):
        for item in self.by_region[region]:
            if item.address < address < item.address + item.size:
                return item
        return None

    def mapped_code(self, region, address):
        """The base function or piece at an address, or the one an
        identical or relocated one contains: (item, other address, outer)."""
        item = self.code.get((region, address))
        if item is not None:
            return item, item.other, None
        outer = self.containing(region, address)
        if outer is not None and outer.status in CARRIED and outer.other is not None:
            return outer, outer.other + address - outer.address, outer
        return None, None, None

    def is_table(self, region, address):
        matcher = self.matchers[region]
        size = self.sizes.get(self.labels[region, address].type) or 1
        return matcher.base.contains(address) and (
            matcher.base.base == 0 or any(matcher.base.bytes(address, address + size)))

    @staticmethod
    def counted(item):
        return item.status in CARRIED or (item.status == 'changed' and item.similarity >= THRESHOLD)

    def votes(self, region, address, label):
        """The counted references at a RAM label's start and inside it:
        start in the image implied to the set of (function, instruction,
        field, status) giving it, for each."""
        matcher = self.matchers[region]
        size = self.sizes.get(label.type) or 1
        extent = max(min(address + size, matcher.next_start(address)), address + 1)
        exact, interior = collections.defaultdict(set), collections.defaultdict(set)
        # the installer's code votes for plain RAM above both overlays: its
        # own data there, which the primary's labels name
        sources = [(region, v) for v in matcher.references]
        if region == 'primary':
            sources += [('installer', v) for v in self.matchers['installer'].references
                        if v[0] >= self.overlay_ends[0] and v[1] >= self.overlay_ends[1]]
        for voter, (target, other, function, instruction, index) in sources:
            if not address <= target < extent:
                continue
            item = self.code.get((voter, function))
            if item is None or not self.counted(item):
                continue
            votes = exact if target == address else interior
            votes[other - (target - address)].add((item.name, instruction, index, item.status))
        return exact, interior

    def place_labels(self):
        """Every carried label's address in the image, the collisions dropped."""
        placements = {}
        for (region, address), label in sorted(self.labels.items()):
            if self.is_table(region, address):
                continue
            exact, interior = self.votes(region, address, label)
            votes, how = (exact, 'exact') if exact else (interior, 'inside')
            if not votes:
                continue
            # the candidates are where its start is named, if anywhere; each
            # is supported by every reference that agrees with it
            support_of = {c: exact.get(c, set()) | interior.get(c, set()) for c in votes}
            ranked = sorted(support_of.items(), key=lambda kv: -len(kv[1]))
            top, support = ranked[0]
            identical = any(s == 'identical' for *_, s in support)
            second = len(ranked[1][1]) if len(ranked) > 1 else 0
            accepted = (len(support) >= 2 or identical) and len(support) > second
            # the references inside it outvoting its start's
            inside_top = max(interior.items(), key=lambda kv: len(kv[1]), default=(None, set()))
            outvoted = how == 'exact' and inside_top[0] not in (None, top) and len(inside_top[1]) > len(support)
            exception = self.port.placed_by_start.get(label.name)
            evidence = None
            if exception is not None:
                if not (outvoted and accepted and top == exception[0]):
                    sys.exit('%s: no longer outvoted at its start 0x%x, or placed elsewhere; revisit the port\'s placed_by_start'
                             % (label.name, exception[0]))
                offsets = Offsets()
                for implied, references in interior.items():
                    offsets[implied - top] += len(references)
                evidence = exception[1].format(exact=len(exact[top]), inside=offsets.total(), offsets=offsets,
                                               voters=' and '.join(sorted({r[0] for r in exact[top]})))
            elif outvoted:
                accepted = False
            if accepted:
                placements[region, address] = (top, how, len(support), identical, evidence)
        self.label_target = {}
        for (region, address), label in self.labels.items():
            if (region, address) in placements:
                top, how, support, identical, evidence = placements[region, address]
                moved = 'same address' if top == address else 'moved %+#x' % (top - address)
                if evidence is not None:
                    text = '%s: %s %s; %s' % (self.port.origin, moved, evidence, self.port.layout)
                else:
                    text = '%s: %s by %d reference%s %s%s' % (
                        self.port.origin, moved, support, '' if support == 1 else 's',
                        'at its start' if how == 'exact' else 'inside it',
                        ', one in an identical function' if identical else '')
                self.label_target[region, address] = (top, text, 'ram ' + ('same' if top == address else 'moved'))
            elif label.kind == 'label' and label.status in CARRIED and self.is_table(region, address):
                if label.method == 'bytes' and label.size < SMALLEST_BY_BYTES:
                    continue
                text = '%s: %s table, placed by %s' % (self.port.origin, label.status, label.method)
                self.label_target[region, address] = (label.other, text, 'table ' + label.status)
        by_target = collections.defaultdict(list)
        for key, (other, _, _) in self.label_target.items():
            by_target[key[0], other].append(key)
        for (region, other), keys in by_target.items():
            if len(keys) > 1:
                self.notes.append('labels at one address %s: %s' % (
                    show(region, other), ', '.join(self.labels[k].name for k in keys)))
                for k in keys:
                    del self.label_target[k]
        for key in list(self.label_target):
            name = self.labels[key].name
            if name in self.port.dropped:
                if self.label_target[key][0] != self.port.dropped[name]:
                    sys.exit('%s: placed at 0x%x, not 0x%x; revisit the port\'s dropped'
                             % (name, self.label_target[key][0], self.port.dropped[name]))
                del self.label_target[key]

    def names(self):
        """The names file's text, and the carried functions by base
        (region, address) to (status, the image's address) for the modules."""
        out_rows, pending_comments = [], []
        counts, dropped = collections.Counter(), collections.Counter()
        seen_addresses, seen_names, carried = {}, {}, {}
        weak_leads = []

        def emit(fields, counter_key):
            while fields and fields[-1] == '':
                fields.pop()
            if fields[1] in ('function', 'label'):
                key = fields[0]
                if key in seen_addresses:
                    if seen_addresses[key][2] == fields[2]:
                        dropped['exact duplicate'] += 1
                    else:
                        self.notes.append('address conflict %s: %s / %s' % (key, seen_addresses[key][2], fields[2]))
                        dropped['address conflict'] += 1
                    return
                if fields[2] in seen_names:
                    self.notes.append('name conflict %s at %s and %s' % (fields[2], seen_names[fields[2]], key))
                    dropped['name conflict'] += 1
                    return
                seen_addresses[key] = fields
                seen_names[fields[2]] = key
            out_rows.extend(pending_comments)
            pending_comments.clear()
            out_rows.append('\t'.join(fields))
            counts[counter_key] += 1

        corrected_others = {(r, o) for (r, _), (o, _) in self.port.corrections.items()}
        last_row = True
        for line in self.lines[self.body_start:]:
            if not line.strip():
                continue
            if line.startswith('#'):
                # a new block: comments no carried row followed are dropped
                if last_row:
                    pending_comments.clear()
                pending_comments.append(line)
                last_row = False
                continue
            last_row = True
            f = (line.split('\t') + [''] * 7)[:7]
            address, kind, name, source, note, prototype, convention = f
            region, value = image_diff.parse_address(address)
            if kind == 'function':
                item, other, outer = self.mapped_code(region, value)
                if (region, value) in self.port.corrections and (region, value) in self.port.confirmed:
                    other, why = self.port.corrections[region, value]
                    emit([show(region, other), kind, name, source,
                          note_with(note, '%s, paired by %s: %s' % (self.port.origin, self.port.document, why)), prototype, convention],
                         ('function', region, 'corrected'))
                    carried[region, value] = ('corrected', other)
                    continue
                if (region, value) in self.port.corrections:
                    other, why = self.port.corrections[region, value]
                    text = '%s, paired by %s: %s' % (self.port.origin, self.port.document, why)
                    if (region, value) == self.port.corrected_from_changed:
                        text = '%s: changed at %.3f with 0x%x, which the doc says is wrong; %s' % (
                            self.port.origin, item.similarity, item.other, why)
                    emit([show(region, other), kind, name + self.port.suffix, source, note_with(note, text + '; unconfirmed')],
                         ('function', region, 'lead'))
                    carried[region, value] = ('lead', other)
                    continue
                if item is None or other is None or item.status == 'missing':
                    dropped['function ' + (item.status if item else 'unmatched')] += 1
                    continue
                if (region, value) in self.port.dropped_functions:
                    if other != self.port.dropped_functions[region, value]:
                        sys.exit('%s: matched at 0x%x, not 0x%x; revisit the port\'s dropped_functions'
                                 % (name, other, self.port.dropped_functions[region, value]))
                    dropped['function matched at another routine\'s end'] += 1
                    continue
                pin = self.port.pinned.get((region, value))
                if pin is not None:
                    if other != pin[0]:
                        sys.exit('%s: matched at 0x%x, not 0x%x; revisit the port\'s pinned' % (name, other, pin[0]))
                    other = pin[1]
                if (region, other) in corrected_others:
                    dropped['function superseded by a correction'] += 1
                    continue
                status = item.status
                if status in CARRIED:
                    tag = status if outer is None else '%s, inside %s' % (status, outer.name)
                    if item.kind == 'piece' and outer is None:
                        tag = status + ' piece'
                    if pin is not None:
                        tag += '; ' + pin[2]
                    emit([show(region, other), kind, name, source, note_with(note, '%s: %s' % (self.port.origin, tag)),
                          prototype, convention], ('function', region, status))
                    carried[region, value] = (status, other)
                elif item.similarity >= THRESHOLD:
                    emit([show(region, other), kind, name, source,
                          note_with(note, '%s: changed, %s at %.3f' % (self.port.origin, item.method, item.similarity)),
                          prototype, convention], ('function', region, 'changed'))
                    carried[region, value] = ('changed', other)
                elif item.similarity < LEAD:
                    weak_leads.append((region, other, name, item.method, item.similarity))
                    counts['function', region, 'weak lead, unnamed'] += 1
                else:
                    emit([show(region, other), kind, name + self.port.suffix, source,
                          note_with(note, '%s: changed, %s at %.3f, below %.2f; a lead, its prototype and '
                                          'convention not carried' % (self.port.origin, item.method, item.similarity,
                                                                      THRESHOLD))],
                         ('function', region, 'lead'))
                    carried[region, value] = ('lead', other)
            elif kind == 'local':
                # the function may be named by Setup's scpi= or pforth=, not here
                item = self.code.get((region, value))
                if item is not None and item.status in CARRIED:
                    emit([show(region, item.other), kind, name, source, note, prototype, convention],
                         ('local', region, item.status))
                else:
                    dropped['local, function not identical or relocated'] += 1
            elif kind == 'call':
                outer = self.containing(region, value) or self.code.get((region, value))
                if outer is not None and outer.status in CARRIED and outer.other is not None:
                    emit([show(region, outer.other + value - outer.address), kind, name, source, note, prototype,
                          convention], ('call', region, outer.status))
                else:
                    dropped['call, function not identical or relocated'] += 1
            elif kind == 'label':
                label = self.labels.get((region, value))
                if label is None:
                    dropped['label not in image_diff'] += 1
                elif name == 'installer_records':
                    # the image's own row, its extent read from it, takes its place
                    dropped['label installer_records, read from the image'] += 1
                elif (region, value) in self.label_target:
                    other, text, key = self.label_target[region, value]
                    emit([show(region, other), kind, name, source, note_with(note, text), prototype, convention],
                         ('label', region, key))
                else:
                    dropped['label %s %s' % ('table' if self.is_table(region, value) else 'ram', label.status)] += 1
            else:
                dropped['kind ' + kind] += 1

        out_rows = [self.installer_copy(row, seen_names) for row in out_rows]

        # the image's own installer records, which Setup must type as data
        records, extent = image_diff.installer_records(self.other_image)
        length = extent[1] + 1 - extent[0]
        installer_row = ['0x%x' % extent[0], 'label', 'installer_records', 'read',
                         "boot_unpack_installer's %d bytes: an S, %d records of a C, a count, a RAM address and the "
                         "bytes, and an E at 0x%x (the %s's installer_records, its extent read from this image)"
                         % (length, len(records), extent[1], self.port.base_model), 'uint8_t[%d]' % length]
        assert '0x%x' % extent[0] not in seen_addresses

        # a new function a pin or a correction names has its row
        new = sorted((region, item.other, item.other_size, item.summary)
                     for region, found in self.results.items() for item in found
                     if item.status == 'new' and (region, item.other) not in corrected_others
                     and show(region, item.other) not in seen_addresses)
        listed = {(region, address) for region, address, _, _ in new}
        for key in self.port.not_code:
            if key not in listed:
                sys.exit("%s: not listed new; revisit the port's not_code" % show(*key))
        new = [item for item in new if item[:2] not in self.port.not_code]
        for region, address in self.port.called_unlisted:
            if (region, address) in listed or show(region, address) in seen_addresses:
                sys.exit('%s: listed or named by the matches; revisit the port\'s called_unlisted' % show(region, address))
            item = self.matchers[region].new_function(address, CALLED_UNLISTED_HOW)
            new.append((region, address, item.other_size, item.summary))
        new.sort()

        # the base's format description, from its line that starts it to
        # its installer records' block or its body, less the Z3805A's
        # sentence on which of a note's addresses count, which the header
        # replaces
        first = next(i for i, l in enumerate(self.lines) if 'Tab-separated: ' in l)
        last = next(i for i, l in enumerate(self.lines)
                    if i == self.body_start or l == INSTALLER_RECORDS % self.port.base_model)
        format_text = '\n'.join(['# Tab-separated: ' + self.lines[first].split('Tab-separated: ', 1)[1]]
                                + self.lines[first + 1:last])
        format_text = re.sub(r"  Only the Z3805A's own column of a note's table, or a\n# statement about this image, "
                             r"counts; an address a note gives for\n# another image does not carry over\.", '',
                             format_text)
        assert 'Only the Z3805A' not in format_text
        cleaned = format_text.split('\n')
        # the first paragraph, rewrapped where its first line lost its start
        first_end = cleaned.index('#')
        paragraph = ' '.join(l[2:] for l in cleaned[:first_end])
        cleaned = ['# ' + l for l in textwrap.wrap(paragraph, 70, break_on_hyphens=False)] + cleaned[first_end:]

        out_rows, new = self.with_hand_rows(out_rows, new, seen_names)
        text = [self.port.text(HEADER), '\n'.join(cleaned) + '\n',
                INSTALLER_RECORDS % self.port.model + '\n' + '\t'.join(installer_row) + '\n#\n',
                '\n'.join(out_rows) + '\n',
                '#\n# New functions: the %s\'s functions no %s function or piece\n'
                % (self.port.model, self.port.base_model) +
                '# matched (image_diff\'s "new"), and those found "called, by Ghidra\'s\n'
                '# analysis" that image_diff does not find, unnamed, for the rewrite\n'
                '# waves: entry rows, which Setup makes a function at, keeping\n'
                '# Ghidra\'s name, their note the extent in bytes and image_diff\'s\n'
                '# summary.  A wave that names one makes it a function row.\n']
        text += ['%s\t%s\t\t%s\t%d bytes; %s\n' % (show(region, address), ENTRY, ENTRY_SOURCE, size, summary)
                 for region, address, size, summary in new]
        text.append('#\n# Leads: functions image_diff pairs with a %s function as changed\n'
                    '# at a similarity below %.2f, a pairing too weak to name them by:\n'
                    '# address, the %s function, how it was paired and the similarity.\n'
                    % (self.port.base_model, LEAD, self.port.base_model))
        text += ['# %s\t%s\t%s\t%.3f\n' % (show(region, address), name, method, similarity)
                 for region, address, name, method, similarity in sorted(weak_leads)]
        self.counts, self.dropped = counts, dropped
        return ''.join(text), carried

    def hand_rows(self):
        """The names file's rows written by hand, which carry HAND in
        their note, in the file's order."""
        path = os.path.join(self.folder, 'names.tsv')
        if not os.path.exists(path):
            return []
        rows = []
        for line in open(path).read().split('\n'):
            fields = line.split('\t')
            if line.startswith('#') or len(fields) < 5 or self.port.hand not in fields[4]:
                continue
            if self.port.origin in fields[4]:
                sys.exit('%s: a row is written by hand or seeded, not both: %s' % (path, line))
            rows.append(line)
        return rows

    def with_hand_rows(self, out_rows, new, seen_names):
        """The carried rows and the new functions with the hand rows in
        them: each in place of the carried row of its address and kind,
        or of the new function at its address, which it names; the rest
        after the carried rows, under a comment of their own."""
        by_place = {place_key(row.split('\t')): row for row in self.hand_rows()}
        for row in self.hand_rows():
            fields = row.split('\t')
            seeded = seen_names.get(fields[2])
            if (fields[1] in ('function', 'label') and seeded not in (None, fields[0])
                    and (seeded, fields[1]) not in by_place):
                sys.exit('%s: written by hand at %s, seeded at %s' % (fields[2], fields[0], seeded))
        placed, rows = set(), []
        for row in out_rows:
            key = place_key(row.split('\t'))
            if key in by_place:
                placed.add(key)
            rows.append(by_place.get(key, row))
        named = {key[0] for key in by_place if key[1] == 'function'}
        new = [item for item in new if show(item[0], item[1]) not in named]
        rest = [row for key, row in by_place.items() if key not in placed]
        if rest:
            rows += ['#', '# Rows written by hand that no carried row is in place of'] + rest
        return rows, new

    def installer_copy(self, row, seen_names):
        """The row, an installer copy's note giving where it is relative
        to its primary in the image, where the primary is carried under
        its name; a copy said to be byte for byte is checked so."""
        fields = row.split('\t')
        found = INSTALLER_COPY.search(fields[4]) if len(fields) > 4 and fields[1] == 'function' else None
        if found is None:
            return row
        primary = seen_names.get(found.group(1))
        if primary is None:
            return row
        copy, start = image_diff.parse_address(fields[0])[1], int(primary, 16)
        if found.group(4) == ', byte for byte':
            item = self.code['primary', int(found.group(2), 16)]
            matcher = self.matchers['installer']
            if matcher.other.bytes(copy, copy + item.size) != self.other_image[start:start + item.size]:
                sys.exit('%s: not byte for byte its primary at 0x%x' % (fields[2], start))
        fields[4] = fields[4][:found.start()] + "the installer's copy of %s, at 0x%x plus 0x%x in this image%s" % (
            found.group(1), start, copy - start, found.group(4) or '') + fields[4][found.end():]
        return '\t'.join(fields)

    def modules(self, carried):
        """The modules file's text: each base row re-keyed."""
        rows = [self.port.text(MODULES_HEADER)]
        corrected_others = {(r, o) for (r, _), (o, _) in self.port.corrections.items()}
        lines = open(os.path.join(self.base_folder, 'modules.tsv')).read().split('\n')
        for line in lines:
            if not line.strip() or line.startswith('#'):
                continue
            f = line.split('\t')
            region, value = image_diff.parse_address(f[0])
            item = self.code.get((region, value))
            seeded = carried.get((region, value))
            if seeded is not None:
                other, name = seeded[1], f[1] + (self.port.suffix if seeded[0] == 'lead' else '')
            elif item is not None and item.other is not None and (
                    self.counted(item) or (item.status == 'changed' and item.similarity >= LEAD)):
                # a lead named by Setup (an SCPI handler): its module still a hint
                other, name = item.other, f[1]
            else:
                continue
            if (region, other) in corrected_others and seeded is None:
                continue
            rows.append('\t'.join([show(region, other), name] + f[2:]) + '\n')
        seeded_rows = {row.split('\t')[0] for row in rows[1:]}
        hand = self.hand_module_rows()
        for row in hand:
            if row.split('\t')[0] in seeded_rows:
                sys.exit('%s: a modules row written by hand at an address the seeding gives' % row.split('\t')[1])
        if hand:
            rows.append(self.port.text(MODULES_HAND_HEADER))
            rows += [row + '\n' for row in hand]
        return ''.join(rows)

    def hand_module_rows(self):
        """The modules file's rows written by hand, which carry HAND in
        their reason: the image's own functions, which no base row
        re-keys, in the file's order."""
        path = os.path.join(self.folder, 'modules.tsv')
        if not os.path.exists(path):
            return []
        return [line for line in open(path).read().split('\n')
                if line and not line.startswith('#') and self.port.hand in line.split('\t')[-1]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('base_folder')
    parser.add_argument('folder')
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--counts', help='where to write the counts carried and not')
    arguments = parser.parse_args()
    seeding = Seeding(os.path.abspath(arguments.base_folder), os.path.abspath(arguments.folder))
    names, carried = seeding.names()
    outputs = {'names.tsv': names, 'modules.tsv': seeding.modules(carried)}
    if arguments.counts:
        with open(arguments.counts, 'w') as counts:
            for key, count in sorted(seeding.counts.items()):
                counts.write('%s\t%d\n' % ('/'.join(key), count))
            for key, count in sorted(seeding.dropped.items()):
                counts.write('not carried: %s\t%d\n' % (key, count))
            for note in seeding.notes:
                counts.write(note + '\n')
    stale = False
    for file_name, text in outputs.items():
        path = os.path.join(arguments.folder, file_name)
        if arguments.write:
            with open(path, 'w') as out:
                out.write(text)
            continue
        current = open(path).read()
        if current != text:
            stale = True
            sys.stdout.writelines(difflib.unified_diff(current.splitlines(True), text.splitlines(True),
                                                       path, path + ' (seeded)'))
    if stale:
        sys.exit('%s: not what the matches give; make seed-names rewrites them' % arguments.folder)


if __name__ == '__main__':
    main()
