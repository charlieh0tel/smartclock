"""Seeds an image's names.tsv and modules.tsv from a base image's,
through the matches tools/firmware/image_diff.py finds between the two
images, by the rules the names file's header gives; or checks that they
are what the matches give: the 58503A 3704's from the Z3805A 3543B's,
the Z3815A 4010's from the 58503A 3704's (PORTS).

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
# from here to THRESHOLD it is a lead, under the base's name, its note saying
# so; below, no row
LEAD = 0.5
# a ROM table placed by its bytes alone and shorter than this is not carried
SMALLEST_BY_BYTES = 8
# the statuses carried as they stand
CARRIED = ('identical', 'relocated')
# the first line of the base file's body, after its header
BODY_START = '# float library'
# an image's two regions: its primary, and its installer as unpacked
REGIONS = ('primary', 'installer')

# The 58503A 3704's port from the Z3805A 3543B (PORT_58503A_3704).
DOCUMENT_58503A = 'docs/firmware/58503a-3704.md'
# the pairings docs/firmware/58503a-3704.md corrects: base function's
# (region, address) to the 58503A address and why; a lead, noted
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
CORRECTIONS_TEXT_58503A = "#   docs/firmware/58503a-3704.md corrects one pairing: 0x30b24 is\n#   save_calibration_block's counterpart, 0x30c6c save_parameter_record's.\n#   It corrects four more, which the image settles, and carries them\n#   under their names: the Oncore decoders oncore_visibility_message,\n#   oncore_traim_message and oncore_receiver_id_message, which image_diff\n#   pairs by their shape, are 0x50716, 0x50924 and 0x50968, the message\n#   table's @@Bb, @@Bn and @@Cj decoders (0x51178); and\n#   selftest_gps_with_lamp, which image_diff pairs by its bytes with the\n#   power test's, 0x18efa, is 0x18ed6, the self-test table's GPS entry\n#   (0x18814), which tests a long.  The table also names\n#   selftest_cpu_result, selftest_ram_result, selftest_eeprom_result and\n#   selftest_power, 0x18746, 0x18750, 0x1875a and 0x18046, where\n#   image_diff pairs the first with the EEPROM's and finds the others\n#   missing; supplies_test is 0x186da, which both call.  Routines\n#   image_diff pairs at another's last instructions or finds missing\n#   are carried where the 58503A's callers name them: selftest_init,\n#   gps_pps_monitor_reset and internal_pps_monitor_reset at 0x18574,\n#   0x238b0 and 0x23a3c, one store ahead of the Z3805A's (image_diff\n#   takes the stores after it); enter_installer at 0x130da, which\n#   deletes fpnl first (image_diff takes its trap #11); scpi_input_source at\n#   0x29246, the handlers' (image_diff takes an uncalled tail at\n#   0x28d98); scpi_console_port at 0x28db4 and the installer's at\n#   installer::0x10739c, the port layer's for port 2 (image_diff takes\n#   the tails of the session starts); monitors_reset at 0x23dc2,\n#   timecode_schedule at 0x140d0, scpi_task at 0x294cc and the\n#   installer's at installer::0x1079ec, temporary_hold_set at 0x45cbc\n#   and gps_script_lookup at 0x4c80e, each by its caller or table.\n#   sci_echo_none, an rts image_diff finds at qspi_fpga_byte_write's end\n#   (0x1f328), has no row: the 58503A has no SCI.\n"
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
#   to %(threshold).2f the pairing is a lead, under the %(base_model)s's name, its
#   note saying so, without its prototype or convention; below %(lead).2f
#   there is no row, and the pairing is listed under "Leads" at the end.
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
#   its start%(labels)s# - Missing functions, functions matched inside the installer's
#   records (the installer's copy, whose bytes they are, unpacked), and
#   tables or labels not placed so, have no row;
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
%(second)s%(installer)s#
"""
INSTALLER_HEADER = """\
# - The installer is named from the installer's base, the %(installer_base)s's
#   (third_party/firmware/analysis/%(installer_folder)s/names.tsv), not the
#   %(base_model)s's, by the same rules and image_diff's matches between the
#   %(installer_model)s and this image: its functions, local and call rows and
#   tables, and the RAM labels its code's references place, where their
#   address and name are free.  Neither the %(base_model)s's installer rows nor
#   its installer's references give a row.  Their notes end
#   "%(installer_origin)s".
"""
SECOND_HEADER = """\
# - A function the matches find new, which the %(base_model)s has no
#   counterpart of, is carried from the second base, the %(second_base)s's
#   (third_party/firmware/analysis/%(second_folder)s/names.tsv), by the same rules and
#   image_diff's matches between the %(second_model)s and this image,
#   where it is identical, relocated or changed at %(threshold).2f or more
#   there and its name is free; so are its local rows, and a ROM table
#   the second base carries at an address and under a name no other row
#   takes.  Its note ends "%(second_origin)s".
"""
INSTALLER_ROWS_HEADER = """\
#
# Rows from the installer's base, the %(installer_base)s's, for the installer
"""
SECOND_ROWS_HEADER = """\
#
# Rows from the second base, the %(second_base)s's, for the functions and tables
# the %(base_model)s has no counterpart of
"""
MODULES_HEADER = """\
# The %(image)s firmware's C functions by module, seeded from the
# %(base)s's (third_party/firmware/analysis/%(base_folder)s/modules.tsv): each
# %(base_model)s row re-keyed to its function's %(model)s address through
# tools/firmware/image_diff.py's matches, as names.tsv is (identical,
# relocated, or changed at a similarity of %(threshold).2f or more, and from
# %(lead).2f a lead), under the name names.tsv gives its address, or the
# %(base_model)s's where it gives none.  A %(base_model)s function missing
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
# "%(hand)s" is kept as it is: in
# place of the seeded row of its address, which it corrects, or after
# the seeded rows, for a function the seeding gives no row.
"""
MODULES_SECOND_HEADER = """\
#
# The second base's, the %(second_base)s's rows, re-keyed as names.tsv's rows
# from it are, for the functions the %(base_model)s has no counterpart of.
"""
MODULES_HAND_HEADER = """\
#
# The %(model)s's own functions, which no %(base_model)s row re-keys: rows added
# by hand, by module as above.
"""

# Each image by its folder's name: its model and revision, as a
# document names it.
IMAGE_NAMES = {'z3805a-3543b': ('Z3805A', '3543B'), '58503a-3704': ('58503A', '3704'),
               'z3815a-4010': ('Z3815A', '4010'), 'z3801a-3543': ('Z3801A', '3543'),
               'z3817a-3752': ('Z3817A', '3752'), 'z3816a-4001': ('Z3816A', '4001'),
               '58503b-1.01.04': ('58503B', '1.01.04'), 'z3805a-1.01.03': ('Z3805A', '1.01.03')}


