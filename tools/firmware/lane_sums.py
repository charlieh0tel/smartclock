"""The boot checksum of the receivers whose flash is AMD parts in
word-interleaved pairs: two 16-bit sums over a span of a bank, one of
its even-address bytes and one of its odd, each modulo 65536, stored in
the bank's last four bytes interleaved as the lanes hold them: the even
sum's high byte, the odd sum's, the even sum's low byte, the odd sum's
(docs/firmware/restart.md, "The installer", Boot check;
crates/smartclock-cli/src/flash/firmware.rs, Layout::AmdLanes).
"""

# The stored sums: a bank's last four bytes, which no sum covers.
STORED_SIZE = 4
BYTE_BITS = 8
SUM_MASK = 0xFFFF


def lane_sums(image: bytes, start: int, end: int) -> tuple[tuple[int, int], tuple[int, int]]:
    """The even and odd byte sums of image[start:end] but its last four
    bytes, and the two those four bytes hold; start even."""
    span = image[start : end - STORED_SIZE]
    even, odd = sum(span[0::2]) & SUM_MASK, sum(span[1::2]) & SUM_MASK
    stored = image[end - STORED_SIZE : end]
    return (even, odd), (stored[0] << BYTE_BITS | stored[2], stored[1] << BYTE_BITS | stored[3])
