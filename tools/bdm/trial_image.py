# /// script
# requires-python = ">=3.11"
# ///
"""Makes a bench trial's image: a whole-flash image with one word
changed and its lane sums fixed, so that a run writes the word's sector
pair and the one holding the sums (tools/bdm/README.md, "A bench
trial").

    python3 -I tools/bdm/trial_image.py IMAGE OUT [--address ADDRESS] [--word WORD]

By default the word is the first of the sector pair at 0x70000, blank
in the 58503A 3704 image.  OUT must not exist.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from board import FLASH_SIZE, WORD  # noqa: E402
from flash_host import LANE_SUM_SPANS, Refused, check_image, with_lane_sums  # noqa: E402
from lane_sums import STORED_SIZE  # noqa: E402

TRIAL_ADDRESS = 0x70000
TRIAL_WORD = 0x0000


def trial(image: bytes, address: int = TRIAL_ADDRESS, word: int = TRIAL_WORD) -> bytes:
    """image with word at address and the lane sums fixed; refused for
    an address odd, past the flash or in the sums, or holding word
    already."""
    check_image(image)
    sums = [end - STORED_SIZE for _, end in LANE_SUM_SPANS]
    if address % WORD or not 0 <= address < FLASH_SIZE or any(at <= address < at + STORED_SIZE for at in sums):
        raise Refused(f"{address:#x} is not an even address of the flash outside the sums")
    old = int.from_bytes(image[address : address + WORD], "big")
    if old == word:
        raise Refused(f"{address:#x} already holds {word:#06x}")
    changed = bytearray(image)
    changed[address : address + WORD] = word.to_bytes(WORD, "big")
    return with_lane_sums(bytes(changed))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("image", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--address", type=lambda text: int(text, 0), default=TRIAL_ADDRESS)
    parser.add_argument("--word", type=lambda text: int(text, 0), default=TRIAL_WORD)
    arguments = parser.parse_args()
    try:
        changed = trial(arguments.image.read_bytes(), arguments.address, arguments.word)
        with arguments.out.open("xb") as out:
            out.write(changed)
    except (Refused, OSError) as error:
        raise SystemExit(f"refused: {error}") from error
    print(f"{arguments.out}: {arguments.word:#06x} at {arguments.address:#x}, lane sums fixed")


if __name__ == "__main__":
    main()
