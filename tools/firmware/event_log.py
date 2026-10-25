"""The event log's pages in the EEPROM, as log.c and smartclock.h define
them (log_page, log_entry): where the pages and an entry's argument
longs lie.

An entry keeps its format by address, the image's own text
(log.c), and log_read prints it with the entry's two argument longs
through doprnt (libc.h's libc_doprnt), which takes from them what the
format's conversions consume and no more.  What lies past that is
whatever the caller left on the stack: log_event pushes only the
format (0x31a0a), so its entries' arguments are its own return address
and the long holding its char, each copy's own.
"""

# The EEPROM, chip select 9's 8 KiB from 0x400000 (CSBAR9 0x4001).
EEPROM_BASE = 0x400000
# The event log: RECORDS_LOG_PAGES pages from 0x400d40 (log.c,
# LOG_EEPROM_PAGES).
LOG_EEPROM_PAGES = 0x400D40
LOG_PAGES = 75
# log_page: a write count, a checksum, then three log_entry.
LOG_PAGE_SIZE = 64
LOG_PAGE_CHECKSUM = range(2, 4)
LOG_PAGE_ENTRIES = 4
LOG_ENTRIES = 3
LOG_ENTRY_SIZE = 20
# log_entry: time, date, format, argument_1, argument_2, a long each.
LOG_ENTRY_ARGUMENTS = (12, 16)
LONG = 4


def page_offsets() -> range:
    """Each event log page's offset in the EEPROM's dump."""
    start = LOG_EEPROM_PAGES - EEPROM_BASE
    return range(start, start + LOG_PAGES * LOG_PAGE_SIZE, LOG_PAGE_SIZE)


def entry_argument_offsets(entry: int) -> list[range]:
    """An entry's argument longs, by their offsets in its page."""
    base = LOG_PAGE_ENTRIES + LOG_ENTRY_SIZE * entry
    return [range(base + at, base + at + LONG) for at in LOG_ENTRY_ARGUMENTS]