@dataclasses.dataclass(frozen=True)
class Port:
    """An image seeded from a base image's names: its folder's and the
    base's names; the document that describes the port; the corrections
    to image_diff's pairings that document makes, base function's (region,
    address) to the image's address and why, each a lead, noted
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
    starts image_diff rejects, the not_code starts; hand_figures, the
    document's counts of the hand rows, each a file's name and a
    regular expression whose one group is its count."""

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
    hand_figures: tuple = ()
    # the second base's folder, which names what the base has no
    # counterpart of (SECOND_HEADER), or none; and whether this port is
    # itself a second base's, whose rows say so
    second_base_folder: str = ''
    second: bool = False
    # whether image_diff moves a masked match to the byte match whose
    # calls agree with it (image_diff.py's --callee-ties): the Makefile's
    # CALLEE_TIES_DIFFS passes it for the same diffs
    callee_ties: bool = False
    # the second base's dropped_functions, as dropped_functions
    second_dropped_functions: dict = dataclasses.field(default_factory=dict)
    # the regions whose rows the seeding gives: their functions, local
    # and call rows and tables, and the RAM labels their code's
    # references place
    regions: frozenset = frozenset(REGIONS)
    # the installer's base's folder, which names the installer in place
    # of the base (INSTALLER_HEADER), or none; with one, regions is the
    # primary's alone
    installer_base_folder: str = ''

    def __post_init__(self):
        if self.installer_base_folder and 'installer' in self.regions:
            raise ValueError('%s: the installer from %s, so not from %s' % (
                self.folder, self.installer_base_folder, self.base_folder))

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
    def origin(self):
        """How a carried row's note says where it came from."""
        role = ''
        if self.second:
            role = ' as the second base'
        elif 'primary' not in self.regions:
            role = " as the installer's base"
        return 'from the %s, by image_diff%s' % (self.base, role)

    @property
    def second_port(self):
        """The port from the second base, its rows the second base's."""
        return Port(folder=self.folder, base_folder=self.second_base_folder, second=True, regions=self.regions,
                    dropped_functions=self.second_dropped_functions)

    @property
    def installer_port(self):
        """The port from the installer's base, its rows the installer's."""
        return Port(folder=self.folder, base_folder=self.installer_base_folder, regions=frozenset({'installer'}))

    @property
    def document(self):
        return 'docs/firmware/%s.md' % self.folder

    @property
    def hand(self):
        """The mark a row added by hand carries in its note, in place
        of origin: the seeding keeps it as it is, in place of the row the
        matches give at its address, or else among the hand rows after
        the carried ones, so that check-seed-names checks only the rest."""
        return 'row added by hand for the %s' % self.image

    def text(self, template):
        """A header, its fields this port's."""
        second = self.second_port if self.second_base_folder else None
        installer = self.installer_port if self.installer_base_folder else None
        fields = {
            'second': '', 'second_base': second.base if second else '',
            'second_model': second.base_model if second else '',
            'second_folder': self.second_base_folder, 'second_origin': second.origin if second else '',
            'installer': '', 'installer_base': installer.base if installer else '',
            'installer_model': installer.base_model if installer else '',
            'installer_folder': self.installer_base_folder, 'installer_origin': installer.origin if installer else '',
            'threshold': THRESHOLD, 'lead': LEAD, 'smallest': SMALLEST_BY_BYTES,
            'called': len(self.called_unlisted), 'hand': self.hand, 'image': self.image, 'model': self.model,
            'base': self.base, 'base_model': self.base_model, 'base_folder': self.base_folder,
            'document_name': os.path.basename(self.document), 'corrections': self.corrections_text,
            'labels': self.labels_text, 'tables': self.tables_text, 'hand_rows': self.hand_rows_text,
            'not_code': self.not_code_text}
        if second:
            fields['second'] = SECOND_HEADER % fields
        if installer:
            fields['installer'] = INSTALLER_HEADER % fields
        return template % fields

    @property
    def unmatched_by(self):
        """Whose functions the new functions and the leads are counted
        against: the base's, and the installer's base's in the installer."""
        if not self.installer_base_folder:
            return self.base_model
        return '%s (in the installer, %s)' % (self.base_model, self.installer_port.base)


PORT_58503A_3704 = Port(
    folder='58503a-3704', base_folder='z3805a-3543b', corrections=CORRECTIONS_58503A,
    confirmed=frozenset(CONFIRMED_58503A), corrected_from_changed=CORRECTED_FROM_CHANGED_58503A,
    pinned=PINNED_58503A, dropped_functions=DROPPED_FUNCTIONS_58503A, placed_by_start=PLACED_BY_START_58503A,
    layout=LAYOUT_58503A, called_unlisted=CALLED_UNLISTED_58503A, dropped=DROPPED_58503A,
    corrections_text=CORRECTIONS_TEXT_58503A, labels_text=LABELS_TEXT_58503A, tables_text=TABLES_TEXT_58503A,
    hand_rows_text=HAND_ROWS_TEXT_58503A,
    hand_figures=(('names.tsv', r'(\d+) rows are added by hand, each note'),
                  ('modules.tsv', r'`modules.tsv` keeps (\d+) rows added by\s+hand')))
# The Z3815A 4010's port from the 58503A 3704, the closer of the two
# named images (docs/firmware/z3815a-4010.md, "The base"): the functions
# whose few bytes match at more than one address, pinned where the
# Z3815A's callers name them, as the 58503A's port pins them.
PINNED_Z3815A = {
    ('primary', 0x47fa6): (
        0x5140c, 0x4be5c,
        "its 2 bytes, an rts, match more than one; pinned to 0x4be5c, which output_state_done calls (0x4bec6), "
        "as the 58503A's calls 0x47fa6"),
    ('primary', 0x47fa8): (
        0x516d8, 0x4be5e,
        "its 2 bytes, an rts, match more than one; pinned to 0x4be5e, which pulse_settings_done calls, as the "
        "58503A's calls 0x47fa8"),
    ('primary', 0x290ac): (
        0x3c3f6, 0x3c636,
        "its 4 bytes match more than one; pinned to 0x3c636, which scpi_interpret calls three times (0x3b9da, "
        "0x3ba0e, 0x3bb04), as the 58503A's calls 0x290ac"),
    ('primary', 0x290b0): (
        0x3c636, 0x3c63a,
        "its 4 bytes match more than one; pinned to 0x3c63a, which scpi_interpret calls once, at 0x3bac8, where "
        "the 58503A's calls 0x290b0"),
    ('installer', 0x107674): (
        0x108156, 0x1074de,
        "its 4 bytes match more than one; pinned to installer::0x1074de, which installer_scpi_interpret calls "
        "three times, as scpi_interpret calls scpi_message_done"),
    ('installer', 0x107678): (
        0x1081ec, 0x1074e2,
        "its 4 bytes match more than one; pinned to installer::0x1074e2, which installer_scpi_interpret calls "
        "once, at installer::0x1072f4, as scpi_interpret calls scpi_input_closed"),
}
LABELS_TEXT_Z3815A = '''.
#   The functions whose few bytes match at more than one address are
#   pinned where the Z3815A's callers name them: output_state_program
#   and pulse_program, bare rts, at 0x4be5c and 0x4be5e, which
#   output_state_done and pulse_settings_done call, where image_diff
#   takes 0x5140c and 0x516d8; and scpi_message_done and
#   scpi_input_closed at 0x3c636 and 0x3c63a, by scpi_interpret's
#   calls, and the installer's at installer::0x1074de and 0x1074e2, for
#   0x3c3f6 and 0x3c636, 0x108156 and 0x1081ec.
'''
# the two words before psos_config_defaults (0x5ed0a) that the NV
# descriptors point to (0x47abe on, 0x48286 on), after
# primary_unpack_records' rts, which image_diff finds new by those
# pointers: data, whose first long is no address
NOT_CODE_Z3815A = frozenset({('primary', 0x5ed02), ('primary', 0x5ed04)})
NOT_CODE_TEXT_Z3815A = '''  Nor are the
#   words at 0x5ed02 and 0x5ed04, before psos_config_defaults, which the
#   NV descriptors point to, though image_diff finds them new.'''
# the pairings docs/firmware/z3815a-4010.md corrects, each confirmed by
# a relocated caller's call where the 58503A's calls the base function
CORRECTIONS_Z3815A = {
    ('primary', 0x31b64): (0x46260, "%s: log_write's call (0x45f32), where the 58503A's log_write calls "
                                    "log_append (0x3187c)" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x290ce): (0x3c670, "%s: port_report_errors' two calls (0x3c6f6, 0x3c72e); its switch has a case "
                                    "for port 1, 0x3fa72, and for port 2, status_report" % 'docs/firmware/z3815a-4010.md'),
    ('installer', 0x108280): (0x108372, "%s: installer_system_init's call (installer::0x100c84), where the "
                                        "58503A's calls installer_parameter_record_read (installer::0x100888); "
                                        "its defaults, before it at installer::0x10836a, are what image_diff "
                                        "pairs" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x23dc2): (0x36f2e, "%s: system_init's two calls (0x22e90, 0x22ebe), where the 58503A's "
                                    "system_init calls monitors_reset (0x12e9c, 0x12eca); image_diff pairs "
                                    "0x36e7c, counter_reset's call of counter_load_registers" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x47716): (0x50d06, "%s: its stage steps, \"PLL powerup\" to \"Went into fatal error\" (0x515b6 "
                                    "to 0x51688), each calling the stage's routine as the 58503A's does"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x46072): (0x4f76a, "%s: pllp_task's stage 1, \"PLL powerup\" (0x50f6a), as the 58503A's calls "
                                    "powerup" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x4429e): (0x4d652, "%s: pllp_task's stage 5, \"Went into PLL startup\" (0x510a4); its "
                                    "\"startup_pll - Error with measurement\"" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x4495a): (0x4ddea, "%s: pllp_task's stage 7, \"Went into PLL normal\" (0x5113e); its "
                                    "\"pll_normal - Error with measurement\"" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x2917c): (0x3c772, "%s: scpi_task_run's call with its port and 1 (0x3ca32), as the 58503A's "
                                    "calls port_prompt_set" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x291a6): (0x3c7ae, "%s: scpi_task_run's call with its port (0x3ca3c), whose cases call the "
                                    "sessions' inits (0x3c7ca, 0x3c7d2), as the 58503A's calls port_session_init"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x29154): (0x3c742, "%s: :SYSTem:COMMunicate:ALARm:PROMpt?'s call (0x443e4), whose cases call "
                                    "the prompts' getters (0x3c75e, 0x3c766)" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x290f8): (0x3c6ac, "%s: console_port_read's call on a read error (0x3c1c2), as the 58503A's "
                                    "calls port_report_errors" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x29246): (0x3c84e, "%s: the SCPI handlers' calls with their call's input, 0 for the SCPI "
                                    "port's object (0x3c034) and 1 for another" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x2bcfa): (0x3fac2, "%s: scpi_query_failed's call (0x3c008); its start loads the code into D1 "
                                    "before the switch the 58503A's starts with" % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x290b4): (0x3c63e, "%s: error_queue_push's and error_queue_pop's calls (0x3aba0, 0x3ac1a), "
                                    "its first test scpi_status_model's, calling scpi_prompt, as the 58503A's"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x13104): (0x230d6, "%s: root_task's call (0x233cc), which has spoo_run start "
                                    "0x3cbb6, the creator of \"scpi\" at scpi_task, as the 58503A's starts "
                                    "scpi_task_create; image_diff pairs 0x230f6, the same bytes, the SCI session's"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x294f8): (0x3cbb6, "%s: scpi_task_start's routine (0x230de), which creates \"scpi\" (0x3cc6c) "
                                    "and starts it at scpi_task, as the 58503A's; image_diff pairs 0x3cb3c, the "
                                    "same bytes, which creates \"sci\" at sci_scpi_task"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x1eb22): (0x2f3fc, "%s: primary_reset's call (0x24f6a), as the 58503A's primary_reset calls "
                                    "duart_early_init; image_diff pairs it with the boot code's copy, "
                                    "boot_duart_early_init, the same bytes, which this image's boot code has not"
                           % 'docs/firmware/z3815a-4010.md'),
    ('primary', 0x130da): (0x230d2, "%s: scpi_task_run's call for :SYSTem:LANGuage \"INSTALL\" (0x3cacc), "
                                    "trap #11 alone, as the Z3805A's" % 'docs/firmware/z3815a-4010.md'),
}
# installer_duartb_baud_from_settings' bytes are the end of
# installer::0x103a88, which runs into them with no return before;
# status_screen_print's the end of 0x56b0c, which picks one of two
# snapshots by its first argument and branches into them;
# installer_duarta_drain's, its one branch, a branch inside
# installer_console_write_polled to its loop's test, this image's
# installer reading nothing from DUART channel A
DROPPED_FUNCTIONS_Z3815A = {('installer', 0x1030b6): 0x103b02, ('primary', 0x4b6ec): 0x56b1c,
                            ('installer', 0x103a14): 0x103d9c}
