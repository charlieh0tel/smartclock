# /// script
# requires-python = ">=3.12"
# dependencies = ["mc683xx==0.9.0"]
# ///
"""Tests of the BDM flash routine and its host on the model target:
the routine as CPU32 code on mc683xx's MC68331, its BDM and SIM, the
board's selects decoding the flash model (model_target.py,
am29f010.py).  mc683xx is pinned as the firmware's checks pin it.

    uv run -q tools/bdm/test_flash.py

`make test-bdm` runs it.  It assembles the routine first
(build_routine.py), with the m68k binutils.
"""

import contextlib
import hashlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_routine
import flash_host
import trial_image
from am29f010 import DQ5, DQ7, PART_SIZE, BoardFlash, Hazard, State
from board import FLASH_SIZE, PAIR_SIZE, SECTOR_PAIR_SIZE, WORD
from model_target import WINDOW_SECONDS, ModelTarget, clocks, timing
from flash_host import (
    BOOT_REGION_END,
    BUFFER,
    CHIP_SELECTS,
    Busy,
    Failed,
    Options,
    Refused,
    Routine,
    Session,
    hazards,
    with_lane_sums,
)

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "third_party" / "firmware" / "images"
IMAGE_3704 = (IMAGES / "58503a-3704.bin").read_bytes()
SHA256_3704 = "d13b9ff1e4a0a59517aac4d066c60e22b290cf4ff6810c5c2bf01f1bc9491ca3"
BLANK = bytes([0xFF]) * FLASH_SIZE
# The longest a run's erase may take in the tests: the routine's limit.
TEST_ERASE_LIMIT = 0.05


def variant(image: bytes, base: int) -> bytes:
    """image with the sector pair at base changed, bits both cleared and
    set, and the sums fixed."""
    changed = bytearray(image)
    for offset in range(0, SECTOR_PAIR_SIZE, 7):
        changed[base + offset] ^= 0xA5
    return with_lane_sums(bytes(changed))


# The routine, assembled afresh.
ROUTINE = Routine.load(build_routine.build())


def board(image: bytes = IMAGE_3704, part_timing=None, **flash) -> tuple[ModelTarget, BoardFlash]:
    model = BoardFlash.from_image(image, part_timing or timing(), **flash)
    return ModelTarget(model), model


def quiet(_: str) -> None:
    pass


def run(target: ModelTarget, image: bytes, **options) -> flash_host.Report:
    options.setdefault("erase_time", TEST_ERASE_LIMIT)
    return flash_host.flash(target, image, Options(**options), ROUTINE, quiet)


def session(target: ModelTarget, **keywords) -> Session:
    built = Session(target, ROUTINE, quiet, Options(erase_time=TEST_ERASE_LIMIT), **keywords)
    built.set_up()
    return built


def clean(test: unittest.TestCase, model: BoardFlash) -> None:
    """No hazard, nothing unmodeled, every part reading array data."""
    test.assertEqual(model.hazards, [])
    test.assertEqual(model.unmodeled(), [])
    test.assertEqual([part.state for part in model.parts()], [State.READ] * 4)


