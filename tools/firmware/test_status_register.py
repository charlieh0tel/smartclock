# /// script
# requires-python = ">=3.11"
# ///
"""Tests of status_register.py's two readings and their comparison, on
code written here: instructions assembled by hand, which objdump reads
back, and C as the preprocessor would leave it.

    python3 tools/firmware/test_status_register.py
"""

import unittest

from image_diff import Memory
from status_register import (
    CALLED,
    ENTER,
    LOWER,
    OPAQUE,
    RAISE,
    RETURN_FROM_EXCEPTION,
    SAVE,
    STOP,
    TOGGLE,
    USER_STACK,
    WRITE,
    WRITE_LITERAL,
    Event,
    Source,
    bracketed,
    compared,
    differences,
    image_events,
    instruction_event,
)

# Where the tests' code lies.
BASE = 0x1000
# Instructions, as the bytes of each.
SAVE_D0 = "40c0"  # move %sr,%d0
RAISE_7 = "007c0700"  # ori #0x700,%sr
LOWER_TO_3 = "027cfbff"  # andi #0xfbff,%sr
RESTORE_D0 = "46c0"  # move %d0,%sr
WRITE_2700 = "46fc2700"  # move #0x2700,%sr
RETURN = "4e75"  # rts
NOTHING = "4e71"  # nop
BRANCH_OVER_2 = "6002"  # bra.s past the next word
BRANCH_IF_EQUAL_OVER_4 = "6704"  # beq.s past the next two words
JUMP_TO = "4ef9"  # jmp to the absolute long that follows
CALL = "4eb9"  # jsr to the absolute long that follows

# cpu.h's helpers, as much of them as the reading names.
HELPERS = """
enum { CPU_IPL_3 = 0x300, CPU_IPL_7 = 0x700, };
static inline uint16_t cpu_sr_read(void) { uint16_t sr; __asm__ volatile("move.w %%sr,%0" : "=d"(sr)); return sr; }
static inline void cpu_sr_write(uint16_t sr) { __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc"); }
static inline uint16_t cpu_interrupts_raise(uint16_t mask) {
  uint16_t saved = cpu_sr_read();
  cpu_sr_write(saved | mask);
  return saved;
}
static inline uint16_t cpu_interrupts_at(uint16_t level) {
  uint16_t saved = cpu_interrupts_raise(level);
  cpu_sr_write(cpu_sr_read() & ~(CPU_IPL_7 & ~level));
  return saved;
}
"""


def memory(*instructions: str) -> Memory:
    """A memory at BASE holding the instructions."""
    return Memory(bytes.fromhex("".join(instructions)), BASE)


def address(value: int) -> str:
    """An absolute long operand's bytes."""
    return f"{value:08x}"


def image(memory: Memory, others: dict[int, str] | None = None) -> list[Event]:
    """The Events of the function at BASE, others the functions after
    it by their starts."""
    return [event for _, event in image_events(memory, BASE, {BASE: "routine", **(others or {})})]


def called(name: str) -> Event:
    """A call's Event."""
    return Event(CALLED, name=name)


def written(body: str, name: str = "routine", in_image: tuple[str, ...] = ("routine",)) -> list[Event]:
    """The Events of a routine of C after the helpers."""
    return Source(HELPERS + body).events(name, frozenset(in_image))


class Instructions(unittest.TestCase):
    def test_kinds(self):
        for mnemonic, operands, event in (
            ("movew", "%sr,%d0", Event(SAVE)),
            ("movew", "%sr,%sp@-", Event(SAVE)),
            ("oriw", "#1792,%sr", Event(RAISE, 0x700)),
            ("andiw", "#-1793,%sr", Event(LOWER, 0xF8FF)),
            ("eoriw", "#8192,%sr", Event(TOGGLE, 0x2000)),
            ("movew", "%d0,%sr", Event(WRITE)),
            ("movew", "#9984,%sr", Event(WRITE_LITERAL, 0x2700)),
            ("stop", "#8192", Event(STOP, 0x2000)),
            ("rte", "", Event(RETURN_FROM_EXCEPTION)),
            ("movel", "%a0,%usp", Event(USER_STACK)),
            ("orib", "#16,%ccr", None),
            ("movew", "%d0,%ccr", None),
            ("movew", "%d0,%d1", None),
        ):
            self.assertEqual(instruction_event(mnemonic, operands), event, (mnemonic, operands))