CORRECTIONS_TEXT_Z3815A = '''#   docs/firmware/z3815a-4010.md corrects sixteen pairings and carries
#   them under their names.  Four by a relocated caller's call where
#   the 58503A's calls the function: log_append is 0x46260, which
#   log_write calls (image_diff pairs it by its callees, at 0.482),
#   port_status_report 0x3c670, which port_report_errors calls (image_diff
#   pairs 0x3c742, at 0.571), installer_parameter_record_read
#   installer::0x108372, which installer_system_init calls (image_diff
#   pairs its defaults before it, installer::0x10836a, at 0.925), and
#   monitors_reset 0x36f2e, which system_init calls (image_diff pairs
#   0x36e7c, counter_reset's tail, by its bytes).  Four
#   by pllp_task's stage steps, whose debug text names each stage:
#   pllp_task is 0x50d06 (image_diff finds it missing), powerup 0x4f76a,
#   which stage 1 calls (missing), startup_pll 0x4d652, which stage 5
#   calls and which prints "startup_pll - Error with measurement", and
#   pll_normal 0x4ddea, which stage 7 calls and which prints "pll_normal
#   - Error with measurement" (image_diff pairs startup_pll with it, at
#   0.686, and finds pll_normal missing).  Eight by the SCPI port
#   layer's calls, its switches having a case for each of the two
#   sessions: port_prompt_set 0x3c772 and port_session_init 0x3c7ae,
#   which scpi_task_run calls (image_diff pairs port_prompt_get with
#   0x3c7ae), port_prompt_get 0x3c742, which :ALARm:PROMpt? calls,
#   port_report_errors 0x3c6ac, which console_port_read calls,
#   scpi_input_source 0x3c84e, which the handlers call (image_diff pairs
#   0x3c7da, a moveq inside 0x3c7ae), status_report_from 0x3fac2, which
#   scpi_query_failed calls (0x3fac6, past its first load),
#   status_prompt_refresh 0x3c63e, which error_queue_push calls (0x3c656,
#   its second test), and enter_installer 0x230d2, which scpi_task_run
#   calls for INSTALL (a lead at 0.000).
#   installer_duartb_baud_from_settings, whose bytes image_diff finds at
#   installer::0x103b02, has no row: they are the end of
#   installer::0x103a88, which runs into them; nor has
#   status_screen_print, whose bytes it finds at 0x56b1c, the end of
#   0x56b0c, which a hand row names so; nor installer_duarta_drain,
#   whose one branch it finds at installer::0x103d9c, inside
#   installer_console_write_polled, before its loop: this image's
#   installer reads nothing from DUART channel A.
'''
PORT_Z3815A_4010 = Port(folder='z3815a-4010', base_folder='58503a-3704', pinned=PINNED_Z3815A,
                        corrections=CORRECTIONS_Z3815A, confirmed=frozenset(CORRECTIONS_Z3815A),
                        dropped_functions=DROPPED_FUNCTIONS_Z3815A, corrections_text=CORRECTIONS_TEXT_Z3815A,
                        labels_text=LABELS_TEXT_Z3815A, not_code=NOT_CODE_Z3815A,
                        not_code_text=NOT_CODE_TEXT_Z3815A,
                        hand_figures=(('names.tsv', r'Hand rows \((\d+), "'),))
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
DOCUMENT_Z3801A = 'docs/firmware/z3801a-3543.md'
# image_diff's pairings the self-test table corrects, base function's
# (region, address) to the Z3801A address and why: the three results
# are byte for byte the Z3805A's, but selftest moved 2 bytes lower, so
# that each Z3805A result's bytes are the next result's here, and the
# EEPROM's are the tail of selftest_fpga_with_lamp (0x189ea)
CORRECTIONS_Z3801A = {
    ('primary', 0x185e2): (0x182ea, "%s: the self-test table's PROC entry (0x183c0)" % DOCUMENT_Z3801A),
    ('primary', 0x185ec): (0x182f4, "%s: the self-test table's RAM entry (0x183c8)" % DOCUMENT_Z3801A),
    ('primary', 0x185f6): (0x182fe, "%s: the self-test table's EEPROM entry (0x183d0)" % DOCUMENT_Z3801A),
}
CORRECTIONS_TEXT_Z3801A = (
    "#   docs/firmware/z3801a-3543.md corrects three pairings, which the\n"
    "#   image settles, and carries them under their names: the self-test\n"
    "#   table names selftest_cpu_result, selftest_ram_result and\n"
    "#   selftest_eeprom_result 0x182ea, 0x182f4 and 0x182fe (0x183c0),\n"
    "#   where image_diff pairs the first two by their bytes with the next\n"
    "#   result's, selftest having moved 2 bytes lower, and the third with\n"
    "#   selftest_fpga_with_lamp's last instructions (0x189ea).\n")
PORT_Z3801A_3543 = Port(folder='z3801a-3543', base_folder='z3805a-3543b', corrections=CORRECTIONS_Z3801A,
                        confirmed=frozenset(CORRECTIONS_Z3801A), dropped=DROPPED_Z3801A,
                        corrections_text=CORRECTIONS_TEXT_Z3801A, labels_text=LABELS_TEXT_Z3801A,
                        hand_figures=(('names.tsv', r'(\d+) hand rows \("'),))
