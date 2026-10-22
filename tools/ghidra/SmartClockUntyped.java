// Lists the ROM that is neither code nor typed data: every run of
// undefined bytes, at least min= long, in the memory block at address 0
// (the ROM, to its block's end) and, where SmartClockInstaller.java made
// it, in the installer's overlay.  Run after SmartClockSetup.java with
// the names, or at any later step; it changes nothing in the program.
//
// Arguments:
//   output=FILE  the TSV to write (docs/firmware/untyped/IMAGE.tsv)
//   min=N        the shortest run listed, in bytes; 4 if not given
//
// Each run is classified, the first that fits:
//   padding     one fill byte, 0x00 or 0xff, too short to be more than
//               the alignment of what follows: under 16 bytes, ending
//               at a multiple of the smallest power of two not below its
//               length
//   erased      at least 98% of it 0xff
//   code-like   its first instruction a prologue (link on A6, movem to
//               -(SP)) or rts, and the instructions from there decoding
//               without a fault to a return or jump or the run's end;
//               decoded in memory by PseudoDisassembler, not written
//   referenced  data something refers to
//   unreferenced
// and given the references into it (their count and the first one's
// function, or address), and the defined item before and after it, its
// label and type, which name the structure a run lies beside.  The
// summary at the end, "#" lines, gives each block's count and bytes per
// class.  The file's first line, "#", names the program; the second
// gives the columns.
//
// It reads only: Memory.getBytes, Listing.getUndefinedRanges and
// getDefinedCodeUnitBefore/After, ReferenceManager's destination
// iterator and getReferencesTo, SymbolTable's iterators,
// FunctionManager.getFunctionContaining, and PseudoDisassembler's
// disassemble(Address), which decodes through Disassembler's
// pseudoDisassembleBlock into an InstructionBlock apart from the
// listing, with a PseudoDisassemblerContext of its own: the program's
// context is only read.  The output file is the one thing written.
//
//@category SmartClock

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class SmartClockUntyped extends GhidraScript {

	/** What a run of undefined bytes looks like; its name is the TSV's. */
	private enum Kind {
		PADDING("padding"),
		ERASED("erased"),
		CODE_LIKE("code-like"),
		REFERENCED("referenced"),
		UNREFERENCED("unreferenced");

		final String label;

		Kind(String label) {
			this.label = label;
		}
	}

	/** The references into a run: how many, and the first one's source. */
	private record References(int count, Address first) {}

	/** A run's reading as code: its first instruction, how many decoded, and whether cleanly. */
	private record CodeWalk(String first, int decoded, boolean clean) {}

	/** A block's tally for the summary: runs and bytes per kind. */
	private record Tally(Map<Kind, Integer> counts, Map<Kind, Long> bytes) {}

	private static final String OUTPUT = "output=";
	private static final String MIN = "min=";
	private static final int DEFAULT_MIN = 4;
	private static final String OVERLAY = "installer";
	private static final int ERASED = 0xff;
	private static final int ZERO = 0x00;
	private static final int ERASED_PERCENT = 98;
	private static final int PERCENT = 100;
	/** A fill-only run this long or longer is more than alignment. */
	private static final int PADDING_LIMIT = 16;
	/** The bytes of a run's start the TSV shows. */
	private static final int SHOWN_BYTES = 8;
	/** The 68000 decodes instructions only at even addresses. */
	private static final int INSTRUCTION_ALIGNMENT = 2;
	private static final String LINK = "link";
	private static final String MOVEM = "movem";
	private static final String RTS = "rts";
	private static final String FRAME_REGISTER = "A6";
	private static final String PUSH = "-(SP)";
	private static final String NONE = "-";
	private static final String COLUMNS = String.join("\t", "block", "start", "end", "length",
		"class", "detail", "bytes", "references", "first_reference", "before", "after");

	@Override
	protected void run() throws Exception {
		String output = null;
		int min = DEFAULT_MIN;
		for (String arg : getScriptArgs()) {
			if (arg.startsWith(OUTPUT)) {
				output = arg.substring(OUTPUT.length());
			}
			else if (arg.startsWith(MIN)) {
				min = Integer.decode(arg.substring(MIN.length()));
			}
		}
		if (output == null) {
			throw new IllegalArgumentException("no " + OUTPUT + "FILE");
		}
		List<MemoryBlock> blocks = new ArrayList<>();
		blocks.add(currentProgram.getMemory().getBlock(toAddr(0)));
		MemoryBlock overlay = currentProgram.getMemory().getBlock(OVERLAY);
		if (overlay != null) {
			blocks.add(overlay);
		}
		PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);
		Path path = Path.of(output);
		if (path.getParent() != null) {
			Files.createDirectories(path.getParent());
		}
		try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(path))) {
			out.println("# " + currentProgram.getName() + ": runs of " + min
				+ " or more undefined bytes (tools/ghidra/SmartClockUntyped.java)");
			out.println(COLUMNS);
			List<String> summary = new ArrayList<>();
			for (MemoryBlock block : blocks) {
				Tally tally = survey(block, min, disassembler, out);
				for (Kind kind : Kind.values()) {
					summary.add("# " + block.getName() + "\t" + kind.label + "\t"
						+ tally.counts().get(kind) + "\t" + tally.bytes().get(kind));
				}
				summary.add("# " + block.getName() + "\ttotal\t"
					+ tally.counts().values().stream().mapToInt(Integer::intValue).sum() + "\t"
					+ tally.bytes().values().stream().mapToLong(Long::longValue).sum());
			}
			out.println("# block\tclass\truns\tbytes");
			summary.forEach(out::println);
			summary.forEach(this::println);
		}
		println("wrote " + output);
	}

	/** Writes a block's runs and returns its tally. */
	private Tally survey(MemoryBlock block, int min, PseudoDisassembler disassembler,
			PrintWriter out) throws Exception {
		Map<Kind, Integer> counts = new EnumMap<>(Kind.class);
		Map<Kind, Long> bytes = new EnumMap<>(Kind.class);
		for (Kind kind : Kind.values()) {
			counts.put(kind, 0);
			bytes.put(kind, 0L);
		}
		Listing listing = currentProgram.getListing();
		AddressSet span = new AddressSet(block.getStart(), block.getEnd());
		for (AddressRange range : listing.getUndefinedRanges(span, true, monitor)) {
			monitor.checkCancelled();
			long length = range.getLength();
			if (length < min) {
				continue;
			}
			byte[] contents = new byte[(int) length];
			currentProgram.getMemory().getBytes(range.getMinAddress(), contents);
			References references = references(range);
			CodeWalk walk = walk(range, disassembler);
			Kind kind = classify(range, contents, references, walk);
			counts.merge(kind, 1, Integer::sum);
			bytes.merge(kind, length, Long::sum);
			out.println(String.join("\t", block.getName(), hex(range.getMinAddress()),
				hex(range.getMaxAddress()), Long.toString(length), kind.label,
				detail(kind, contents, walk), shown(contents), Integer.toString(references.count()),
				references.first() == null ? NONE : where(references.first()),
				describe(listing.getDefinedCodeUnitBefore(range.getMinAddress()), block),
				describe(listing.getDefinedCodeUnitAfter(range.getMaxAddress()), block)));
		}
		return new Tally(counts, bytes);
	}

	private Kind classify(AddressRange range, byte[] contents, References references,
			CodeWalk walk) {
		if (isPadding(range, contents)) {
			return Kind.PADDING;
		}
		if (count(contents, ERASED) * PERCENT >= (long) ERASED_PERCENT * contents.length) {
			return Kind.ERASED;
		}
		if (walk != null && walk.clean() && isPrologue(walk.first())) {
			return Kind.CODE_LIKE;
		}
		return references.count() > 0 ? Kind.REFERENCED : Kind.UNREFERENCED;
	}

	private boolean isPadding(AddressRange range, byte[] contents) {
		int fill = contents[0] & 0xff;
		if ((fill != ERASED && fill != ZERO) || count(contents, fill) != contents.length
				|| contents.length >= PADDING_LIMIT) {
			return false;
		}
		long alignment = Long.highestOneBit(contents.length);
		if (alignment < contents.length) {
			alignment <<= 1;
		}
		return (range.getMaxAddress().getOffset() + 1) % alignment == 0;
	}

	private static int count(byte[] contents, int value) {
		int count = 0;
		for (byte b : contents) {
			if ((b & 0xff) == value) {
				count++;
			}
		}
		return count;
	}

	private static boolean isPrologue(String instruction) {
		String[] parts = instruction.split(" ", 2);
		String mnemonic = parts[0];
		String operands = parts.length > 1 ? parts[1] : "";
		return mnemonic.equals(RTS)
			|| (mnemonic.startsWith(LINK) && operands.startsWith(FRAME_REGISTER))
			|| (mnemonic.startsWith(MOVEM) && operands.endsWith(PUSH));
	}

	/**
	 * Decodes a run from its first even address, in memory only, one
	 * instruction after another to a return or jump, a fault or the run's
	 * end.  Null if it has no even address or its first does not decode.
	 */
	private CodeWalk walk(AddressRange range, PseudoDisassembler disassembler) {
		long offset = range.getMinAddress().getOffset();
		Address at = range.getMinAddress().add(
			(INSTRUCTION_ALIGNMENT - offset % INSTRUCTION_ALIGNMENT) % INSTRUCTION_ALIGNMENT);
		String first = null;
		int decoded = 0;
		while (range.contains(at)) {
			PseudoInstruction instruction;
			try {
				instruction = disassembler.disassemble(at);
			}
			catch (Exception e) {
				instruction = null;
			}
			if (instruction == null) {
				return first == null ? null : new CodeWalk(first, decoded, false);
			}
			if (first == null) {
				first = instruction.toString();
			}
			decoded++;
			FlowType flow = instruction.getFlowType();
			if (flow.isTerminal() || (flow.isJump() && !flow.isConditional())) {
				break;
			}
			at = at.add(instruction.getLength());
		}
		return first == null ? null : new CodeWalk(first, decoded, true);
	}

	private References references(AddressRange range) {
		int count = 0;
		Address first = null;
		AddressIterator targets = currentProgram.getReferenceManager()
			.getReferenceDestinationIterator(new AddressSet(range), true);
		for (Address target : targets) {
			for (Reference reference : currentProgram.getReferenceManager()
					.getReferencesTo(target)) {
				if (first == null) {
					first = reference.getFromAddress();
				}
				count++;
			}
		}
		return new References(count, first);
	}

	private static String detail(Kind kind, byte[] contents, CodeWalk walk) {
		return switch (kind) {
			case CODE_LIKE -> walk.first() + "; " + walk.decoded() + " decoded";
			case ERASED -> count(contents, ERASED) + " of " + contents.length + " 0xff";
			default -> walk == null ? NONE
				: "as code " + walk.first() + "; " + walk.decoded() + " decoded"
					+ (walk.clean() ? "" : ", then a fault");
		};
	}

	private static String shown(byte[] contents) {
		StringBuilder text = new StringBuilder();
		for (int i = 0; i < Math.min(contents.length, SHOWN_BYTES); i++) {
			text.append(i == 0 ? "" : " ").append(String.format("%02x", contents[i] & 0xff));
		}
		return text.toString();
	}

	/** An address as its function and offset, else as the label before it. */
	private String where(Address address) {
		Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
		if (function != null) {
			return function.getName() + "@" + hex(address);
		}
		MemoryBlock block = currentProgram.getMemory().getBlock(address);
		return block == null ? hex(address) : labelAtOrBefore(address, block) + "@" + hex(address);
	}

	/** A defined item beside a run: its function or label, and its type. */
	private String describe(CodeUnit unit, MemoryBlock block) {
		if (unit == null || !block.contains(unit.getAddress())) {
			return NONE;
		}
		if (unit instanceof Instruction) {
			Function function =
				currentProgram.getFunctionManager().getFunctionContaining(unit.getAddress());
			return (function != null ? function.getName() : labelAtOrBefore(unit.getAddress(), block))
				+ " (code)";
		}
		return labelAtOrBefore(unit.getAddress(), block) + " ("
			+ ((Data) unit).getDataType().getName() + ")";
	}

	/** The nearest label at or before an address in its block, as LABEL or LABEL+0xOFFSET. */
	private String labelAtOrBefore(Address address, MemoryBlock block) {
		SymbolIterator symbols = currentProgram.getSymbolTable().getSymbolIterator(address, false);
		for (Symbol symbol : symbols) {
			Address at = symbol.getAddress();
			if (!block.contains(at)) {
				break;
			}
			if (!symbol.isPrimary()) {
				continue;
			}
			long offset = address.subtract(at);
			return symbol.getName() + (offset == 0 ? "" : "+0x" + Long.toHexString(offset));
		}
		return NONE;
	}

	private static String hex(Address address) {
		return "0x" + Long.toHexString(address.getOffset());
	}
}
