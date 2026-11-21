"""The 58503A's flash as its board wires it, shared by the host
(flash_host.py) and the flash model (am29f010.py): two pairs of
Am29F010s, word-interleaved, each pair read through one select (the
parts' output enable) and written through another (their write
enable) (docs/firmware/restart.md, "The installer";
docs/firmware/chip-selects.md, 58503A 3704; output enables measured
by continuity, 2026-11-21).  flash_routine.S states the geometry for
itself; test_flash.py holds the two to agree.
"""

FLASH_SIZE = 0x80000
PAIR_SIZE = FLASH_SIZE // 2
# A part's 16 KiB sector, both parts' across the bus (Am29F010B,
# Table 2, p. 9): what one sector erase command clears.
SECTOR_PAIR_SIZE = 0x8000
WORD = 2
# Each select's pair, 0 at 0x0 and 1 at 0x40000.
READ_SELECTS = {"CSBOOT": 0, "CS1": 1}
WRITE_SELECTS = {"CS6": 0, "CS7": 1}