class Image(unittest.TestCase):
    def test_by_address(self):
        self.assertEqual(
            image(memory(SAVE_D0, RAISE_7, LOWER_TO_3, RESTORE_D0, RETURN)),
            [Event(SAVE), Event(RAISE, 0x700), Event(LOWER, 0xFBFF), Event(WRITE)],
        )

    def test_literal(self):
        self.assertEqual(image(memory(WRITE_2700, RETURN)), [Event(WRITE_LITERAL, 0x2700)])

    def test_unreached_is_left_out(self):
        self.assertEqual(image(memory(BRANCH_OVER_2, RESTORE_D0, RETURN, RAISE_7)), [])

    def test_both_arms(self):
        self.assertEqual(
            image(memory(BRANCH_IF_EQUAL_OVER_4, RAISE_7, RESTORE_D0, RETURN)), [Event(RAISE, 0x700), Event(WRITE)]
        )

    def test_shared_tail_is_followed(self):
        # a jump into the middle of the function after this one
        code = memory(RAISE_7, JUMP_TO, address(BASE + 12), RETURN, RESTORE_D0, RETURN)
        self.assertEqual(image(code, {BASE + 10: "other"}), [Event(RAISE, 0x700), Event(WRITE)])

    def test_jump_to_a_function_leaves(self):
        code = memory(RAISE_7, JUMP_TO, address(BASE + 10), RESTORE_D0, RETURN)
        self.assertEqual(image(code, {BASE + 10: "other"}), [Event(RAISE, 0x700)])

    def test_callee_is_called_not_read(self):
        code = memory(CALL, address(BASE + 8), RETURN, RESTORE_D0, RETURN)
        self.assertEqual(image(code, {BASE + 8: "other"}), [called("other")])

    def test_enter(self):
        code = memory(CALL, address(BASE + 10), RESTORE_D0, RETURN, NOTHING, RETURN)
        self.assertEqual(image(code, {BASE + 10: "enter_supervisor"}), [Event(ENTER), Event(WRITE)])


class Written(unittest.TestCase):
    def test_raise_used(self):
        self.assertEqual(
            written("void routine(void) { uint16_t sr = cpu_interrupts_raise(CPU_IPL_7); work(); cpu_sr_write(sr); }"),
            [Event(SAVE), Event(RAISE, 0x700), Event(WRITE)],
        )

    def test_raise_discarded(self):
        for call in ("cpu_interrupts_raise(CPU_IPL_7);", "(void)cpu_interrupts_raise(CPU_IPL_7);"):
            self.assertEqual(written("void routine(void) { work(); " + call + " }"), [Event(RAISE, 0x700)], call)

    def test_level(self):
        self.assertEqual(
            written("void routine(void) { uint16_t sr = cpu_interrupts_at(CPU_IPL_3); cpu_sr_write(sr); }"),
            [Event(SAVE), Event(RAISE, 0x300), Event(LOWER, 0xFBFF), Event(WRITE)],
        )

    def test_literal_and_expression(self):
        self.assertEqual(
            written("void routine(void) { cpu_sr_write(0x2000 | CPU_IPL_7); cpu_sr_write((uint16_t)saved); }"),
            [Event(WRITE_LITERAL, 0x2700), Event(WRITE)],
        )

    def test_not_constant(self):
        self.assertEqual(written("void routine(int face) { cpu_interrupts_raise(face); }"), [Event(RAISE, None)])

    def test_initializer_is_no_enumerator(self):
        text = "struct s v = { .CPU_IPL_9 = 0x100, };\nvoid routine(void) { cpu_interrupts_raise(CPU_IPL_9); }"
        self.assertEqual(written(text), [Event(RAISE, None)])

    def test_arguments_first(self):
        self.assertEqual(
            written("void routine(void) { cpu_sr_write(merge(cpu_enter_supervisor(), cpu_sr_read())); }"),
            [Event(ENTER), Event(SAVE), Event(WRITE)],
        )

    def test_enter_routine_called_by_name(self):
        self.assertEqual(
            written("void routine(void) { uint16_t sr = installer_enter_supervisor(); cpu_sr_write(sr); }"),
            [Event(ENTER), Event(WRITE)],
        )

    def test_helper_of_the_c_alone_is_read_in_place(self):
        text = (
            "static void lock(void) { cpu_interrupts_raise(CPU_IPL_7); }\n"
            "void other(void) { cpu_sr_write(0x2000); }\n"
            "void routine(void) { lock(); other(); }"
        )
        self.assertEqual(written(text, in_image=("routine", "other")), [Event(RAISE, 0x700), called("other")])

    def test_calls_in_order(self):
        text = "void routine(void) { a(); uint16_t sr = cpu_interrupts_raise(CPU_IPL_7); b(a()); cpu_sr_write(sr); }"
        self.assertEqual(
            written(text, in_image=("routine", "a", "b")),
            [called("a"), Event(SAVE), Event(RAISE, 0x700), called("a"), called("b"), Event(WRITE)],
        )

    def test_string_is_not_code(self):
        self.assertEqual(written('void routine(void) { print("cpu_sr_write(sr)"); }'), [])

    def test_assembly_in_a_function(self):
        self.assertEqual(
            written('void routine(void) { __asm__ volatile("ori.w #0x700,%%sr"); }'), [Event(OPAQUE)]
        )
        self.assertEqual(written('void routine(void) { __asm__ volatile("jsr other" : : : "d0"); }'), [])

    def test_assembly_wrapper(self):
        text = (
            "static uint16_t routine_body(uint16_t sr) { cpu_sr_write(sr | CPU_IPL_7); return sr; }\n"
            '__asm__("routine:\\n\\tmove.w %sr,-(%sp)\\n\\tjsr routine_body\\n\\trts\\n");'
        )
        self.assertEqual(written(text), [Event(OPAQUE), Event(WRITE)])
        text = text.replace("move.w %sr,-(%sp)", "move.w %ccr,-(%sp)")
        self.assertEqual(written(text), [Event(WRITE)])

    def test_assembly_wrapper_calls(self):
        text = (
            "void routine(void) { cpu_sr_read(); wrapper(); cpu_sr_write(0); }\n"
            '__asm__("wrapper:\\n\\tjsr other\\n\\tjsr enter_supervisor\\n\\trts\\n");'
        )
        self.assertEqual(
            written(text, in_image=("routine", "other")),
            [Event(SAVE), called("other"), Event(ENTER), Event(WRITE_LITERAL, 0)],
        )