# The Z3817A 3752's port from the Z3815A 4010 (docs/firmware/z3817a-3752.md,
# "The base").
# functions whose few bytes match at more than one address, pinned where
# the Z3817A's callers name them: base function's (region, address) to
# the address image_diff gives, which is asserted, the address pinned and
# why
PINNED_Z3817A = {
    ('primary', 0x4be5c): (
        0x47ca4, 0x47ca2,
        "its 2 bytes, an rts, match more than one; pinned to 0x47ca2, which output_state_done calls (0x47d0c), "
        "as the Z3815A's calls 0x4be5c"),
    ('primary', 0x4be5e): (
        0x47ca2, 0x47ca4,
        "its 2 bytes, an rts, match more than one; pinned to 0x47ca4, which pulse_settings_done calls (0x47cfa), "
        "as the Z3815A's calls 0x4be5e"),
    ('primary', 0x3c636): (
        0x39a7c, 0x39936,
        "its 4 bytes match more than one; pinned to 0x39936, which scpi_interpret calls three times (0x38fe4, "
        "0x39018, 0x3910e), as the Z3815A's calls 0x3c636"),
}
# the pairing docs/firmware/z3817a-3752.md corrects, confirmed by a
# relocated caller's call where the Z3815A's calls the base function
CORRECTIONS_Z3817A = {
    ('primary', 0x3c63a): (0x3993a, "%s: scpi_interpret's call at 0x390d2, where the Z3815A's calls "
                                    "scpi_input_closed (0x3bac8); image_diff pairs 0x39ac0, which returns 1"
                           % 'docs/firmware/z3817a-3752.md'),
    ('primary', 0x30942): (0x2f338, "%s: deferred_flush's call (0x2ffa4), where the Z3815A's calls printf "
                                    "(0x31666); it formats into 160 bytes and writes them through console_write "
                                    "(0x2f2ec), as the Z3815A's printf does through its own (0x308f6); image_diff "
                                    "pairs it with duartb_printf, the same code but for its write's offset"
                           % 'docs/firmware/z3817a-3752.md'),
    ('primary', 0x2fda8): (0x2e7a2, "%s: sci_write's and sci_putc's calls (0x2f23c, 0x2f2a0), where the "
                                    "Z3815A's call sci_puts (0x30846, 0x308aa); it returns 0 at once for no "
                                    "length, as the installer's copy does, and image_diff pairs the code after "
                                    "that test, 0x2e7ac, which nothing calls"
                           % 'docs/firmware/z3817a-3752.md'),
    ('primary', 0x49f24): (0x45dea, "%s: measured_uncertainty_update's call (0x4602e), where the Z3815A's "
                                    "calls measured_uncertainty_compute (0x4a194); image_diff starts it 6 bytes "
                                    "early, at 0x45de4, the pllc_request_b descriptor that "
                                    ":DIAGnostic:REFerence:CPORt's handler pushes (0x3cae6)"
                           % 'docs/firmware/z3817a-3752.md'),
}
CORRECTIONS_TEXT_Z3817A = '''#   docs/firmware/z3817a-3752.md corrects four pairings and carries them
#   under their names, each by a relocated caller's call where the
#   Z3815A's calls the function: scpi_input_closed is 0x3993a, which
#   scpi_interpret calls once, at 0x390d2 (image_diff pairs 0x39ac0,
#   which returns 1, at 1.000); printf 0x2f338, which deferred_flush
#   calls (image_diff finds it missing and pairs 0x2f338 with
#   duartb_printf, whose code differs only in its write's offset);
#   sci_puts 0x2e7a2, which sci_write and sci_putc call (image_diff
#   pairs 0x2e7ac, the code after its test for no length); and
#   measured_uncertainty_compute 0x45dea, which
#   measured_uncertainty_update calls (image_diff starts it at 0x45de4,
#   in the request descriptor before it).
#   Five functions whose bytes image_diff finds have no row, each the
#   end of another function, which nothing calls or takes: mux_set, a
#   bare rts, at 0x4d4ae, the return of 0x4d46e, timeref_select_apply,
#   this image having no set_mux word to call it; sci_scpi_session_close
#   at 0x3987e, status_byte_set_bits' call of status_update_rqs and its
#   return; nmea_encode_nothing at 0x55fba, long_array_get_byte's last
#   two instructions; return_one at 0x52af4, gps_request_admitted's
#   return of 1, which its tests branch to (0x52acc, 0x52ade, 0x52ae8);
#   and nmea_frame_any at 0x56698, gps_position_get's return of 1, which
#   it branches to (0x5668e).
'''
# the functions whose bytes are another function's end, which nothing
# calls: mux_set's, a bare rts, timeref_select_apply's return, the
# image having no set_mux word, the Z3815A's diag_set_mux, to call it;
# sci_scpi_session_close's, status_byte_set_bits' tail, its call of
# status_update_rqs; nmea_encode_nothing's, long_array_get_byte's;
# return_one's, gps_request_admitted's return of 1; nmea_frame_any's,
# gps_position_get's
DROPPED_FUNCTIONS_Z3817A = {('primary', 0x516da): 0x4d4ae, ('primary', 0x3c3ee): 0x3987e,
                            ('primary', 0x5c568): 0x55fba, ('primary', 0x5172c): 0x52af4,
                            ('primary', 0x5d482): 0x56698}
LABELS_TEXT_Z3817A = '''.
#   The functions whose few bytes match at more than one address are
#   pinned where the Z3817A's callers name them: output_state_program
#   and pulse_program, bare rts, at 0x47ca2 and 0x47ca4, which
#   output_state_done and pulse_settings_done call, where image_diff
#   swaps them; and scpi_message_done at 0x39936, by scpi_interpret's
#   three calls, for 0x39a7c.
'''
PORT_Z3817A_3752 = Port(folder='z3817a-3752', base_folder='z3815a-4010', second_base_folder='58503a-3704',
                        pinned=PINNED_Z3817A, dropped_functions=DROPPED_FUNCTIONS_Z3817A,
                        corrections=CORRECTIONS_Z3817A, confirmed=frozenset(CORRECTIONS_Z3817A),
                        corrections_text=CORRECTIONS_TEXT_Z3817A, labels_text=LABELS_TEXT_Z3817A,
                        callee_ties=True)
