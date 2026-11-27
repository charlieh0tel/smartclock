//! The CPU32's exception processing, as data: the stack frames it
//! pushes and `rte` pops, the status register it enters a handler
//! with, and whether its interrupt mask admits a level (MC68331UM, 5.8;
//! CPU32RM, "Exception Stack Frames").  A core that cannot take
//! exceptions itself, Unicorn, takes them with these.

/// The status register's supervisor bit, its trace bits, and its
/// interrupt mask, bits 10 to 8.
pub(crate) const SUPERVISOR: u16 = 0x2000;
const TRACE: u16 = 0xc000;
const MASK_SHIFT: u16 = 8;
const MASK: u16 = 0x0700;
/// Level 7, which the mask does not keep out.
const NONMASKABLE: u8 = 7;
/// The vector word's format, its top four bits.
const FORMAT_SHIFT: u16 = 12;
/// A vector's offset in the table is four times its number.
pub(crate) const VECTOR_SIZE: u32 = 4;

/// The frames this pushes and pops.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Format {
    /// Format 0, four words: the status register, the program counter
    /// and the vector word.  Interrupts, `trap #n`, illegal
    /// instructions, privilege violations, line A and line F.
    Short,
    /// Format 2, six words: format 0's and the address of the
    /// instruction that caused it.  CHK, `TRAPcc`, zero divide, trace.
    Instruction(u32),
}

impl Format {
    fn number(self) -> u16 {
        match self {
            Format::Short => 0,
            Format::Instruction(_) => 2,
        }
    }

    /// The frame's size.
    fn size(number: u16) -> Option<u32> {
        match number {
            0 => Some(8),
            2 => Some(12),
            _ => None,
        }
    }
}

/// The frame an exception through `vector` pushes, as it lies on the
/// stack from its lowest address: the status register as it was, the
/// program counter to return to, the vector word.
pub(crate) fn frame(status: u16, return_to: u32, vector: u8, format: Format) -> Vec<u8> {
    let mut frame = Vec::with_capacity(12);
    frame.extend(status.to_be_bytes());
    frame.extend(return_to.to_be_bytes());
    let vector_word = (format.number() << FORMAT_SHIFT) | (u16::from(vector) * VECTOR_SIZE as u16);
    frame.extend(vector_word.to_be_bytes());
    if let Format::Instruction(address) = format {
        frame.extend(address.to_be_bytes());
    }
    frame
}

/// The status register a handler starts with: supervisor, not tracing,
/// and for an interrupt the mask at its level.
pub(crate) fn entered(status: u16, level: Option<u8>) -> u16 {
    let status = (status | SUPERVISOR) & !TRACE;
    match level {
        Some(level) => status & !MASK | u16::from(level) << MASK_SHIFT & MASK,
        None => status,
    }
}

/// Whether the mask in `status` admits an interrupt at `level`.
pub(crate) fn admits(status: u16, level: u8) -> bool {
    level == NONMASKABLE || u16::from(level) > (status & MASK) >> MASK_SHIFT
}

/// What `rte` takes off the stack.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Popped {
    /// The status register.
    pub(crate) status: u16,
    /// Where it returns to.
    pub(crate) return_to: u32,
    /// The frame's size, which the stack pointer moves up by.
    pub(crate) size: u32,
}

/// The frame at the top of the stack, its first eight bytes: `rte`'s,
/// or `None` for a format this does not take.
pub(crate) fn popped(top: [u8; 8]) -> Option<Popped> {
    let [s0, s1, p0, p1, p2, p3, v0, v1] = top;
    Some(Popped {
        status: u16::from_be_bytes([s0, s1]),
        return_to: u32::from_be_bytes([p0, p1, p2, p3]),
        size: Format::size(u16::from_be_bytes([v0, v1]) >> FORMAT_SHIFT)?,
    })
}

#[cfg(test)]
mod tests {
    use super::Format;
    use super::Popped;
    use super::admits;
    use super::entered;
    use super::frame;
    use super::popped;

    #[test]
    fn a_trap_pushes_four_words_with_its_vector_offset() {
        // trap #3, vector 35, from a task in user mode at 0x12340
        assert_eq!(
            frame(0x0004, 0x12342, 35, Format::Short),
            [0x00, 0x04, 0x00, 0x01, 0x23, 0x42, 0x00, 0x8c]
        );
    }

    #[test]
    fn a_format_2_frame_carries_the_instruction_address() {
        let frame = frame(0x2000, 0x100, 5, Format::Instruction(0xfc));
        assert_eq!(frame.len(), 12);
        assert_eq!(&frame[6..8], [0x20, 0x14]);
        assert_eq!(&frame[8..], [0, 0, 0, 0xfc]);
    }

    #[test]
    fn rte_pops_what_was_pushed() {
        let pushed = frame(0x2510, 0x0001_44ca, 66, Format::Short);
        let top: [u8; 8] = pushed[..8].try_into().expect("eight bytes");
        assert_eq!(
            popped(top),
            Some(Popped {
                status: 0x2510,
                return_to: 0x0001_44ca,
                size: 8
            })
        );
        let mut other = top;
        other[6] = 0xc0;
        assert_eq!(popped(other), None, "a bus error's format C");
    }

    #[test]
    fn an_interrupt_enters_supervisor_at_its_level() {
        assert_eq!(entered(0x8004, Some(5)), 0x2504);
        assert_eq!(entered(0x0700, None), 0x2700);
    }

    #[test]
    fn the_mask_admits_higher_levels_and_level_7() {
        assert!(admits(0x2000, 2));
        assert!(!admits(0x2400, 4));
        assert!(admits(0x2400, 5));
        assert!(admits(0x2700, 7));
    }
}