class Differences(unittest.TestCase):
    def test_equal(self):
        self.assertEqual(differences([(BASE, Event(RAISE, 0x700))], [Event(RAISE, 0x700)]), [])
        self.assertEqual(differences([], []), [])

    def test_c_lacks(self):
        self.assertEqual(
            differences([(BASE, Event(SAVE)), (BASE + 2, Event(RAISE, 0x700)), (BASE + 6, Event(WRITE))], []),
            ["status register: the image's save, or 0x700, write; the C's nothing, differing from 0x1000"],
        )

    def test_other_mask(self):
        self.assertEqual(
            differences([(BASE, Event(SAVE)), (BASE + 2, Event(RAISE, 0x700))], [Event(SAVE), Event(RAISE, 0x600)]),
            ["status register: the image's save, or 0x700; the C's save, or 0x600, differing from 0x1002"],
        )

    def test_c_goes_on(self):
        self.assertEqual(
            differences([], [Event(RAISE, None)]),
            ["status register: the image's nothing; the C's or a value not constant, which go on past the image's"],
        )

    def test_order(self):
        self.assertTrue(differences([(BASE, Event(SAVE)), (BASE + 2, Event(WRITE))], [Event(WRITE), Event(SAVE)]))


class Bracketed(unittest.TestCase):
    def test_calls_outside_are_dropped(self):
        events = [called("a"), Event(SAVE), called("b"), Event(WRITE), called("c")]
        self.assertEqual(bracketed(events), [Event(SAVE), called("b"), Event(WRITE)])

    def test_calls_alone_are_nothing(self):
        self.assertEqual(bracketed([called("a"), called("b")]), [])

    def test_with_addresses(self):
        events = [(BASE, called("a")), (BASE + 6, Event(RAISE, 0x700))]
        self.assertEqual(bracketed(events, lambda item: item[1]), [(BASE + 6, Event(RAISE, 0x700))])


class Compared(unittest.TestCase):
    # three functions written by hand: one that agrees; one whose C
    # loses its restore; one that calls another within its masking,
    # which its C restores before; one that touches nothing, and is not
    # listed; and an assembly routine, which is not compared
    CODE = (
        (SAVE_D0, RAISE_7, RESTORE_D0, RETURN)
        + (RAISE_7, RESTORE_D0, RETURN)
        + (SAVE_D0, RAISE_7, CALL, address(BASE + 0x22), RESTORE_D0, RETURN)
        + (CALL, address(BASE + 0x2A), RETURN)
        + (WRITE_2700, RETURN)
    )
    CONVENTIONS = {
        "first": {"address": "00001000", "rewritten": "a.c"},
        "second": {"address": "0000100a", "rewritten": "a.c"},
        "third": {"address": "00001012", "rewritten": "a.c"},
        "fourth": {"address": "00001022", "rewritten": "a.c"},
        "fifth": {"address": "0000102a"},
        "elsewhere": {"address": "installer::00001000", "rewritten": "b.c"},
    }
    TEXT = HELPERS + (
        "void first(void) { uint16_t sr = cpu_interrupts_raise(CPU_IPL_7); cpu_sr_write(sr); }\n"
        "void second(void) { cpu_interrupts_raise(CPU_IPL_7); }\n"
        "void third(void) { uint16_t sr = cpu_interrupts_raise(CPU_IPL_7); cpu_sr_write(sr); fourth(); }\n"
        "void fourth(void) { fifth(); }\n"
        "void elsewhere(void) { cpu_sr_write(sr); }\n"
    )

    def test_region(self):
        found = compared(memory(*self.CODE), "primary", self.CONVENTIONS, Source(self.TEXT))
        self.assertEqual([name for name, _ in found], ["first", "second", "third"])
        self.assertEqual(found[0][1], [])
        self.assertEqual(
            found[1][1],
            ["status register: the image's or 0x700, write; the C's or 0x700, differing from 0x100e"],
        )
        self.assertEqual(
            found[2][1],
            [
                "status register: the image's save, or 0x700, call of fourth, write; "
                "the C's save, or 0x700, write, differing from 0x1018"
            ],
        )


if __name__ == "__main__":
    unittest.main()