# The Z3816A 4001's port from the 58503A 3704, the Z3815A 4010 its second
# base (docs/firmware/z3816a-4001.md, "The base"): the two bare rts
# pinned where their callers name them; and DUART channel B's driver
# state, the 58503A's and its installer's, dropped where the SCI's is
# (docs/firmware/z3816a-4001.md, "The host port").
PINNED_Z3816A = {
    ('primary', 0x47fa6): (
        0x4b98a, 0x46468,
        "its 2 bytes, an rts, match more than one; pinned to 0x46468, which output_state_done calls (0x464d2), "
        "as the 58503A's calls 0x47fa6"),
    ('primary', 0x47fa8): (
        0x4ba08, 0x4646a,
        "its 2 bytes, an rts, match more than one; pinned to 0x4646a, which pulse_settings_done calls (0x464c0), "
        "as the 58503A's calls 0x47fa8"),
    ('primary', 0x1d3d0): (
        0x2d49a, 0x2e60e,
        "its 2 bytes, an rts, match more than one; pinned to 0x2e60e, which the SCI's init stores as its echo "
        "(0x2efa0), as the 58503A's duart_init stores 0x1d3d0"),
    ('installer', 0x107674): (
        0x10813a, 0x1074c2,
        "its 4 bytes match more than one; pinned to installer::0x1074c2, which installer_scpi_interpret calls "
        "three times, as the 58503A's calls installer::0x107674"),
    ('installer', 0x107678): (
        0x1081d0, 0x1074c6,
        "its 4 bytes match more than one; pinned to installer::0x1074c6, which installer_scpi_interpret calls "
        "once, as the 58503A's calls installer::0x107678"),
}
DROPPED_Z3816A = {'duartb': 0x1037b2, 'installer_duartb_receive_errors': 0x10aff7}
LABELS_TEXT_Z3816A = '''.
#   The functions whose few bytes match at more than one address are
#   pinned where the Z3816A's callers name them: output_state_program
#   and pulse_program, bare rts, at 0x46468 and 0x4646a, which
#   output_state_done and pulse_settings_done call, where image_diff
#   pairs other rts; duartb_echo_none at 0x2e60e, which the SCI's init
#   stores as its echo, for 0x2d49a; and installer_scpi_message_done and
#   installer_scpi_input_closed at installer::0x1074c2 and
#   installer::0x1074c6, which installer_scpi_interpret calls, where it
#   pairs installer::0x10813a and installer::0x1081d0.
#   duartb and installer_duartb_receive_errors are dropped, though
#   placed: the SCI, not DUART channel B, is the host port, and hand rows
#   place its driver state, sci at 0x1037ea and installer_sci at
#   0x10afea, over them.
'''
CORRECTIONS_Z3816A = {
    ('primary', 0x4b6ec): (0x4f710, "%s: :SYSTem:PRINt?'s call (0x3ffd6), where the 58503A's calls "
                                    "status_screen_print; it picks its snapshot by its first argument, "
                                    "and image_diff pairs 0x4f720, its last instructions, the 58503A's code"
                           % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x4aad0): (0x4e9d2, "%s: the health monitor's third part, Oven Pwr, in status_screen's table "
                                    "(0x5246c), as the 58503A's (0x4de3c); it passes bit_is_set 4 (0x4e9d2), "
                                    "and image_diff pairs 0x4e976, which passes 12, by its callee"
                           % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x4aa74): (0x4e976, "%s: the health monitor's fourth part, OCXO, in status_screen's table "
                                    "(0x5247a), as the 58503A's (0x4de4c); it passes bit_is_set 12 (0x4e976), "
                                    "and image_diff pairs 0x4e9d2, which passes 4, by its callee"
                           % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x29246): (0x39480, "%s: the SCPI handlers' 147 calls with their call's input, as the "
                                    "58503A's; image_diff pairs 0x3943c, port_object's last instructions, "
                                    "by their bytes" % 'docs/firmware/z3816a-4001.md'),
    ('installer', 0x108280): (0x108356, "%s: installer_system_init's call (installer::0x100c84), where the "
                                        "58503A's calls installer_parameter_record_read (installer::0x100888); "
                                        "its defaults, before it at installer::0x10834e, are what image_diff "
                                        "pairs, as the Z3815A 4010's" % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x238b0): (0x33ab6, "%s: pps_monitors_reset's first call (0x33fc2), as the 58503A's first "
                                    "call (0x23dc2); image_diff pairs timecode_init (0x24070), which clears "
                                    "five bytes as the 58503A's does" % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x23a3c): (0x33c36, "%s: pps_monitors_reset's second call (0x33fc6), as the 58503A's second; "
                                    "image_diff pairs 0x2407c, inside timecode_init, which nothing calls"
                           % 'docs/firmware/z3816a-4001.md'),
    ('primary', 0x1eb22): (0x2dd64, "%s: primary_reset's call (0x24aa2), as the 58503A's primary_reset calls "
                                    "duart_early_init (0x14958); image_diff pairs it with the boot code's copy, "
                                    "boot_duart_early_init, the same bytes, which this image's boot code has not"
                           % 'docs/firmware/z3816a-4001.md'),
}
CORRECTIONS_TEXT_Z3816A = '''#   docs/firmware/z3816a-4001.md corrects eight pairings and carries them
#   under their names: status_screen_print is 0x4f710, which
#   :SYSTem:PRINt? calls where the 58503A's calls the function
#   (image_diff pairs 0x4f720, its last instructions, which are the
#   58503A's code); bit_4_set and bit_12_set, which image_diff swaps
#   by their callee, are 0x4e9d2 and 0x4e976, by the bit each passes and
#   its place in status_screen's health table;
#   installer_parameter_record_read is installer::0x108356, which
#   installer_system_init calls, where image_diff pairs its defaults,
#   8 bytes before it, as for the Z3815A 4010; scpi_input_source is
#   0x39480, which the SCPI handlers call, where image_diff pairs
#   0x3943c, port_object's last instructions, by their bytes; and
#   gps_pps_monitor_reset and internal_pps_monitor_reset are 0x33ab6 and
#   0x33c36, which pps_monitors_reset calls, where image_diff pairs
#   timecode_init, 0x24070, which clears five bytes as the first does,
#   and 0x2407c inside it; and duart_early_init is 0x2dd64, which
#   primary_reset calls, where image_diff pairs the boot code's copy,
#   boot_duart_early_init, which this image's boot code has not, as for
#   the Z3815A 4010.
#   installer_duartb_baud_from_settings, whose bytes image_diff finds at
#   installer::0x103ae6, has no row: they are a switch inside
#   installer_sci_reconfigure, which runs into them; nor has
#   installer_duarta_drain, whose one branch it finds at
#   installer::0x103d80, inside installer_console_write_polled, to its
#   loop: this image's installer reads nothing from DUART channel A, as
#   the Z3815A 4010's.
'''
# installer_duartb_baud_from_settings' bytes are a switch inside
# installer_sci_reconfigure (installer::0x103a6c), which runs into them;
# installer_duarta_drain's, its one branch, a branch inside
# installer_console_write_polled to its loop, as in the Z3815A 4010
DROPPED_FUNCTIONS_Z3816A = {('installer', 0x1030b6): 0x103ae6, ('installer', 0x103a14): 0x103d80}
NOT_CODE_Z3816A = frozenset({('primary', 0x581b4), ('primary', 0x581b6)})
NOT_CODE_TEXT_Z3816A = '''  Nor are the
#   words at 0x581b4 and 0x581b6, before psos_config_defaults, which the
#   NV descriptors point to, though image_diff finds them new.'''
PORT_Z3816A_4001 = Port(folder='z3816a-4001', base_folder='58503a-3704', second_base_folder='z3815a-4010',
                        pinned=PINNED_Z3816A, corrections=CORRECTIONS_Z3816A,
                        confirmed=frozenset(CORRECTIONS_Z3816A), corrections_text=CORRECTIONS_TEXT_Z3816A,
                        dropped_functions=DROPPED_FUNCTIONS_Z3816A, dropped=DROPPED_Z3816A, labels_text=LABELS_TEXT_Z3816A, not_code=NOT_CODE_Z3816A,
                        not_code_text=NOT_CODE_TEXT_Z3816A)
# The 58503B 1.01.04's port from the Z3817A 3752, its Oncore code the
# Z3816A 4001's where the Z3817A has none, whose matches carry more of
# what the Z3817A's find new than the 58503A 3704's
# (docs/firmware/58503b-1.01.04.md, "The base").
# functions whose few bytes match at more than one address, pinned where
# the 58503B's callers name them, as PINNED_Z3817A
PINNED_58503B = {
    ('primary', 0x47ca2): (
        0x47a8a, 0x4d124,
        "its 2 bytes, an rts, match more than one; pinned to 0x4d124, which output_state_done calls (0x4d18e), "
        "as the Z3817A's calls 0x47ca2"),
    ('primary', 0x47ca4): (
        0x47ad6, 0x4d126,
        "its 2 bytes, an rts, match more than one; pinned to 0x4d126, which pulse_settings_done calls (0x4d17c), "
        "as the Z3817A's calls 0x47ca4"),
    ('primary', 0x2e6fe): (
        0x2da92, 0x2dc38,
        "its 2 bytes, an rts, match more than one; pinned to 0x2dc38, which the SCI's set-up stores as its echo "
        "(0x2e5e0), as the Z3817A's sci_init stores 0x2e6fe; 0x2da92 is hdac_write's last instruction"),
}
LABELS_TEXT_58503B = '''.
#   The functions whose few bytes match at more than one address are
#   pinned where the 58503B's callers name them: output_state_program
#   and pulse_program, bare rts, at 0x4d124 and 0x4d126, which
#   output_state_done and pulse_settings_done call; and sci_echo_none
#   at 0x2dc38, which the SCI's set-up stores as its echo, where
#   image_diff pairs hdac_write's last rts.
#   nv_records_lost is dropped, though placed by its references, at
#   0x1011a2: that is the self-test record's eeprom field, which
#   nv_restore sets (0x42af4) and selftest_eeprom reads, where the
#   Z3817A keeps a word of its own.  So is duart_shadow, at 0x103c94:
#   this image has no DUART, nothing refers to 0x103c94 to 0x103c97,
#   and 0x103c98, where its matches' references fall, is acia_control,
#   the ACIA's control register's shadow, before acia_ring.
#   duartb_echo_none and n3_lock_offset_nothing, bare rts, are dropped,
#   though matched identical, at 0x2d4f2 and 0x4d194: each is another
#   routine's return, acia_isr's and output_state_done's, this image
#   having no DUART and no N3 lock to take them, and nothing calls
#   either; so is fit_evaluate_latest,
#   an rts the Z3817A's matches pair identical with 0x46c1c, the end of
#   the routine before it: fit_linear and its kin call 0x46c1e;
#   lamp_test_command, matched relocated at 0x28fac, the last three
#   instructions of 0x28fa6, the check selftest_print_failure's table
#   names "DISP" (0x288e4), which flashes the latch first;
#   gps_status_alternate, moveq #0, move.b and rts, at 0x4fc10, the last
#   three instructions of satellite_track_class, which branches to them,
#   and which nothing calls; and gps_restart, a call and rts, matched
#   relocated at 0x52ef2, gps_link_reset's last two instructions, its
#   call of request_ring_clear, which nothing calls: this image has no
#   timeref_switch to call it.
#   selftest_table is dropped at 0x2866e, where the Z3817A's eleven
#   entries' references place it: this image's has the Z3816A 4001's
#   fourteen, from 0x28662, a hand row.
'''
# labels the rules place that are dropped all the same: name to the
# 58503B address the rules give, which the header explains
DROPPED_58503B = {'nv_records_lost': 0x1011a2, 'duart_shadow': 0x103c94, 'selftest_table': 0x2866e}
# functions matched at another routine's last instructions, with no
# counterpart: the Z3817A's address to the 58503B's image_diff gives
DROPPED_FUNCTIONS_58503B = {('primary', 0x2d1a8): 0x2d4f2, ('primary', 0x4d6c8): 0x4d194,
                            ('primary', 0x46ec4): 0x46c1c, ('primary', 0x28baa): 0x28fac,
                            ('primary', 0x4fa06): 0x4fc10, ('primary', 0x52ab6): 0x52ef2}