class Routines(unittest.TestCase):
    def test_built_position_independent(self):
        # build_routine refuses relocations; the entries are where the
        # host's vector table and PC point
        self.assertEqual(len(ROUTINE.code), ROUTINE.labels["routine_end"])
        self.assertIn("exception", ROUTINE.labels)
        listing = (build_routine.FOLDER / "flash_routine.lst").read_text()
        self.assertIn("bgnd", listing)

    def test_geometry_agrees(self):
        # flash_routine.S states the board's geometry for itself
        self.assertEqual(
            (ROUTINE.FLASH_END, ROUTINE.PAIR_SIZE, ROUTINE.SECTOR_PAIR_SIZE, ROUTINE.PAIR_2_BASE),
            (FLASH_SIZE, PAIR_SIZE, SECTOR_PAIR_SIZE, PAIR_SIZE),
        )

    def test_identify(self):
        target, model = board()
        built = session(target)
        protection = built.identify()
        self.assertEqual(set(protection.values()), {0})
        self.assertEqual(len(protection), FLASH_SIZE // SECTOR_PAIR_SIZE)
        clean(self, model)
        self.assertEqual(model.image(), IMAGE_3704)

    def test_bad_operation_and_arguments(self):
        target, model = board()
        built = session(target)
        for operation, address, count, floor, status in (
            (99, 0x40000, 1, BOOT_REGION_END, "STATUS_BAD_OPERATION"),
            (ROUTINE.OPERATION_ERASE, 0x8000, 0, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # below the floor
            (ROUTINE.OPERATION_ERASE, 0x70002, 0, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # not a sector pair
            (ROUTINE.OPERATION_PROGRAM, 0x70001, 1, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # odd
            (ROUTINE.OPERATION_PROGRAM, 0x3FFFE, 2, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # across pairs
            (ROUTINE.OPERATION_PROGRAM, 0x80000, 1, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # past the flash
            (ROUTINE.OPERATION_VERIFY, 0x70000, 0, BOOT_REGION_END, "STATUS_BAD_ARGUMENT"),  # no words
        ):
            with self.assertRaisesRegex(Failed, status):
                built.call(operation, address, count, floor, 1, 1)
        clean(self, model)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        self.assertEqual(model.image(), IMAGE_3704)

    def test_needs_erase_refused_before_programming(self):
        target, model = board()
        built = session(target)
        # 0x7fffc holds the sums, 0xbcae: asking 0xffff there needs a 0 made 1
        target.write_block(BUFFER, bytes([0x00, 0x00, 0xFF, 0xFF]))
        with self.assertRaisesRegex(Failed, r"STATUS_NEEDS_ERASE at 0x7fffe: read 0x0c74, expected 0xffff"):
            built.call(ROUTINE.OPERATION_PROGRAM, 0x7FFFC, 2, BOOT_REGION_END, 100, 100)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        clean(self, model)

    def test_program_skips_equal_words(self):
        target, model = board()
        built = session(target)
        target.write_block(BUFFER, IMAGE_3704[0x40000 : 0x40000 + SECTOR_PAIR_SIZE])
        built.call(ROUTINE.OPERATION_PROGRAM, 0x40000, SECTOR_PAIR_SIZE // WORD, BOOT_REGION_END, 100, 100 * SECTOR_PAIR_SIZE)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        clean(self, model)

    def test_verify_mismatch(self):
        target, model = board()
        built = session(target)
        data = bytearray(IMAGE_3704[0x48000 : 0x48000 + SECTOR_PAIR_SIZE])
        data[0x1235] ^= 1
        target.write_block(BUFFER, bytes(data))
        with self.assertRaisesRegex(Failed, r"STATUS_MISMATCH at 0x49234"):
            built.call(ROUTINE.OPERATION_VERIFY, 0x48000, SECTOR_PAIR_SIZE // WORD, BOOT_REGION_END, 1, SECTOR_PAIR_SIZE)
        clean(self, model)

    def test_lanes_finish_apart(self):
        # pair 2's even part slow to program, its odd part slow to erase
        parts = [timing(), timing(), timing(program=250e-6), timing(erase=8e-3)]
        image = variant(IMAGE_3704, 0x50000)
        target, model = board(part_timing=parts)
        report = run(target, image)
        self.assertEqual(report.plan, [0x50000, 0x78000])
        self.assertEqual(model.image(), image)
        clean(self, model)

    def test_stuck_lane_erase(self):
        parts = [timing(), timing(), timing(), timing(stuck=True)]
        target, model = board(part_timing=parts)
        with self.assertRaisesRegex(Failed, r"STATUS_EXCEEDED at 0x50000: read 0xff([0-9a-f]{2}), expected 0xffff") as caught:
            run(target, variant(IMAGE_3704, 0x50000))
        # the even lane done, the odd still erasing (DQ7 0) with DQ5
        odd = int(caught.exception.args[0].split("read 0xff")[1][:2], 16)
        self.assertEqual(odd & (DQ7 | DQ5), DQ5)
        # the routine's read/reset took the stuck part out of DQ5
        clean(self, model)

    def test_stuck_lane_program(self):
        # pair 2's even part stuck; 0x70000 erased in the 3704 image
        target, model = board()
        model.pairs[1][0].timing = timing(stuck=True)
        built = session(target)
        target.write_block(BUFFER, bytes([0x12, 0x34]))
        with self.assertRaisesRegex(Failed, r"STATUS_EXCEEDED at 0x70000: read 0x[0-9a-f]{4}, expected 0x1234"):
            built.call(ROUTINE.OPERATION_PROGRAM, 0x70000, 1, BOOT_REGION_END, built.limit(1e-3), 10**5)
        clean(self, model)

    def test_timeout(self):
        # a part that neither finishes nor shows DQ5 in the routine's time
        parts = [timing(), timing(), timing(erase=1.0), timing()]
        target, model = board(part_timing=parts)
        with self.assertRaisesRegex(Busy, r"STATUS_TIMEOUT at 0x50000"):
            run(target, variant(IMAGE_3704, 0x50000), erase_time=0.01)

    def test_exception_returns_through_vector_table(self):
        target, model = board()
        target.peripheral.bus_errors = frozenset([0x50000])
        built = session(target)
        with self.assertRaisesRegex(Failed, r"exception, vector 2, PC 0x10[0-9a-f]{4}, fault address 0x50000"):
            built.call(ROUTINE.OPERATION_ERASE, 0x50000, 0, BOOT_REGION_END, built.limit(TEST_ERASE_LIMIT), 10**6)
        self.assertEqual(model.hazards, [])

    def test_clock_bounds(self):
        # the routine's poll limit rests on POLL_CLOCKS_MIN, the host's
        # wait on STEP_CLOCKS_MAX: hold them to mc683xx's clocks
        erase = 5e-3
        target, model = board(part_timing=timing(erase=erase))
        built = session(target)
        pair = model.pairs[1]
        built.call(ROUTINE.OPERATION_ERASE, 0x70000, 0, BOOT_REGION_END, built.limit(TEST_ERASE_LIMIT), 10**6)
        polls = sum(part.status_reads for part in pair) // WORD
        self.assertGreaterEqual(clocks(WINDOW_SECONDS + erase) / polls, flash_host.POLL_CLOCKS_MIN)
        image = variant(IMAGE_3704, 0x70000)
        target.write_block(BUFFER, image[0x70000 : 0x70000 + SECTOR_PAIR_SIZE])
        before, start = sum(part.status_reads for part in pair), target.chip.now
        words = SECTOR_PAIR_SIZE // WORD
        built.call(ROUTINE.OPERATION_PROGRAM, 0x70000, words, BOOT_REGION_END, built.limit(1e-3), 10**7)
        polls = (sum(part.status_reads for part in pair) - before) // WORD
        steps = words * flash_host.PASSES_PER_WORD + polls
        self.assertLessEqual(target.chip.now - start, steps * flash_host.STEP_CLOCKS_MAX)


class Procedure(unittest.TestCase):
    def test_from_blank(self):
        target, model = board(BLANK)
        report = run(target, IMAGE_3704)
        # all but the image's two blank sector pairs
        self.assertEqual(report.plan, [base for base in range(0, FLASH_SIZE, SECTOR_PAIR_SIZE) if base not in (0x68000, 0x70000)])
        self.assertEqual(report.final_sha256, SHA256_3704)
        self.assertEqual(report.dump_sha256, hashlib.sha256(BLANK).hexdigest())
        self.assertEqual(model.image(), IMAGE_3704)
        clean(self, model)

    def test_from_another_image(self):
        image = variant(variant(IMAGE_3704, 0x10000), 0x68000)
        target, model = board()
        report = run(target, image)
        self.assertEqual(report.plan, [0x10000, 0x38000, 0x68000, 0x78000])
        self.assertEqual(report.dump_sha256, SHA256_3704)
        self.assertEqual(model.image(), image)
        clean(self, model)

    def test_from_3633(self):
        # the 58503A's other image differs in the installer too: written
        # whole, the boot region among it
        image_3633 = (IMAGES / "58503a-3633.bin").read_bytes()
        target, model = board(image_3633)
        report = run(target, IMAGE_3704)
        self.assertEqual(report.plan[:2], [0x0, 0x8000])
        self.assertEqual(report.final_sha256, SHA256_3704)
        clean(self, model)

    def test_dry_run(self):
        target, model = board()
        report = run(target, variant(IMAGE_3704, 0x60000), dry_run=True)
        self.assertEqual(report.plan, [0x60000, 0x78000])
        self.assertEqual(model.image(), IMAGE_3704)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        self.assertEqual(target.runs, 0)
        clean(self, model)

    def test_writes_the_boot_region(self):
        # a word the boot checksum does not cover: that sector pair alone
        image = bytearray(IMAGE_3704)
        image[0x8000] ^= 0xFF
        target, model = board()
        report = run(target, bytes(image))
        self.assertEqual(report.plan, [0x8000])
        self.assertEqual(model.image(), bytes(image))
        clean(self, model)

    def test_refuses_short_image(self):
        target, model = board()
        with self.assertRaisesRegex(Refused, "not 524288"):
            run(target, IMAGE_3704[:-2])
        self.assertEqual(target.runs, 0)

    def test_bad_sums_reported_and_written(self):
        image = bytearray(IMAGE_3704)
        image[0x50000] ^= 1
        target, model = board()
        lines: list[str] = []
        flash_host.flash(target, bytes(image), Options(erase_time=TEST_ERASE_LIMIT), ROUTINE, lines.append)
        self.assertIn("image lane sums of 0x10000-0x40000 hold", lines)
        self.assertTrue(any(line.startswith("image lane sums of 0x40000-0x80000 DO NOT HOLD") for line in lines))
        self.assertIn("WARNING: an image whose lane sums fail boots into the installer, not the primary", lines)
        self.assertEqual(model.image(), bytes(image))
        clean(self, model)

    def test_refuses_unexpected_ids(self):
        target, model = board()
        model.pairs[1][1].device = 0xA4  # an Am29F040's code
        with self.assertRaisesRegex(Refused, "not two AMD Am29F010s: 0x0101 0x20a4"):
            run(target, variant(IMAGE_3704, 0x50000))
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        clean(self, model)

    def test_refuses_protected_sector(self):
        # pair 2's odd part's sector 2, the sector pair at 0x50000
        target, model = board()
        model.pairs[1][1].protected = frozenset([2])
        with self.assertRaisesRegex(Refused, "protected: 0x50000"):
            run(target, variant(IMAGE_3704, 0x50000))
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)

    def test_watchdog_needs_sypcr(self):
        # without SYPCR written the watchdog resets the CPU 2^9 crystal
        # periods into the routine, before a 20 ms erase completes
        target, model = board(part_timing=timing(erase=20e-3))
        image = variant(IMAGE_3704, 0x50000)
        built = Session(target, ROUTINE, quiet, Options(erase_time=TEST_ERASE_LIMIT), write_sypcr=False)
        with self.assertRaisesRegex(Failed, "reset while running"):
            flash_host.flash(target, image, Options(erase_time=TEST_ERASE_LIMIT), ROUTINE, quiet, built)


def entering(target: ModelTarget, writes: tuple[tuple[int, int, int], ...]) -> ModelTarget:
    """target, its reset entry followed by writes (address, width,
    value): an entry that is not a fresh reset's."""
    reset = target.reset_into_bdm

    def reset_and_write() -> None:
        reset()
        for address, width, value in writes:
            target.write_memory(address, width, value)

    target.reset_into_bdm = reset_and_write
    return target


class EntryState(unittest.TestCase):
    # the boot selects already as the bench order leaves them, SYPCR
    # already the host's value: not a fresh reset's
    NOT_FRESH = ((flash_host.SYPCR, 1, flash_host.SYPCR_VALUE), (flash_host.CSORBT, 2, 0x6870), (flash_host.CSBARBT, 2, 0x0005))

    def test_not_fresh_refused(self):
        target, model = board()
        lines: list[str] = []
        options = Options(erase_time=TEST_ERASE_LIMIT)
        with self.assertRaisesRegex(Refused, "SYPCR reads 0x4 at entry, not a fresh reset's 0x80"):
            flash_host.flash(entering(target, self.NOT_FRESH), trial_image.trial(IMAGE_3704), options, ROUTINE, lines.append)
        self.assertIn("at entry SYPCR 0x04, CSPAR0 0x3fff, CSPAR1 0x03ff, CSBARBT 0x0005, CSORBT 0x6870", lines)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)

    def test_fresh_reset_entry_passes(self):
        target, model = board()
        lines: list[str] = []
        flash_host.flash(target, IMAGE_3704, Options(dry_run=True), ROUTINE, lines.append)
        self.assertIn("at entry SYPCR 0x80, CSPAR0 0x3fff, CSPAR1 0x03ff, CSBARBT 0x0007, CSORBT 0x7b70", lines)

    def test_any_entry_state_goes_on(self):
        target, model = board()
        image = trial_image.trial(IMAGE_3704)
        report = run(entering(target, self.NOT_FRESH), image, any_entry_state=True)
        self.assertEqual(report.differing, [])
        self.assertEqual(report.final_sha256, hashlib.sha256(image).hexdigest())
        clean(self, model)

    def test_any_entry_state_sypcr_written_before(self):
        # SYPCR is write-once: the host's write does not take, and its
        # read-back refuses
        target, model = board()
        with self.assertRaisesRegex(Refused, "SYPCR reads 0x00 after writing 0x04"):
            run(entering(target, ((flash_host.SYPCR, 1, 0x00),)), IMAGE_3704, any_entry_state=True)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)


class ChipSelectOrder(unittest.TestCase):
    def test_bench_order_adds_no_hazard(self):
        target, model = board()
        built = session(target)
        self.assertTrue(built.selected)
        self.assertEqual(model.hazards, [])

    def test_reset_decode_has_the_mirror(self):
        registers = {address: value for _, address, _, value in flash_host.ENTRY_STATE}
        for name in flash_host.SELECTS[1:]:
            registers[flash_host.csbar(int(name[2:]))] = registers[flash_host.csor(int(name[2:]))] = 0
        found = {hazard for hazard, _ in hazards(registers)}
        self.assertEqual(found, {flash_host.Hazard.MIRROR, flash_host.Hazard.READ_SELECT_WRITES})

    def test_mirror_on_the_model(self):
        # at a reset entry a write meant for pair 2 reaches CSBOOT, pair 1's
        # read select, through its 1 MiB block: the model sees it
        target, model = board()
        target.reset_into_bdm()
        target.write_memory(0x4AAAA, WORD, 0xAAAA)
        self.assertEqual({hazard for hazard, _, _ in model.hazards}, {Hazard.MIRROR, Hazard.WRITE_ON_READ_SELECT})
        self.assertEqual({select for _, select, _ in model.hazards}, {"CSBOOT"})

    def test_changed_orders_refused(self):
        def moved(name: str, after: str) -> tuple:
            order = [entry for entry in CHIP_SELECTS if entry[0] != name]
            index = [entry[0] for entry in order].index(after) + 1
            return tuple(order[:index] + [next(entry for entry in CHIP_SELECTS if entry[0] == name)] + order[index:])

        for name, after, hazard in (
            ("CSBARBT", "CSOR1", "two selects drive a flash read"),  # CS1 open under the mirror
            ("CSBARBT", "CSOR7", "two selects drive a flash read"),
            ("CSORBT", "CSOR6", "two selects take a flash write"),  # CS6 beside CSBOOT's writes
            ("CSORBT", "CSOR7", "two selects take a flash write"),  # a pair 2 write reaching pair 1
        ):
            with self.subTest(name=name, after=after):
                target, model = board()
                with self.assertRaisesRegex(Refused, hazard):
                    Session(target, ROUTINE, quiet, chip_selects=moved(name, after)).set_up()
                self.assertEqual(model.hazards, [])


class Model(unittest.TestCase):
    def test_program_clears_only(self):
        _, model = board()
        part = model.pairs[1][0]
        part.write(0x5555, 0xAA, 0)
        part.write(0x2AAA, 0x55, 0)
        part.write(0x5555, 0xA0, 0)
        address = 0x10000
        old = part.cells[address]
        part.write(address, old | 0x01 if old & 0x01 == 0 else 0x00, 0)
        part.settle(clocks(1e-3))
        if old & 0x01 == 0:
            self.assertEqual(part.state, State.EXCEEDED_PROGRAM)
            self.assertEqual(part.cells[address], old)
        part.write(0, 0xF0, clocks(1e-3))
        self.assertEqual(part.state, State.READ)

    def test_parts_hold_the_image(self):
        _, model = board()
        self.assertEqual(model.image(), IMAGE_3704)
        self.assertEqual(len(model.pairs[0][0].cells), PART_SIZE)


class Trial(unittest.TestCase):
    def test_trial_plan(self):
        image = trial_image.trial(IMAGE_3704)
        flash_host.check_image(image)
        self.assertEqual(flash_host.differing_ranges(IMAGE_3704, image)[0], (0x70000, 0x70002))
        self.assertEqual(flash_host.plan(IMAGE_3704, image), [0x70000, 0x78000])
        self.assertEqual(flash_host.plan(IMAGE_3704, trial_image.trial(IMAGE_3704, 0x8000)), [0x8000])

    def test_trial_refusals(self):
        for address in (0x70001, 0x80000, 0x3FFFC, 0x7FFFE):
            with self.subTest(address=address), self.assertRaisesRegex(Refused, "not an even address"):
                trial_image.trial(IMAGE_3704, address)
        with self.assertRaisesRegex(Refused, "already holds"):
            trial_image.trial(IMAGE_3704, 0x70000, 0xFFFF)


class Modes(unittest.TestCase):
    def test_identify_only(self):
        target, model = board()
        report = run(target, variant(IMAGE_3704, 0x60000), identify_only=True)
        self.assertEqual(report.plan, [0x60000, 0x78000])
        self.assertEqual(target.runs, 2)
        self.assertEqual([part.operations for part in model.parts()], [[]] * 4)
        clean(self, model)

    def test_each_step_logged(self):
        target, model = board()
        lines: list[str] = []
        flash_host.flash(target, trial_image.trial(IMAGE_3704), Options(erase_time=TEST_ERASE_LIMIT), ROUTINE, lines.append)
        for base in ("0x70000", "0x78000"):
            for step in ("erased", "programmed", "verified"):
                self.assertIn(f"sector pair {base} {step}", lines)


class CommandLine(unittest.TestCase):
    def main(self, *arguments: str) -> tuple[str, str | None]:
        """flash_host's main on arguments: its output, and its exit's."""
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["flash_host.py", *arguments]), contextlib.redirect_stdout(output):
            try:
                flash_host.main()
            except SystemExit as error:
                return output.getvalue(), str(error.code)
        return output.getvalue(), None

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name)
        self.unit = self.path / "unit.bin"
        self.unit.write_bytes(IMAGE_3704)
        self.trial = self.path / "trial.bin"
        self.trial.write_bytes(trial_image.trial(IMAGE_3704))
        self.short = self.path / "short.bin"
        self.short.write_bytes(IMAGE_3704[:-2])
        self.original = str(IMAGES / "58503a-3704.bin")

    def test_dry_run(self):
        output, code = self.main("--model", str(self.unit), "--image", str(self.trial), "--dry-run")
        self.assertIsNone(code)
        self.assertIn(f"dump SHA-256 {SHA256_3704}", output)
        self.assertIn("sector pairs to write: 0x70000, 0x78000", output)
        self.assertIn("image lane sums of 0x40000-0x80000 hold", output)
        self.assertIn(flash_host.STOPPED, output)

    def test_identify(self):
        output, code = self.main("--model", str(self.unit), "--image", self.original, "--identify")
        self.assertIsNone(code)
        self.assertIn("manufacturer 0x0101, device 0x2020", output)
        self.assertIn("sector pairs to write: none", output)

    def test_write(self):
        output, code = self.main("--model", str(self.unit), "--image", str(self.trial), "--write")
        self.assertIsNone(code, output)
        self.assertIn(f"dump SHA-256 {SHA256_3704}", output)
        trial_sha256 = hashlib.sha256(self.trial.read_bytes()).hexdigest()
        self.assertIn(f"success: the flash is the image, SHA-256 {trial_sha256}", output)
        self.assertIn("sector pair 0x78000 verified", output)
        self.assertTrue(output.rstrip().endswith(flash_host.STOPPED))

    def test_refusals_before_any_target(self):
        unit, original = str(self.unit), self.original
        for arguments, message in (
            (("--model", unit, "--image", original), "one of the arguments"),
            (("--image", original, "--dry-run"), "--pod or --model is required"),
            (("--model", unit, "--dry-run"), "--image is required"),
            (("--pod", "usb", "--model", unit, "--image", original, "--dry-run"), "not allowed with"),
            (("--model", unit, "--image", original[:-4] + "-missing.bin", "--dry-run"), "No such file"),
            (("--plan", unit, original, "--pod", "usb"), "--plan touches no target"),
            (("--plan", unit, original, "--any-entry-state"), "--plan touches no target"),
            (("--model", unit, "--image", str(self.short), "--dry-run"), "not 524288"),
        ):
            with self.subTest(arguments=arguments):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    output, code = self.main(*arguments)
                self.assertIsNotNone(code)
                self.assertIn(message, (code or "") + stderr.getvalue())

    def test_plan(self):
        output, code = self.main("--plan", str(self.unit), str(self.trial))
        self.assertIsNone(code)
        self.assertIn("hold\n0x70000\n0x78000", output)

    def test_bad_sums_written(self):
        image = bytearray(IMAGE_3704)
        image[0x70000] = 0
        bad = self.path / "bad.bin"
        bad.write_bytes(bytes(image))
        output, code = self.main("--model", str(self.unit), "--image", str(bad), "--write")
        self.assertIsNone(code, output)
        self.assertLess(output.index("WARNING: an image whose lane sums fail"), output.index("sector pair 0x70000 erased"))
        self.assertIn("success: the flash is the image", output)


if __name__ == "__main__":
    unittest.main()
