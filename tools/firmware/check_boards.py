"""Checks each board file's chip-select attachments (boards/*.toml)
against the chip-select maps chip_selects.py reads from its images'
reset code.

    python3 -I tools/firmware/check_boards.py

For every image a board names, and every map of that image (the boot
code's and the primary's), each select attached by role and lane must be
enabled and programmed for them:

- read, write, read-write: R/W read, write, or read and write;
- lane upper or lower: BYTE that lane, or both (a part on one half of
  the bus); lane both (the default): on a 16-bit port BYTE both (on an
  8-bit port any BYTE but disabled, 4.8.1.3);
- iack: an interrupt acknowledge in CPU space (4.8.3), whatever its
  lane.

Every select a map enables must be attached, or listed under
[[unconnected]], and an [[unconnected]] select must not be attached
too.  A mismatch in the primary's map fails; one in a boot code's map
is reported, since a boot map may program a select for what the board
does not have there (the Z3815A's CS7), and a select a boot map leaves
disabled is not one.
"""

import pathlib
import sys
import tomllib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import chip_selects  # noqa: E402

BOARDS = chip_selects.ROOT / 'boards'
ROLES = ('read', 'write', 'read-write', 'iack')
# docs/board-files.md (mc683xx-board): an attachment's role, unless it
# gives one; a decoder takes its select read and write
DEFAULT_ROLE = 'read-write'
# Table D-12's R/W field, by role
READ_WRITE = {'read': 1, 'write': 2, 'read-write': 3}
# Table D-12's BYTE field, by lane
LANE = {'lower': 1, 'upper': 2}
DEFAULT_LANE = 'both'
BYTE_BOTH = 3
PRIMARY = 'primary'
NOT_ENABLED = 'not enabled'


def attachments(board):
    """Each select attachment in a board file, as (select, role, lane, owner)."""
    found = [(item['select'], DEFAULT_ROLE, DEFAULT_LANE, item['name']) for item in board.get('decoder', [])]
    for section in ('memory', 'peripheral'):
        for item in board.get(section, []):
            for attach in item.get('attach', []):
                if 'select' in attach:
                    found.append((attach['select'], attach.get('role', DEFAULT_ROLE),
                                  attach.get('lane', DEFAULT_LANE), item['name']))
    return found


def select_state(item, select):
    """A select in a map: (enabled, iack, BYTE, R/W, 16-bit port)."""
    bar_name, or_name = chip_selects.select_registers(select)
    bar = item.registers[bar_name] or 0
    option = item.registers[or_name] or 0
    size = chip_selects.BLOCK_SIZES[bar & chip_selects.BLOCK_MASK]
    base = ((bar & chip_selects.BASE_MASK) << chip_selects.BASE_SHIFT) & ~(size - 1)
    byte = (option >> chip_selects.BYTE_SHIFT) & chip_selects.TWO_BITS
    read_write = (option >> chip_selects.RW_SHIFT) & chip_selects.TWO_BITS
    space = (option >> chip_selects.SPACE_SHIFT) & chip_selects.TWO_BITS
    iack = (space == chip_selects.SPACE_CPU and base & chip_selects.CPU_SPACE_TYPE == chip_selects.CPU_SPACE_TYPE
            and size <= chip_selects.CPU_SPACE_LARGEST)
    port = chip_selects.port(item, select)
    enabled = byte != chip_selects.BYTE_DISABLED and port in ('8-bit', '16-bit', 'from the DATA lines at reset (4.8.4)')
    return enabled, iack, byte, read_write, port != '8-bit'


def role_problem(item, select, role, lane):
    """Why a map does not program a select for a role and lane, or None."""
    enabled, iack, byte, read_write, wide = select_state(item, select)
    if not enabled:
        return NOT_ENABLED
    if role == 'iack':
        return None if iack else 'not an interrupt acknowledge'
    if iack:
        return 'an interrupt acknowledge'
    if read_write != READ_WRITE[role]:
        return 'R/W %s' % chip_selects.READ_WRITE[read_write]
    if lane in LANE:
        return None if byte in (LANE[lane], BYTE_BOTH) else 'BYTE %s' % chip_selects.BYTES[byte]
    if wide and byte != BYTE_BOTH:
        return 'BYTE %s on a 16-bit port' % chip_selects.BYTES[byte]
    return None


def check_board(path, images):
    """A board file's failures and warnings."""
    board = tomllib.loads(path.read_text())
    where = path.relative_to(chip_selects.ROOT)
    failures, warnings = [], []
    attached = attachments(board)
    unconnected = [entry['select'] for entry in board.get('unconnected', [])]
    selects = chip_selects.select_names()
    for select, role, lane, owner in attached:
        if select not in selects or role not in ROLES or (lane != DEFAULT_LANE and lane not in LANE):
            failures.append('%s: %s: no select %s, role %s or lane %s' % (where, owner, select, role, lane))
    for select in unconnected:
        if select in (a[0] for a in attached):
            failures.append('%s: %s is both attached and unconnected' % (where, select))
    for image in board['board']['images']:
        stem = pathlib.Path(image).stem
        if stem not in images:
            failures.append('%s: no image %s' % (where, image))
            continue
        for item in images[stem]:
            out = failures if item.key == PRIMARY else warnings
            label = '%s: %s %s' % (where, stem, item.key)
            for select, role, lane, owner in attached:
                problem = role_problem(item, select, role, lane)
                if problem == NOT_ENABLED and item.key != PRIMARY:
                    continue
                if problem:
                    out.append('%s: %s %s for %s is %s' % (label, select, role, owner, problem))
            listed = {a[0] for a in attached} | set(unconnected)
            for select in selects:
                if select not in listed and select_state(item, select)[0]:
                    out.append('%s: %s is enabled and not in the board' % (label, select))
    return failures, warnings


def main():
    images = chip_selects.every_image()
    paths = sorted(BOARDS.glob('*.toml'))
    if not paths:
        sys.exit('%s: no board files' % BOARDS)
    failures, warnings = [], []
    for path in paths:
        board_failures, board_warnings = check_board(path, images)
        failures += board_failures
        warnings += board_warnings
    for line in warnings:
        print('warning: ' + line)
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