# the pairings docs/firmware/58503b-1.01.04.md corrects, each confirmed by
# a caller's call, or the I/O table's entry, where the Z3817A's names the
# base function
CORRECTIONS_58503B = {
    ('primary', 0x2f0b8): (0x2e5f6, "%s: the I/O table's entry for device 0's control (0x5c060), where the "
                                    "Z3817A's names sci_reconfigure (0x592a4); this image's opens a frame for "
                                    "the status register it raises at its end, and image_diff pairs the code "
                                    "after its link, 0x2e5fa"
                           % 'docs/firmware/58503b-1.01.04.md'),
    ('primary', 0x230c2): (0x230c2, "%s: scpi_task_run's call for :SYSTem:LANGuage (0x38dd8), where the Z3817A's "
                                    "calls enter_installer; this image's deletes fpnl and blanks the display before "
                                    "trap #11, as the 58503A's does, and image_diff pairs the Z3817A's trap #11 "
                                    "alone with its last two instructions, 0x230ee, which nothing calls"
                           % 'docs/firmware/58503b-1.01.04.md'),
    ('primary', 0x2e67c): (0x2dba0, "%s: efc_write_dithered's call (0x32d28), where the Z3817A's calls efc_write; "
                                    "this image's pulses PE7 and reads cs7_window where the Z3817A's clears its "
                                    "strobe, and image_diff pairs 0x2dc20, a transfer without one"
                           % 'docs/firmware/58503b-1.01.04.md'),
    ('primary', 0x2e6e6): (0x2dc20, "%s: the transfer after qspi_fpga_word_write's, the Z3816A 4001's "
                                    "qspi_fpga_byte_write's instructions (image_diff, relocated), which nothing "
                                    "calls; image_diff pairs the Z3817A's with efc_write, 0x2dba0"
                           % 'docs/firmware/58503b-1.01.04.md'),
}
CORRECTIONS_TEXT_58503B = '''#   docs/firmware/58503b-1.01.04.md corrects four pairings and carries
#   them under their names: efc_write is 0x2dba0, which
#   efc_write_dithered calls, and qspi_fpga_byte_write 0x2dc20, the
#   transfer after qspi_fpga_word_write's (image_diff pairs efc_write
#   with 0x2dc20 and qspi_fpga_byte_write with 0x2dba0);
#   sci_reconfigure 0x2e5f6, the I/O table's entry for
#   device 0's control (image_diff pairs 0x2e5fa, the code after its
#   link); and enter_installer 0x230c2, which scpi_task_run calls
#   (image_diff pairs 0x230ee, its trap #11 and rts).
'''
PORT_58503B_1_01_04 = Port(folder='58503b-1.01.04', base_folder='z3817a-3752', second_base_folder='z3816a-4001',
                           pinned=PINNED_58503B, labels_text=LABELS_TEXT_58503B, dropped=DROPPED_58503B,
                           dropped_functions=DROPPED_FUNCTIONS_58503B,
                           corrections=CORRECTIONS_58503B, confirmed=frozenset(CORRECTIONS_58503B),
                           corrections_text=CORRECTIONS_TEXT_58503B)
# The Z3805A 1.01.03's port from the 58503B 1.01.04, the 1.01 line's
# other image, its Furuno code the Z3815A 4010's, whose matches carry
# the most of what the 58503B's find new
# (docs/firmware/z3805a-1.01.03.md, "The base").
# functions whose few bytes match at more than one address, pinned where
# the Z3805A 1.01.03's callers name them, as PINNED_58503B
PINNED_Z3805A_1_01_03 = {
    ('primary', 0x4d124): (
        0x488ba, 0x43308,
        "its 2 bytes, an rts, match more than one; pinned to 0x43308, which output_state_done calls (0x43372), "
        "as the 58503B's calls 0x4d124"),
    ('primary', 0x4d126): (
        0x48a02, 0x4330a,
        "its 2 bytes, an rts, match more than one; pinned to 0x4330a, which pulse_settings_done calls (0x43360), "
        "as the 58503B's calls 0x4d126"),
    ('primary', 0x2dce6): (
        0x1f7d6, 0x1f7cc,
        "image_diff pairs it past its test of a zero length, which the 58503B's leaves unnamed (0x2dcdc); pinned to "
        "0x1f7cc, which sci_write and sci_putc call (0x20270, 0x202d4)"),
    ('primary', 0x2e5fa): (
        0x200de, 0x200da,
        "image_diff pairs it past its link, which the 58503B's leaves unnamed (0x2e5f6); pinned to 0x200da, where "
        "the I/O table's entry jumps (0x55dd2)"),
    ('primary', 0x315e8): (
        0x230a4, 0x2309e,
        "image_diff pairs it past its first instruction, which clears oven_warmup_bypassed (0x2309e) and runs on "
        "into the rest; pinned to 0x2309e, which pps_monitors_reset calls (0x251bc)"),
}
LABELS_TEXT_Z3805A_1_01_03 = '''.
#   The functions whose few bytes match at more than one address are
#   pinned where the Z3805A 1.01.03's callers name them:
#   output_state_program and pulse_program, bare rts, at 0x43308 and
#   0x4330a, which output_state_done and pulse_settings_done call.
#   Two whose starts image_diff pairs a few instructions in are pinned
#   to their starts: sci_puts at 0x1f7cc, which sci_write and sci_putc
#   call, past its test of a zero length, and sci_reconfigure at
#   0x200da, where the I/O table's entry jumps, past its link, and
#   hmon_reset at 0x2309e, which pps_monitors_reset calls, past its
#   clear of oven_warmup_bypassed.
#   nmea_frame_any and nmea_encode_nothing, whose few bytes image_diff
#   finds at 0x431a2 and 0x51956, have no seeded row: those are the end
#   of get_ffom, which branches to it, and of long_array_get_byte,
#   which runs into it.  Hand rows name the two where the framer's
#   table (0x54ab4) and a sentence's encoders (0x54eec) point, 0x53ebc
#   and 0x52cdc.  fit_evaluate_latest, the 58503B's rts at 0x46c1c,
#   has no seeded row either: image_diff finds it at 0x424d0, the end
#   of the routine before; a hand row names the routine the aging fits
#   call, 0x424d2.
'''
# nmea_frame_any's and nmea_encode_nothing's bytes are the ends of
# get_ffom, which branches to them, and of long_array_get_byte, which
# runs into them; the 58503B's rts named fit_evaluate_latest is the end
# of the routine before 0x424d2, and so is the Z3815A's, the second
# base's, whose return_one image_diff finds at get_ffom's end too, where
# the 58503B's is the end of gps_request_admitted, which a hand row says
DROPPED_FUNCTIONS_Z3805A_1_01_03 = {('primary', 0x478ec): 0x431a2, ('primary', 0x56cfe): 0x51956,
                                    ('primary', 0x46c1c): 0x424d0}
SECOND_DROPPED_FUNCTIONS_Z3805A_1_01_03 = {('primary', 0x4b062): 0x424d0, ('primary', 0x5172c): 0x431a2}
NOT_CODE_Z3805A_1_01_03 = frozenset({('primary', 0x5588a)})
NOT_CODE_TEXT_Z3805A_1_01_03 = '''  Nor is
#   0x5588a, the last words before psos_config_defaults, a pointer to
#   psos_config among them, though image_diff finds it new.'''
# the 58503B's ACIA's registers and its control's shadow, which its
# matches' references place on this image's DUART (duart, 0x200000) and
# in duart_shadow (0x103e8e): this board has no ACIA
DROPPED_Z3805A_1_01_03 = {'acia_control': 0x103e92, 'acia_data': 0x200007, 'acia_status_control': 0x20000b}
# Its installer is the Z3805A 3543B's byte for byte, which names it in
# place of the 58503B's Vatican installer.
PORT_Z3805A_1_01_03 = Port(folder='z3805a-1.01.03', base_folder='58503b-1.01.04', second_base_folder='z3815a-4010',
                           installer_base_folder='z3805a-3543b', regions=frozenset({'primary'}),
                           pinned=PINNED_Z3805A_1_01_03, dropped_functions=DROPPED_FUNCTIONS_Z3805A_1_01_03,
                           second_dropped_functions=SECOND_DROPPED_FUNCTIONS_Z3805A_1_01_03,
                           dropped=DROPPED_Z3805A_1_01_03,
                           labels_text=LABELS_TEXT_Z3805A_1_01_03,
                           not_code=NOT_CODE_Z3805A_1_01_03, not_code_text=NOT_CODE_TEXT_Z3805A_1_01_03)
# Each seeded image's port, by its folder's name.
PORTS = {port.folder: port for port in (PORT_58503A_3704, PORT_Z3815A_4010, PORT_Z3801A_3543, PORT_Z3817A_3752,
                                        PORT_Z3816A_4001, PORT_58503B_1_01_04, PORT_Z3805A_1_01_03)}


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

    def __init__(self, base_folder, folder, port=None):
        self.base_folder, self.folder = base_folder, folder
        base_name = os.path.basename(base_folder)
        self.port = port or PORTS.get(os.path.basename(folder))
        self.second_chosen = set()
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
        base_memories, other_memories = image_diff.region_memories(base_image), image_diff.region_memories(other_image)
        regions = [('primary', base_memories['primary'], other_memories['primary'],
                    image_diff.handler_pairs([os.path.join(base_folder, 'handlers.txt'),
                                              os.path.join(folder, 'handlers.txt')])),
                   ('installer', base_memories['installer'], other_memories['installer'], ([], {}))]
        self.matchers, self.results = {}, {}
        for region, base, other, pairs in regions:
            matcher = image_diff.Matcher(base, other, items[region], starts[region], pairs, self.port.callee_ties)
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
        sources = [(region, v) for v in matcher.references] if region in self.port.regions else []
        if region == 'primary' and 'installer' in self.port.regions:
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
            elif (label.kind == 'label' and label.status in CARRIED and region in self.port.regions
                  and self.is_table(region, address)):
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
        _, records_extent = image_diff.installer_records(self.other_image)
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
            # another seeding gives a region not this one's, but for the RAM
            # labels this one's code places
            if region not in self.port.regions and not (kind == 'label' and (region, value) in self.label_target):
                continue
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
                    emit([show(region, other), kind, name, source, note_with(note, text + '; unconfirmed')],
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
                if region == 'primary' and records_extent[0] <= other <= records_extent[1]:
                    dropped["function matched in the installer's records"] += 1
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
                    emit([show(region, other), kind, name, source,
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

        out_rows = [self.installer_copy(row, self.primary_by_name(seen_names)) for row in out_rows]
        self.carried_rows = out_rows
        if self.port.installer_base_folder:
            out_rows = out_rows + self.installer_rows(seen_addresses, seen_names)
            weak_leads += self.installer_seeding.weak_leads

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
                     for region, found in self.results.items() if region in self.port.regions for item in found
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
        if self.port.installer_base_folder:
            new += [item for item in self.installer_seeding.new if show(item[0], item[1]) not in seen_addresses]
        new.sort()
        self.new, self.weak_leads = new, weak_leads

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

        second_rows = self.second_rows(seen_addresses, seen_names, new) if self.port.second_base_folder else []
        new = [item for item in new if show(item[0], item[1]) not in self.second_chosen]
        out_rows, new = self.with_hand_rows(out_rows, new, seen_names)
        if second_rows:
            out_rows += [self.port.text(SECOND_ROWS_HEADER).rstrip('\n')] + second_rows
        text = [self.port.text(HEADER), '\n'.join(cleaned) + '\n',
                INSTALLER_RECORDS % self.port.model + '\n' + '\t'.join(installer_row) + '\n#\n',
                '\n'.join(out_rows) + '\n',
                '#\n# New functions: the %s\'s functions no %s function or piece\n'
                % (self.port.model, self.port.unmatched_by) +
                '# matched (image_diff\'s "new"), and those found "called, by Ghidra\'s\n'
                '# analysis" that image_diff does not find, unnamed, for the rewrite\n'
                '# waves: entry rows, which Setup makes a function at, keeping\n'
                '# Ghidra\'s name, their note the extent in bytes and image_diff\'s\n'
                '# summary.  A wave that names one makes it a function row, or a\n'
                '# label row where it is data.\n']
        text += ['%s\t%s\t\t%s\t%d bytes; %s\n' % (show(region, address), ENTRY, ENTRY_SOURCE, size, summary)
                 for region, address, size, summary in new]
        text.append('#\n# Leads: functions image_diff pairs with a %s function as changed\n'
                    '# at a similarity below %.2f, a pairing too weak to name them by:\n'
                    '# address, the %s function, how it was paired and the similarity.\n'
                    % (self.port.unmatched_by, LEAD, self.port.unmatched_by))
        text += ['# %s\t%s\t%s\t%.3f\n' % (show(region, address), name, method, similarity)
                 for region, address, name, method, similarity in sorted(weak_leads)]
        if self.port.installer_base_folder:
            counts += self.installer_seeding.counts
            dropped += self.installer_seeding.dropped
            self.notes += ["installer's base: " + note for note in self.installer_seeding.notes]
        self.counts, self.dropped = counts, dropped
        return ''.join(text), carried

    def installer_rows(self, seen_addresses, seen_names):
        """The installer's base's rows (INSTALLER_HEADER), under their
        comment: its seeding's carried rows, but a function or label row
        at an address or under a name a row takes, each name and address
        taken added to seen_names and seen_addresses; a copy's primary
        found by its address in the installer's base's matches."""
        self.installer_seeding = Seeding(str(FIRMWARE / 'analysis' / self.port.installer_base_folder), self.folder,
                                         self.port.installer_port)
        self.installer_seeding.names()

        def primary_of(_, address):
            item = self.installer_seeding.code.get(('primary', address))
            if item is None or item.status not in CARRIED or item.other is None:
                return None
            fields = seen_addresses.get(show('primary', item.other))
            return (fields[2], item.other) if fields is not None and fields[1] == 'function' else None

        rows, comments = [], []
        for row in self.installer_seeding.carried_rows:
            if row.startswith('#'):
                comments.append(row)
                continue
            fields = row.split('\t')
            if fields[1] in ('function', 'label'):
                if fields[0] in seen_addresses or fields[2] in seen_names:
                    self.notes.append("installer's base: %s at %s taken" % (fields[2], fields[0]))
                    comments.clear()
                    continue
                seen_addresses[fields[0]], seen_names[fields[2]] = fields, fields[0]
            rows += comments + [self.installer_seeding.installer_copy(row, primary_of)]
            comments.clear()
        return [self.port.text(INSTALLER_ROWS_HEADER).rstrip('\n')] + rows

    def second_rows(self, seen_addresses, seen_names, new):
        """The second base's rows (SECOND_HEADER): its seeding's function
        rows at the functions the base's matches find new, unless a lead
        or under a name taken, their local rows, and its ROM tables at an
        address and under a name no row takes; each name and address
        taken added to seen_names and seen_addresses."""
        self.second_seeding = Seeding(str(FIRMWARE / 'analysis' / self.port.second_base_folder), self.folder,
                                      self.port.second_port)
        self.second_text, self.second_carried = self.second_seeding.names()
        origin = self.port.second_port.origin
        new_addresses = {show(region, address) for region, address, _, _ in new}
        seeded = [line.split('\t') for line in self.second_text.split('\n')
                  if not line.startswith('#') and len(line.split('\t')) > 4 and origin in line.split('\t')[4]]
        # a name a hand row places elsewhere is the hand row's
        hand_placed = {row.split('\t')[2]: row.split('\t')[0] for row in self.hand_rows()
                       if row.split('\t')[1] in ('function', 'label')}
        rows = []
        for fields in seeded:
            if fields[1] == 'function':
                if fields[0] not in new_addresses or 'a lead' in fields[4].split(origin)[-1]:
                    continue
                self.second_chosen.add(fields[0])
            elif fields[1] != 'label' or 'table, placed by' not in fields[4]:
                continue
            if hand_placed.get(fields[2], fields[0]) != fields[0]:
                self.second_chosen.discard(fields[0])
                self.notes.append('second base: %s at %s placed by hand at %s'
                                  % (fields[2], fields[0], hand_placed[fields[2]]))
                continue
            if fields[0] in seen_addresses or fields[2] in seen_names:
                self.second_chosen.discard(fields[0])
                self.notes.append('second base: %s at %s taken' % (fields[2], fields[0]))
                continue
            seen_addresses[fields[0]], seen_names[fields[2]] = fields, fields[0]
            rows.append(fields)
        rows += [fields for fields in seeded if fields[1] == 'local' and fields[0] in self.second_chosen]
        return ['\t'.join(fields) for fields in rows]

    def hand_rows(self):
        """The names file's rows added by hand, which carry HAND in
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
                sys.exit('%s: a row is added by hand or seeded, not both: %s' % (path, line))
            rows.append(line)
        return rows

    def with_hand_rows(self, out_rows, new, seen_names):
        """The carried rows and the new functions with the hand rows in
        them: each in place of the carried row of its address and kind,
        or of the function at its address, carried or new, which a
        function row names and a label row says is data; the rest
        after the carried rows, under a comment of their own.  A hand row
        whose name the matches seed elsewhere fails, but in a second
        base's seeding, whose rows are taken only under names no row of
        the first base's takes (second_rows), so that its own pairing of
        a name, at another routine's end, say, is no conflict: the first
        base's seeding checks the hand rows against the names it takes."""
        by_place = {place_key(row.split('\t')): row for row in self.hand_rows()}
        for row in [] if self.port.second else self.hand_rows():
            fields = row.split('\t')
            seeded = seen_names.get(fields[2])
            if (fields[1] in ('function', 'label') and seeded not in (None, fields[0])
                    and (seeded, fields[1]) not in by_place and (seeded, 'label') not in by_place):
                sys.exit('%s: added by hand at %s, seeded at %s' % (fields[2], fields[0], seeded))
        placed, rows = set(), []
        for row in out_rows:
            key = place_key(row.split('\t'))
            # a label row says a carried function's place is data, as
            # it does a new function's
            if key[1:] == ('function',) and key not in by_place and (key[0], 'label') in by_place:
                key = (key[0], 'label')
                if key in placed:
                    continue
            if key in by_place:
                placed.add(key)
            rows.append(by_place.get(key, row))
        # a function row names the new function at its address; a label
        # row there says image_diff took data for one
        named = {key[0] for key in by_place if key[1] in ('function', 'label')}
        new = [item for item in new if show(item[0], item[1]) not in named]
        rest = [row for key, row in by_place.items() if key not in placed]
        if rest:
            rows += ['#', '# Rows added by hand that no carried row is in place of'] + rest
        return rows, new

    @staticmethod
    def primary_by_name(seen_names):
        """The lookup installer_copy takes: a copy's primary, by the
        base's name and address for it, to its name and address in the
        image, where it is carried under that name, or None."""
        def lookup(name, _):
            address = seen_names.get(name)
            return None if address is None else (name, int(address, 16))
        return lookup

    def installer_copy(self, row, primary_of):
        """The row, an installer copy's note giving where it is relative
        to its primary in the image, where primary_of finds the primary;
        a copy said to be byte for byte is checked so."""
        fields = row.split('\t')
        found = INSTALLER_COPY.search(fields[4]) if len(fields) > 4 and fields[1] == 'function' else None
        if found is None:
            return row
        primary = primary_of(found.group(1), int(found.group(2), 16))
        if primary is None:
            return row
        name, start = primary
        copy = image_diff.parse_address(fields[0])[1]
        if found.group(4) == ', byte for byte':
            item = self.code['primary', int(found.group(2), 16)]
            matcher = self.matchers['installer']
            if matcher.other.bytes(copy, copy + item.size) != self.other_image[start:start + item.size]:
                sys.exit('%s: not byte for byte its primary at 0x%x' % (fields[2], start))
        fields[4] = fields[4][:found.start()] + "the installer's copy of %s, at 0x%x plus 0x%x in this image%s" % (
            name, start, copy - start, found.group(4) or '') + fields[4][found.end():]
        return '\t'.join(fields)

    def modules(self, carried, names):
        """The modules file's text: each base row re-keyed, under the name
        the names file's text, names, gives its function."""
        named = {fields[0]: fields[2] for fields in (line.split('\t') for line in names.split('\n'))
                 if not fields[0].startswith('#') and len(fields) > 2 and fields[1] == 'function'}
        rows = [self.port.text(MODULES_HEADER)]
        corrected_others = {(r, o) for (r, _), (o, _) in self.port.corrections.items()}
        lines = open(os.path.join(self.base_folder, 'modules.tsv')).read().split('\n')
        for line in lines:
            if not line.strip() or line.startswith('#'):
                continue
            f = line.split('\t')
            region, value = image_diff.parse_address(f[0])
            if (region, value) in self.port.dropped_functions:
                continue
            item = self.code.get((region, value))
            seeded = carried.get((region, value))
            if seeded is not None:
                other = seeded[1]
                name = named.get(show(region, other), f[1])
            elif item is not None and item.other is not None and (
                    self.counted(item) or (item.status == 'changed' and item.similarity >= LEAD)):
                # a lead named by Setup (an SCPI handler): its module still a hint
                other, name = item.other, f[1]
            else:
                continue
            if (region, other) in corrected_others and seeded is None:
                continue
            rows.append('\t'.join([show(region, other), name] + f[2:]) + '\n')
        if self.second_chosen:
            second = self.second_seeding.modules(self.second_carried, self.second_text)
            chosen = [row + '\n' for row in second.split('\n')
                      if not row.startswith('#') and row.split('\t')[0] in self.second_chosen]
            if chosen:
                rows.append(self.port.text(MODULES_SECOND_HEADER))
                rows += chosen
        # a hand row at a seeded row's address corrects it in place
        by_address = {row.split('\t')[0]: row + '\n' for row in self.hand_module_rows()}
        rows = rows[:1] + [by_address.pop(row.split('\t')[0], row) for row in rows[1:]]
        hand = list(by_address.values())
        if hand:
            rows.append(self.port.text(MODULES_HAND_HEADER))
            rows += hand
        return ''.join(rows)

    def hand_module_rows(self):
        """The modules file's rows added by hand, which carry HAND in
        their reason: corrections of seeded rows and the image's own
        functions, which no base row re-keys, in the file's order."""
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
    outputs = {'names.tsv': names, 'modules.tsv': seeding.modules(carried, names)}
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
    if not hand_figures(seeding.port, outputs, arguments.write):
        stale = True
    if stale:
        sys.exit('%s: not what the matches give; make seed-names rewrites them' % arguments.folder)


def hand_figures(port, outputs, write):
    """Checks the port's document's counts of the hand rows against the
    files' text, or with write rewrites them; whether they agree."""
    path = ROOT / port.document
    text = path.read_text()
    agree = True
    for file_name, pattern in port.hand_figures:
        rows = sum(1 for line in outputs[file_name].split('\n')
                   if line and not line.startswith('#') and port.hand in line)
        matches = list(re.finditer(pattern, text))
        if len(matches) != 1:
            sys.exit('%s: "%s" found %d times' % (path, pattern, len(matches)))
        have = matches[0].group(1)
        if have == str(rows):
            continue
        agree = write
        print('%s: %s hand rows in %s, not %s' % (path, rows, file_name, have))
        text = text[:matches[0].start(1)] + str(rows) + text[matches[0].end(1):]
    if write and text != path.read_text():
        path.write_text(text)
    return agree


if __name__ == '__main__':
    main()
