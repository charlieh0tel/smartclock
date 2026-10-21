// Prepares a SmartClock CPU32 firmware image (Z3801A / Z3805A family)
// imported raw at address 0: memory map, both vector tables, string
// tables, the compiler's PC-relative switch tables, flow that analysis
// got wrong, and names, the peripherals' registers among them as the
// types headers lay them out.  Safe to run again.
//
// Arguments, any of:
//
//   dry-run
//       report the functions in data that would be removed, and do not
//       remove them; everything else is applied.
//   types=tools/ghidra/types/smartclock.h, then types=.../mc68331.h
//       C declarations of the firmware's structures and the CPU's
//       modules, parsed into the program in the order given, before
//       the names, so that names can use them; their #defines are
//       skipped, as Ghidra's C parser takes none.
//   names=tools/ghidra/names/IMAGE.tsv
//       address, "function", "label" or "local", name, source, and
//       optionally a note saying what it is or does, a function's C
//       prototype, a label's data type or a local's "TYPE @ OFFSET",
//       and a function's calling convention, tab-separated; an address
//       in the installer's overlay is written installer::0x....  The names file's header sets out the
//       convention's keys.  A register prototype is built from its in
//       and out, at confidence run, read or documented only.  A
//       function's note, its convention and its source make the comment
//       the decompiler prints above its C; the convention is kept as JSON,
//       and the note too, for SmartClockExportC.java's sidecar:
//       names the firmware notes establish.  These replace names the
//       other two gave, but not names given by hand.
//   scpi=FILE
//       smartclock-cli dump-scpi --handlers IMAGE: a path, its query
//       handler and its setter, "-" for none; named scpi_query_PATH and
//       scpi_set_PATH.
//   pforth=docs/firmware/pforth/IMAGE.txt
//       table, word and code address; kernel words named forth_WORD,
//       diagnostic words diag_WORD, punctuation spelled out.
//
// Sources: MC68331 User's Manual (third_party/motorola/MC68331UM.pdf)
// Tables D-2 (GPT), D-3 (SIM), D-13 (QSM) and 5-2 (vectors); the chip
// select map in smartclockmon docs/firmware/restart.md, "The switch byte
// at 0x302000".  68681 register names are the part's standard ones.
//
// The switch idiom:
//
//     [cmpi.l #n,Rn | cmpa.l #n,An | subq.l #k,Dn | subi.l #k,Dn]
//     bhi     default
//     move.w  (d8,PC,Rn.w*2),Rn
//     [nop]
//     jmp     (2,PC,Rn.w)
//
// The table starts at the jmp's base and holds word offsets from it.
// After subq/subi #k the index runs -k..0, so the move.w displacement
// points 2k bytes into the table, and there are k + 1 cases.  After
// cmpi #n it runs 0..n.
//
// The kernel's service dispatch, in the primary image and in the
// installer's copy:
//
//     cmpi.w  #n,D7
//     bhi     error
//     ...
//     lsl.w   #2,D7
//     jsr     (d8,PC,D7.w)
//
// The table holds n + 1 bra.w, one a service, each to its handler.
// Analysis follows none of them.
//
//@category SmartClock

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.LongStream;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.services.DataTypeManagerService;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.AbstractFloatDataType;
import ghidra.program.model.data.AbstractIntegerDataType;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.ArchiveType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.Enum;
import ghidra.program.model.data.EnumDataType;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.InvalidDataTypeException;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.SignedByteDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TerminatedStringDataType;
import ghidra.program.model.data.TypedefDataType;
import ghidra.program.model.data.WordDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.StackFrame;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.util.exception.InvalidInputException;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.util.StringPropertyMap;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.data.DataTypeParser.AllowedDataTypes;
import ghidra.util.data.DataTypeParser;

public class SmartClockSetup extends GhidraScript {

	/** A memory region to map: name, base, length, and whether it is I/O. */
	private record Region(String name, long base, long size, boolean io) {}

	/** A named register: absolute address and width in bytes (1 or 2). */

	/**
	 * A recovered switch: the jmp, its table, the index of the table's
	 * first entry (-k after subq #k, else 0), and each case target in
	 * index order.
	 */
	private record Switch(Address jmp, Address table, int first, List<Address> targets) {}

	private static final long PRIMARY_VECTORS = 0x10000;
	private static final long BOOT_VECTORS = 0x0;
	private static final int VECTOR_COUNT = 256;

	/** Opcode of jmp (d8,PC,Xn); the extension word follows. */
	private static final int JMP_PC_INDEX = 0x4efb;
	/** Opcode of jsr (d8,PC,Xn), and of the bra.w each entry of its table holds. */
	private static final int JSR_PC_INDEX = 0x4ebb;
	private static final int BRA_W = 0x6000;
	private static final int BRA_W_LENGTH = 4;
	/** How far before the service dispatch's jsr to look for its bound check. */
	private static final int SERVICE_BOUND_SEARCH_BYTES = 32;
	/**
	 * move.w (d8,PC,Xn),Dn is 0011 RRR0 0011 1011 and movea.w to An is
	 * 0011 RRR0 0111 1011; mask out RRR and the data/address bit.
	 */
	private static final int MOVEW_PC_INDEX = 0x303b;
	private static final int MOVEW_PC_INDEX_MASK = 0xf1bf;
	private static final int NOP = 0x4e71;
	private static final String STOP = "stop";
	/** trap #5, the kernel's return from interrupt. */
	private static final int TRAP_RETURN_FROM_INTERRUPT = 0x4e45;
	private static final int RTE = 0x4e73;
	/** Naming arguments, each KIND=FILE; see the header. */
	private static final String NAMES = "names";
	private static final String SCPI = "scpi";
	private static final String PFORTH = "pforth";
	private static final String TYPES = "types";
	/** A #define, with any lines it continues onto. */
	private static final Pattern DEFINE = Pattern.compile("(?m)^#define\\b(?:[^\\n]*\\\\\\n)*[^\\n]*$");
	private static final String COMMENT = "#";
	private static final String NO_HANDLER = "-";
	private static final String FUNCTION = "function";
	/** A names row giving a function's stack variable a type. */
	private static final String LOCAL = "local";
	/** A names row giving an indirect call the prototype of what it calls. */
	private static final String CALL = "call";
	/**
	 * Stack offsets are from SP at entry, where the return address occupies
	 * 0..3; arguments start at 4, each in a slot of at least 2 bytes.
	 */
	private static final int FIRST_ARGUMENT_OFFSET = 4;
	private static final int STACK_SLOT_MINIMUM = 2;
	/** A long or pointer argument's slot, the default convention's for every argument. */
	private static final int STACK_SLOT = 4;
	/** A prototype's result type, its pointer stars and its name: "log_entry *f(". */
	private static final Pattern POINTER_RESULT = Pattern.compile("^(.*?\\S)\\s*(\\*+)\\s*(\\w+\\s*\\()");
	/** A register annotation in a prototype: "@ D0" or "@ D0:D1". */
	private static final Pattern ANNOTATION = Pattern.compile("\\s*@\\s*[A-Za-z0-9:]+");
	/** The names file's fourth field: where the name comes from. */
	private static final int SOURCE_FIELD = 3;
	/** Its optional fifth: what the function or label is or does. */
	private static final int NOTE_FIELD = 4;
	/** Its optional sixth: a function's C prototype, or a label's data type. */
	private static final int PROTOTYPE_FIELD = 5;
	/** Its optional seventh: a function's calling convention, as key=value pairs. */
	private static final int CONVENTION_FIELD = 6;
	/** The convention's pairs, and each pair's key and value. */
	private static final String PAIR_SEPARATOR = "; ";
	private static final String ITEM_SEPARATOR = ", ";
	/** The convention's keys, in the order the comment gives them. */
	private static final List<String> CONVENTION_KEYS = List.of("convention", "confidence", "in",
		"out", "copies", "returns", "results", "error", "preserves", "family");
	/** A key's own confidence: KEY.confidence. */
	private static final String CONFIDENCE_SUFFIX = ".confidence";
	private static final String CONFIDENCE = "confidence";
	private static final String KIND = "convention";
	/** The kinds of convention, and what each means; c and unknown are not "not C". */
	private static final Map<String, String> KINDS = Map.of(
		"c", "",
		"register", "its arguments and results in registers",
		"exception", "entered by the processor, not called",
		"pforth", "a routine of the pForth console's kernel, which runs with the data stack in "
			+ "A6, the end of the user area in A5, the dictionary pointer in A4 and the input in A3",
		"psos-service", "a pSOS kernel service, reached from trap #3 through the kernel's "
			+ "dispatch; its error comes back in D7.b and its results in D0 to D2, A0 and A1",
		"other", "neither C nor one of the usual kinds; the note says how it is called",
		"undetermined", "how it is called is not yet set out");
	private static final String C = "c";
	/** The confidences, strongest first; only the first three build a prototype. */
	private static final List<String> CONFIDENCES =
		List.of("run", "read", "documented", "derived", "assumed");
	private static final Set<String> TRUSTED = Set.of("run", "read", "documented");
	/** A register, or a pair of them high first, with an optional width. */
	private static final Pattern REGISTER =
		Pattern.compile("^([DA][0-7](?::[DA][0-7])?)(\\.[bwl])?$");
	/** Where the conventions and notes are kept for SmartClockExportC.java. */
	private static final String CONVENTION_MAP = "SmartClock.convention";
	private static final String NOTE_MAP = "SmartClock.note";
	/** What C cannot say of a function that is not C, for its signature line. */
	private static final String SIGNATURE_MAP = "SmartClock.signature";
	/** The keys that SIGNATURE_MAP gives, in its order. */
	private static final List<String> SIGNATURE_KEYS =
		List.of("out", "copies", "returns", "results", "error", "preserves");
	/** Each recovered switch, by its jmp: "TABLE FIRST ENTRIES", for SmartClockExportC.java. */
	private static final String SWITCH_MAP = "SmartClock.switch";
	/** The tag every function that is not C carries. */
	private static final String NOT_C_TAG = "not-c";
	/** What the exception handlers' recorded convention says. */
	private static final String EXCEPTION_CONVENTION =
		"{\"convention\":\"exception\",\"confidence\":\"derived\",\"family\":\"exception\"}";
	/** A source that is no citation: the name was read from the function's callers. */
	private static final String CONTEXT = "context";
	/** How a comment says its function is not C, which the comments added later look for. */
	private static final String NOT_C = "Not a C function";
	/** How far from a named entry to repair flow when it does not disassemble. */
	private static final int ENTRY_WINDOW = 0x400;

	/** What applying names did to a name. */
	private enum NameCount { NAMED, LABELED, SKIPPED }

	/** How each punctuation character of a Forth word is spelled in a name. */
	private static final Map<Character, String> SPELLED = Map.ofEntries(
		Map.entry('!', "store"), Map.entry('@', "fetch"), Map.entry('+', "plus"),
		Map.entry('-', "minus"), Map.entry('*', "star"), Map.entry('/', "slash"),
		Map.entry('.', "dot"), Map.entry('"', "quote"), Map.entry('\'', "tick"),
		Map.entry('(', "paren"), Map.entry(')', "close_paren"), Map.entry(',', "comma"),
		Map.entry(':', "colon"), Map.entry(';', "semicolon"), Map.entry('<', "less"),
		Map.entry('>', "greater"), Map.entry('=', "equal"), Map.entry('?', "query"),
		Map.entry('#', "number"), Map.entry('[', "bracket"), Map.entry(']', "close_bracket"),
		Map.entry('$', "dollar"), Map.entry('%', "percent"), Map.entry('&', "and"),
		Map.entry('|', "or"), Map.entry('~', "tilde"), Map.entry('^', "caret"),
		Map.entry('{', "brace"), Map.entry('}', "close_brace"), Map.entry('\\', "backslash"));

	/** Script argument: report functions in data without removing them. */
	private static final String DRY_RUN = "dry-run";
	/** More functions in data than this means the rule misfired, not the image. */
	private static final int MAX_DATA_FUNCTIONS = 20;
	/** A string table: at least this many text bytes holding this many strings. */
	private static final int MIN_STRING_TABLE_BYTES = 32;
	private static final int MIN_STRING_TABLE_STRINGS = 3;
	/** Printable characters before the NUL for a piece to count as a string. */
	private static final int MIN_STRING_LENGTH = 4;
	/** cmpi.l #imm32,Dn and cmpi.w #imm16,Dn, register in the low 3 bits. */
	private static final int CMPI_L = 0x0c80;
	private static final int CMPI_W = 0x0c40;
	private static final int CMPI_REGISTER_MASK = 0xfff8;
	/** cmpa.l #imm32,An and cmpa.w #imm16,An, register in bits 11-9. */
	private static final int CMPA_L = 0xb1fc;
	private static final int CMPA_W = 0xb0fc;
	private static final int CMPA_REGISTER_MASK = 0xf1ff;
	/** bhi.b / bhi.w / bhi.l all start 0x62. */
	private static final int BHI_HIGH_BYTE = 0x62;
	/** Lengths of bhi.b, bhi.w and bhi.l. */
	private static final int[] BHI_LENGTHS = { 2, 4, 6 };
	/** How far before the move.w to look for the bound check. */
	private static final int BOUND_SEARCH_BYTES = 16;
	/** link.w A6,#d16. */
	private static final int LINK_A6 = 0x4e56;
	private static final int LINK_LENGTH = 4;
	/** movem.l <list>,-(SP). */
	private static final int MOVEM_L_SAVE = 0x48e7;
	/** How far before an orphaned switch to look for its function's link. */
	private static final int FRAME_SEARCH_BYTES = 0x1000;
	/** How far past a table to repair flow through the case bodies. */
	private static final int CASE_BODY_WINDOW = 0x400;
	/** The namespace analysis puts a switch's case labels in, and their prefix. */
	private static final String SWITCH_NAMESPACE_PREFIX = "switchD_";
	private static final String CASE_LABEL_PREFIX = "caseD_";
	/** The sizes sizeEnums tries, smallest first. */
	private static final List<Integer> ENUM_SIZES = List.of(1, 2, 4);
	/** <stdint.h>'s byte types, and the Ghidra types that print as numbers. */
	private static final Map<String, DataType> BYTE_TYPES = Map.of(
		"uint8_t", ByteDataType.dataType,
		"int8_t", SignedByteDataType.dataType);
	/** Upper limit on a table with no bound check found. */
	private static final int MAX_UNBOUNDED_CASES = 256;

	private static final Region[] REGIONS = {
		new Region("RAM", 0x100000, 0x10000, false),
		new Region("DUART", 0x200000, 0x800, true),
		new Region("CS5_IO", 0x300000, 0x10000, true),
		new Region("EEPROM", 0x400000, 0x2000, false),
		new Region("CS4", 0x500000, 0x800, true),
		new Region("GPT", 0xfff900, 0x40, true),
		new Region("SIM", 0xfffa00, 0x80, true),
		new Region("QSM", 0xfffc00, 0x150, true),
	};

	/** CPU32 vector names, MC68331UM Table 5-2; others are named by number. */
	private static final Map<Integer, String> VECTOR_NAMES = new TreeMap<>(Map.ofEntries(
		Map.entry(0, "initial_sp"), Map.entry(1, "reset"), Map.entry(2, "bus_error"),
		Map.entry(3, "address_error"), Map.entry(4, "illegal_instruction"),
		Map.entry(5, "zero_division"), Map.entry(6, "chk"), Map.entry(7, "trapcc"),
		Map.entry(8, "privilege_violation"), Map.entry(9, "trace"), Map.entry(10, "line_a"),
		Map.entry(11, "line_f"), Map.entry(12, "hardware_breakpoint"),
		Map.entry(14, "format_error"), Map.entry(15, "uninitialized_interrupt"),
		Map.entry(24, "spurious_interrupt")));
	private static final int AUTOVECTOR_FIRST = 25;
	private static final int AUTOVECTOR_LAST = 31;
	private static final int TRAP_FIRST = 32;
	private static final int TRAP_LAST = 47;
	private static final int USER_FIRST = 64;

	@Override
	protected void run() throws Exception {
		mapMemory();
		labelVectors(BOOT_VECTORS, "boot");
		labelVectors(PRIMARY_VECTORS, "primary");
		typeStrings();
		analyzeChanges(currentProgram);
		recoverServiceTables();
		repairFlow();
		removeDataFunctions();
		applyNames();
		repairFlow();
		commentVectors(BOOT_VECTORS, "boot");
		commentVectors(PRIMARY_VECTORS, "primary");
	}

	/**
	 * Recovers switches and repairs flow until a round finds nothing to
	 * resume: code a repair frees can hold a switch, and a switch's cases
	 * can hold flow to repair.
	 */
	private void repairFlow() throws Exception {
		int resumed;
		do {
			List<Switch> switches = new ArrayList<>();
			List<Address> unrecognized = new ArrayList<>();
			int found;
			do {
				found = recoverSwitches(switches, unrecognized);
				analyzeChanges(currentProgram);
			}
			while (found > 0);
			overrideSwitches(switches);
			println("switches recovered: " + switches.size() + ", unrecognized: "
				+ unrecognized.size());
			endFlowAtNoReturn();
			clearOddData();
			resumed = resumeBlockedFlow();
		}
		while (resumed > 0);
	}

	/**
	 * Applies the naming arguments, in the order names, scpi, pforth.  A
	 * name replaces only one Ghidra made up; otherwise it is added as a
	 * further label, so nothing named earlier or by hand is overwritten,
	 * though a names-file label becomes the primary one at its address.
	 */
	private void applyNames() throws Exception {
		for (String arg : getScriptArgs()) {
			if (arg.startsWith(TYPES + "=")) {
				// Ghidra's C parser takes no preprocessor lines but #pragma; a
				// header's #defines are for gcc and the exported C
				String header = DEFINE.matcher(
					Files.readString(Path.of(arg.substring(TYPES.length() + 1)))).replaceAll("");
				new CParser(currentProgram.getDataTypeManager(), true, null).parse(header);
				bindByteTypes();
				sizeEnums();
				println("types parsed from " + arg.substring(TYPES.length() + 1));
			}
		}
		int[] counts = new int[NameCount.values().length];
		for (String kind : List.of(NAMES, SCPI, PFORTH)) {
			for (String arg : getScriptArgs()) {
				if (!arg.startsWith(kind + "=")) {
					continue;
				}
				List<String> lines = Files.readAllLines(Path.of(arg.substring(kind.length() + 1)));
				for (String line : lines) {
					applyNameLine(kind, line, counts);
				}
			}
		}
		println("named " + counts[NameCount.NAMED.ordinal()] + ", labeled "
			+ counts[NameCount.LABELED.ordinal()] + ", skipped "
			+ counts[NameCount.SKIPPED.ordinal()]);
	}

	private void applyNameLine(String kind, String line, int[] counts) throws Exception {
		if (line.isBlank() || line.startsWith(COMMENT)) {
			return;
		}
		switch (kind) {
			case NAMES -> {
				String[] fields = line.split("\t");
				Address address = toAddr(fields[0]);
				if (address == null) {
					throw new IllegalArgumentException("unparsable address: " + line);
				}
				if (fields[1].equals(FUNCTION)) {
					nameFunction(address, fields[2], true, counts);
					Map<String, String> convention = parseConvention(field(fields, CONVENTION_FIELD),
						line);
					String prototype = field(fields, PROTOTYPE_FIELD);
					if (prototype.contains("@")) {
						throw new IllegalArgumentException(
							"registers go in the convention's in and out, not the prototype: " + line);
					}
					String built = prototypeFor(fields[2], prototype, convention, line);
					if (built != null) {
						applyPrototype(address, built);
					}
					Function function = getFunctionAt(address);
					if (function != null) {
						function.setComment(commentText(functionComment(fields, convention)));
						record(function, convention.isEmpty() ? null : conventionJson(convention),
							field(fields, NOTE_FIELD));
						recordSignature(function, convention);
					}
				}
				else if (fields[1].equals(LOCAL)) {
					applyLocal(address, fields[2], field(fields, PROTOTYPE_FIELD), line);
				}
				else if (fields[1].equals(CALL)) {
					applyCall(address, field(fields, PROTOTYPE_FIELD), line);
				}
				else {
					label(address, fields[2], counts);
					if (fields.length > PROTOTYPE_FIELD && !fields[PROTOTYPE_FIELD].isBlank()) {
						applyDataType(address, fields[PROTOTYPE_FIELD]);
					}
				}
			}
			case SCPI -> {
				String[] fields = line.split(" ");
				String path = (fields[0].startsWith("*")
						? "common_" + fields[0].substring(1)
						: fields[0].substring(1)).replace(':', '_');
				if (!fields[1].equals(NO_HANDLER)) {
					Address handler = toAddr(Long.decode(fields[1]));
					nameFunction(handler, "scpi_query_" + path, false, counts);
					describe(handler, "the query handler for " + fields[0] + "?");
					applyHandlerPrototype(handler);
				}
				if (!fields[2].equals(NO_HANDLER)) {
					Address handler = toAddr(Long.decode(fields[2]));
					nameFunction(handler, "scpi_set_" + path, false, counts);
					describe(handler, "the setter for " + fields[0]);
					applyHandlerPrototype(handler);
				}
			}
			default -> {
				String[] fields = line.split(" ");
				boolean kernel = fields[0].equals("kernel");
				Address word = toAddr(Long.decode(fields[2]));
				nameFunction(word, (kernel ? "forth_" : "diag_") + spell(fields[1]), false,
					counts);
				describe(word, "the pForth " + (kernel ? "" : "diagnostic ") + "word "
					+ fields[1]);
			}
		}
	}

	/** The Forth word as an identifier, its punctuation spelled out. */
	private static String spell(String word) {
		StringBuilder name = new StringBuilder();
		for (char c : word.toCharArray()) {
			String spelled = SPELLED.get(c);
			if (spelled == null) {
				name.append(c);
				continue;
			}
			if (!name.isEmpty() && name.charAt(name.length() - 1) != '_') {
				name.append('_');
			}
			name.append(spelled).append('_');
		}
		String result = name.toString();
		return result.endsWith("_") ? result.substring(0, result.length() - 1) : result;
	}

	/**
	 * Names the function at address, an entry a table in the image vouches
	 * for.  If there is none, one is made: through what analysis left in
	 * the way, and splitting a function whose body ran into it.  A function
	 * Ghidra named is renamed, and with curated so is one these arguments
	 * named, on this run or an earlier one; one named otherwise, by hand,
	 * gets the name as a further label.  The SCPI and pForth names a
	 * curated name displaces come back as labels when they are applied.
	 */
	private void nameFunction(Address address, String name, boolean curated, int[] counts)
			throws Exception {
		Function function = getFunctionAt(address);
		if (function == null) {
			if (getInstructionAt(address) == null) {
				repairFrom(List.of(address), new AddressSet(address, address.add(ENTRY_WINDOW)),
					false);
			}
			CreateFunctionCmd create = new CreateFunctionCmd(address);
			create.applyTo(currentProgram, monitor);
			function = getFunctionAt(address);
			if (function == null) {
				println("skipped " + name + ": no function could be made at " + address);
				counts[NameCount.SKIPPED.ordinal()]++;
				return;
			}
		}
		if (function.getName().equals(name)) {
			return;
		}
		SourceType source = function.getSymbol().getSource();
		if (source == SourceType.DEFAULT || (curated && source == SourceType.IMPORTED)) {
			for (Symbol label : currentProgram.getSymbolTable().getSymbols(address)) {
				if (label.getName().equals(name) && !label.equals(function.getSymbol())) {
					label.delete();
				}
			}
			function.setName(name, SourceType.IMPORTED);
			counts[NameCount.NAMED.ordinal()]++;
		}
		else {
			label(address, name, counts);
		}
	}

	/**
	 * What scpi_call_handler (0x29642 in the Z3805A 3543B) passes every
	 * handler it calls through a node's slot: the parsed parameters, from
	 * +10 of its call record, the room it took in the arena for a query's
	 * response, and the call record.  A handler reads only what it needs,
	 * so its own body shows fewer.
	 */
	private static final String HANDLER_PARAMETERS = "(void *parameters, void *response, void *call)";

	/**
	 * Gives the SCPI handler at address the handlers' prototype, unless
	 * the names file gave its function one.
	 */
	private void applyHandlerPrototype(Address address) throws Exception {
		Function function = getFunctionAt(address);
		if (function.getSignatureSource() == SourceType.IMPORTED
			|| function.getSignatureSource() == SourceType.USER_DEFINED) {
			return;
		}
		applyPrototype(address, "int " + function.getName() + HANDLER_PARAMETERS);
	}

	/**
	 * Gives the function at address the C prototype prototype, as the
	 * notes establish it.  A prototype with register annotations --
	 * "float f(float a @ D0, double b @ A0:A1) @ D0", a pair high
	 * register first -- puts each annotated value in those registers, in
	 * custom storage, and each argument without one on the stack, in
	 * order from the return address up.  This compiler pushes a char or
	 * short in a 2-byte slot, its value at the slot's start, where Ghidra's
	 * default convention would look at the end of a 4-byte slot; so a
	 * prototype with an argument narrower than 4 bytes is laid out this way
	 * too, and so is one returning a float or double: Ghidra's default
	 * convention returns those in FP0, but these images have no FPU and
	 * return them in D0 or D0:D1.  Any other takes the default
	 * convention.
	 */
	private void applyPrototype(Address address, String prototype) throws Exception {
		FunctionDefinitionDataType signature =
			parseSignature(ANNOTATION.matcher(prototype).replaceAll(""));
		if (signature == null) {
			throw new IllegalArgumentException("cannot parse prototype at " + address + ": "
				+ prototype);
		}
		boolean narrow = false;
		for (ParameterDefinition argument : signature.getArguments()) {
			narrow |= argument.getDataType().getLength() < STACK_SLOT;
		}
		boolean floating = signature.getReturnType() instanceof AbstractFloatDataType;
		if (!prototype.contains("@") && !narrow && !floating) {
			new ApplyFunctionSignatureCmd(address, signature, SourceType.IMPORTED)
					.applyTo(currentProgram, monitor);
			return;
		}
		int open = prototype.indexOf('(');
		int close = prototype.lastIndexOf(')');
		String[] declared = prototype.substring(open + 1, close).split(",");
		ParameterDefinition[] types = signature.getArguments();
		List<Parameter> parameters = new ArrayList<>();
		int stackOffset = FIRST_ARGUMENT_OFFSET;
		for (int i = 0; i < types.length; i++) {
			DataType type = types[i].getDataType();
			VariableStorage storage;
			if (declared[i].contains("@")) {
				storage = registerStorage(declared[i], address);
			}
			else {
				int size = Math.max(type.getLength(), STACK_SLOT_MINIMUM);
				storage = new VariableStorage(currentProgram, stackOffset, type.getLength());
				stackOffset += size + (size & 1);
			}
			parameters.add(new ParameterImpl(types[i].getName(), type, storage, currentProgram,
				SourceType.IMPORTED));
		}
		String returned = prototype.substring(close + 1);
		DataType returnType = signature.getReturnType();
		ReturnParameterImpl result = returned.contains("@")
				? new ReturnParameterImpl(returnType, registerStorage(returned, address),
					currentProgram)
				: new ReturnParameterImpl(returnType, resultStorage(returnType, address),
					currentProgram);
		try {
			getFunctionAt(address).updateFunction(null, result, parameters,
				FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.IMPORTED);
		}
		catch (InvalidInputException e) {
			throw new IllegalArgumentException("cannot lay out prototype at " + address + ": "
				+ prototype + ": " + e.getMessage(), e);
		}
	}

	/**
	 * Where a C function returns a result of type: nowhere for void, else
	 * D0, its low word or byte for a short or char, or D0:D1 for eight
	 * bytes, as the image's calls take it.  Custom
	 * storage, which a narrow argument needs, gives a result none of its
	 * own, which the decompiler would read as void.
	 */
	private VariableStorage resultStorage(DataType type, Address address) throws Exception {
		if (type == null || type.getLength() <= 0 || type instanceof VoidDataType) {
			return VariableStorage.VOID_STORAGE;
		}
		int length = type.getLength();
		return registerStorage("@ " + (length > STACK_SLOT ? "D0:D1" : length == 2 ? "D0w"
			: length == 1 ? "D0b" : "D0"), address);
	}

	/** The registers after the "@" in declaration, as storage. */
	private VariableStorage registerStorage(String declaration, Address address)
			throws Exception {
		int at = declaration.indexOf('@');
		if (at < 0) {
			throw new IllegalArgumentException("no register for " + declaration.strip() + " at "
				+ address);
		}
		List<Register> registers = new ArrayList<>();
		for (String name : declaration.substring(at + 1).strip().split(":")) {
			Register register = currentProgram.getRegister(name.strip());
			if (register == null) {
				throw new IllegalArgumentException("no register " + name + " at " + address);
			}
			registers.add(register);
		}
		return new VariableStorage(currentProgram, registers.toArray(new Register[0]));
	}

	/**
	 * Makes the header's uint8_t and int8_t Ghidra's byte and sbyte.  The
	 * header defines them as unsigned and signed char, as C does, but the
	 * decompiler prints a char type's constants as characters ('\b' for 8)
	 * and these two's as numbers.
	 */
	private void bindByteTypes() {
		DataTypeManager types = currentProgram.getDataTypeManager();
		for (Map.Entry<String, DataType> binding : BYTE_TYPES.entrySet()) {
			types.addDataType(new TypedefDataType(binding.getKey(), binding.getValue()),
				DataTypeConflictHandler.REPLACE_HANDLER);
		}
	}

	/**
	 * Sizes each enum to the smallest of 1, 2 or 4 bytes that holds its
	 * values, as gcc does an enum the header declares
	 * __attribute__((packed)).  Ghidra's C parser makes every enum 4 bytes
	 * and has no syntax for another size, and these are mostly bytes.  The
	 * header's structures are packed, so they lay out again around the new
	 * size.
	 */
	private void sizeEnums() {
		DataTypeManager types = currentProgram.getDataTypeManager();
		List<Enum> enums = new ArrayList<>();
		types.getAllDataTypes().forEachRemaining(type -> {
			if (type instanceof Enum e && e.getSourceArchive().getArchiveType() == ArchiveType.PROGRAM) {
				enums.add(e);
			}
		});
		for (Enum e : enums) {
			long min = Math.min(0, LongStream.of(e.getValues()).min().orElse(0));
			long max = LongStream.of(e.getValues()).max().orElse(0);
			int size = ENUM_SIZES.stream().filter(n -> fits(min, max, n)).findFirst().orElse(4);
			if (size == e.getLength()) {
				continue;
			}
			EnumDataType sized = new EnumDataType(e.getCategoryPath(), e.getName(), size);
			for (String name : e.getNames()) {
				sized.add(name, e.getValue(name), e.getComment(name));
			}
			types.addDataType(sized, DataTypeConflictHandler.REPLACE_HANDLER);
		}
	}

	/** True if every value from min to max fits in a size-byte integer. */
	private static boolean fits(long min, long max, int size) {
		long bits = size * 8L;
		return min < 0
				? min >= -(1L << (bits - 1)) && max < (1L << (bits - 1))
				: max < (1L << bits);
	}

	/** Types the data at address as declaration, a type the program knows, as "status_model *". */
	private void applyDataType(Address address, String declaration) throws Exception {
		DataType type = parseType(declaration);
		clearListing(address, address.add(type.getLength() - 1));
		createData(address, type);
	}

	/** The type a declaration such as "foo *" names, from the header's. */
	private DataType parseType(String declaration) throws Exception {
		DataTypeManager types = currentProgram.getDataTypeManager();
		try {
			return new DataTypeParser(types, types, null, AllowedDataTypes.ALL)
					.parse(declaration.strip());
		}
		catch (InvalidDataTypeException e) {
			DataType type = namedPointer(types, declaration.strip());
			if (type == null) {
				throw e;
			}
			return type;
		}
	}

	/**
	 * Gives the function at address a stack variable name, of the type
	 * and at the stack offset "TYPE @ OFFSET" says, Ghidra's offset from
	 * the stack pointer at entry; the variables it overlaps are cleared.
	 */
	private void applyLocal(Address address, String name, String declaration, String line)
			throws Exception {
		Function function = getFunctionAt(address);
		String[] parts = declaration.split("@");
		if (function == null || parts.length != 2) {
			throw new IllegalArgumentException("a local needs a function and TYPE @ OFFSET: " + line);
		}
		DataType type = parseType(parts[0]);
		int offset = Integer.decode(parts[1].strip());
		StackFrame frame = function.getStackFrame();
		for (Variable variable : frame.getStackVariables()) {
			int start = variable.getStackOffset();
			if (start < offset + type.getLength() && offset < start + variable.getLength()) {
				frame.clearVariable(start);
			}
		}
		frame.createVariable(name, offset, type, SourceType.IMPORTED);
	}

	/**
	 * prototype parsed, null if it cannot be.  Ghidra's parser drops a
	 * pointer result's stars when they are written against the name,
	 * "log_entry *f(", and keeps them against the type, "log_entry* f(".
	 */
	private FunctionDefinitionDataType parseSignature(String prototype) throws Exception {
		return CParserUtils.parseSignature((DataTypeManagerService) null, currentProgram,
			POINTER_RESULT.matcher(prototype).replaceFirst("$1$2 $3"), false);
	}

	/**
	 * Gives the call at address, through a pointer, the prototype of the
	 * routine it reaches, as a decompiler override: the decompiler takes
	 * a call through a typed pointer loaded from a table as one to code,
	 * with no arguments, and drops what the caller pushed.
	 */
	private void applyCall(Address address, String prototype, String line) throws Exception {
		Function function = getFunctionContaining(address);
		FunctionDefinitionDataType signature = parseSignature(prototype);
		if (function == null || signature == null) {
			throw new IllegalArgumentException("a call needs a function and a prototype: " + line);
		}
		HighFunctionDBUtil.writeOverride(function, address, signature);
	}

	/**
	 * A pointer to a type the header defines by name, as "name *", which
	 * the data type parser does not find when the type is a function
	 * typedef; null if declaration is not that.
	 */
	private static DataType namedPointer(DataTypeManager types, String declaration) {
		int stars = declaration.length() - declaration.replaceFirst("[\\s*]+$", "").length();
		String name = declaration.replaceFirst("[\\s*]+$", "");
		long pointers = declaration.chars().filter(c -> c == '*').count();
		List<DataType> found = new ArrayList<>();
		types.findDataTypes(name, found);
		if (stars == 0 || pointers == 0 || found.isEmpty()) {
			return null;
		}
		DataType type = found.get(0);
		for (long i = 0; i < pointers; i++) {
			type = new PointerDataType(type, types);
		}
		return type;
	}

	/**
	 * Makes name the primary label at address, adding it unless it is
	 * there already.  Other labels there stay, but the decompiler names an
	 * address by its primary label, and a structure the names file types
	 * should read by the name it gives.
	 */
	private void label(Address address, String name, int[] counts) throws Exception {
		Symbol existing = getSymbols(name, null).stream()
			.filter(s -> s.getAddress().equals(address)).findFirst().orElse(null);
		if (existing == null) {
			createLabel(address, name, true, SourceType.IMPORTED);
			counts[NameCount.LABELED.ordinal()]++;
		}
		else if (!existing.isPrimary()) {
			existing.setPrimary();
		}
	}

	/**
	 * Clears pointers and word or long integers at odd addresses, which
	 * the 68000 cannot read (an address error) and so cannot be real.
	 * Analysis's pointer scan leaves such pointers in code, where they
	 * block disassembly.  Byte data, arrays of bytes included, is legal at
	 * any address and is left alone.
	 */
	private void clearOddData() throws Exception {
		List<Data> odd = new ArrayList<>();
		for (Data data : currentProgram.getListing().getDefinedData(true)) {
			boolean wordOrLong = data.isPointer()
					|| (data.getDataType() instanceof AbstractIntegerDataType
						&& data.getLength() > 1);
			if (isOdd(data.getAddress()) && wordOrLong) {
				odd.add(data);
			}
		}
		for (Data data : odd) {
			clearListing(data.getMinAddress(), data.getMaxAddress());
		}
		println("odd-address data cleared: " + odd.size());
	}

	/**
	 * Disassembles where an instruction in a function flows to bytes in
	 * its own block that hold no instruction: flow blocked by data, or by
	 * stray code decoded from the middle of a real instruction.  Only what
	 * is in no function is cleared in the way, so that a function analysis
	 * got wrong cannot clear real code.  Returns how many were resumed.
	 */
	private int resumeBlockedFlow() throws Exception {
		int resumed = 0;
		boolean changed = true;
		while (changed) {
			changed = false;
			for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
				if (getFunctionContaining(instruction.getAddress()) == null) {
					continue;
				}
				MemoryBlock block = currentProgram.getMemory().getBlock(instruction.getAddress());
				List<Address> targets = new ArrayList<>(List.of(instruction.getFlows()));
				if (instruction.getFallThrough() != null) {
					targets.add(instruction.getFallThrough());
				}
				for (Address target : targets) {
					if (!block.contains(target) || isOdd(target)
							|| getInstructionAt(target) != null) {
						continue;
					}
					repairFrom(List.of(target), new AddressSet(target, target.add(ENTRY_WINDOW)),
						true);
					if (getInstructionAt(target) != null) {
						resumed++;
						changed = true;
					}
				}
			}
		}
		if (resumed > 0) {
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				CreateFunctionCmd.fixupFunctionBody(currentProgram, function, monitor);
			}
		}
		println("blocked flows resumed: " + resumed);
		return resumed;
	}

	/**
	 * Removes unnamed functions analysis made in data -- a "bsr" decoded
	 * from a table -- in one pass, so that a removal cannot make another
	 * function look like data.  A function is data if it starts at an odd
	 * address, which no 68000 instruction can, or if its own code reaches
	 * an odd address or bytes that do not decode; and no instruction in a
	 * function calls it.  Being called only from instructions in no
	 * function is not enough: real code Ghidra did not make a function of
	 * is that too.
	 */
	private void removeDataFunctions() throws Exception {
		List<Function> data = new ArrayList<>();
		for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
			if (function.getSymbol().getSource() == SourceType.DEFAULT
					&& !calledFromFunction(function)
					&& (isOdd(function.getEntryPoint()) || reachesNonCode(function))) {
				data.add(function);
			}
		}
		boolean dryRun = List.of(getScriptArgs()).contains(DRY_RUN);
		if (!dryRun && data.size() > MAX_DATA_FUNCTIONS) {
			throw new IllegalStateException(data.size() + " functions look like data, more than "
				+ MAX_DATA_FUNCTIONS + "; the rule is suspect, nothing removed");
		}
		for (Function function : data) {
			println((dryRun ? "would remove" : "removing") + " function " + function.getName()
				+ " in data");
			if (!dryRun) {
				removeFunction(function);
			}
		}
		println("functions in data " + (dryRun ? "found: " : "removed: ") + data.size());
	}

	private static boolean isOdd(Address address) {
		return (address.getOffset() & 1) != 0;
	}

	private boolean calledFromFunction(Function function) {
		for (Reference reference : getReferencesTo(function.getEntryPoint())) {
			if (getFunctionContaining(reference.getFromAddress()) != null) {
				return true;
			}
		}
		return false;
	}

	/**
	 * True if an instruction of function flows to an odd address, or to an
	 * address in its own block with no instruction.  Flow into another
	 * block, such as a call to the installer in RAM, does not count.
	 */
	private boolean reachesNonCode(Function function) {
		MemoryBlock block = currentProgram.getMemory().getBlock(function.getEntryPoint());
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(function.getBody(), true)) {
			List<Address> targets = new ArrayList<>(List.of(instruction.getFlows()));
			if (instruction.getFallThrough() != null) {
				targets.add(instruction.getFallThrough());
			}
			for (Address target : targets) {
				if (isOdd(target) || (block.contains(target) && getInstructionAt(target) == null)) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Ends flow at the instructions that do not come back, by pointing each
	 * one's fall-through at itself, and says why in a comment the
	 * decompiler prints.  Ghidra gives them a fall-through into what
	 * follows, another function or a string.  The decompiler follows an
	 * instruction's p-code, not a removed fall-through, and would carry
	 * the registers on into that; a fall-through override becomes a branch
	 * in the p-code, so the instruction reads as a loop on itself.
	 *
	 * - stop waits for an interrupt and resumes after it; the firmware
	 *   uses it to end a failure path.
	 * - trap #5 ends every interrupt handler, in the primary and the
	 *   installer, in place of rte: the kernel's return from interrupt.
	 *   This is read from those handlers, not documented.
	 */
	private void endFlowAtNoReturn() throws Exception {
		int count = 0;
		for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
			Address address = instruction.getAddress();
			String comment = noReturnComment(instruction);
			if (comment == null || address.equals(instruction.getFallThrough())) {
				continue;
			}
			Address next = instruction.getMaxAddress().add(1);
			instruction.setFallThrough(address);
			setPreComment(address, comment);
			Function function = getFunctionContaining(address);
			if (function != null) {
				CreateFunctionCmd.fixupFunctionBody(currentProgram, function, monitor);
			}
			if (getFunctionContaining(next) == null && isPrologue(next)) {
				disassemble(next);
				createFunction(next, null);
			}
			count++;
		}
		println("flows ended at instructions that do not return: " + count);
	}

	/** The comment for an instruction that does not come back, or null. */
	private String noReturnComment(Instruction instruction) throws Exception {
		if (instruction.getMnemonicString().equals(STOP)) {
			return "stop: halts here; the loop stands for not continuing";
		}
		if ((getShort(instruction.getAddress()) & 0xffff) == TRAP_RETURN_FROM_INTERRUPT) {
			return "trap #5: kernel return from interrupt; does not come back";
		}
		return null;
	}

	private void mapMemory() throws Exception {
		Memory memory = currentProgram.getMemory();
		MemoryBlock rom = memory.getBlock(toAddr(0));
		rom.setName("ROM");
		rom.setRead(true);
		rom.setWrite(false);
		rom.setExecute(true);
		for (Region region : REGIONS) {
			if (memory.getBlock(toAddr(region.base())) != null) {
				continue;
			}
			MemoryBlock block = memory.createUninitializedBlock(region.name(),
				toAddr(region.base()), region.size(), false);
			block.setRead(true);
			block.setWrite(true);
			block.setExecute(false);
			block.setVolatile(region.io());
		}
	}

	private static String vectorName(int vector) {
		String name = VECTOR_NAMES.get(vector);
		if (name != null) {
			return name;
		}
		if (vector >= AUTOVECTOR_FIRST && vector <= AUTOVECTOR_LAST) {
			return "autovector_" + (vector - AUTOVECTOR_FIRST + 1);
		}
		if (vector >= TRAP_FIRST && vector <= TRAP_LAST) {
			return "trap_" + (vector - TRAP_FIRST);
		}
		if (vector >= USER_FIRST) {
			return "user_" + vector;
		}
		return "reserved_" + vector;
	}

	/**
	 * Types the 256 longs at base as pointers and makes a function at each
	 * handler in ROM.  The handler most vectors share is named the default.
	 */
	private void labelVectors(long base, String prefix) throws Exception {
		Address table = toAddr(base);
		createLabel(table, prefix + "_vectors", true, SourceType.USER_DEFINED);
		for (int vector = 0; vector < VECTOR_COUNT; vector++) {
			Address slot = table.add(vector * 4L);
			clearListing(slot, slot.add(3));
			createData(slot, PointerDataType.dataType);
		}
		long[] targets = vectorTargets(base);
		long defaultHandler = defaultHandler(targets);
		MemoryBlock rom = currentProgram.getMemory().getBlock(toAddr(0));
		for (int vector = 1; vector < VECTOR_COUNT; vector++) {
			Address target = toAddr(targets[vector]);
			if (!rom.contains(target)) {
				continue;
			}
			String name = targets[vector] == defaultHandler
					? prefix + "_default_exception"
					: prefix + "_" + vectorName(vector);
			disassemble(target);
			Function function = getFunctionAt(target);
			if (function == null) {
				createFunction(target, name);
			}
			else if (function.getSymbol().getSource() == SourceType.DEFAULT) {
				function.setName(name, SourceType.USER_DEFINED);
			}
		}
	}

	/** Each vector's handler address, from the table at base. */
	private long[] vectorTargets(long base) throws Exception {
		long[] targets = new long[VECTOR_COUNT];
		for (int vector = 0; vector < VECTOR_COUNT; vector++) {
			targets[vector] = getInt(toAddr(base + vector * 4L)) & 0xffffffffL;
		}
		return targets;
	}

	/** The handler most vectors share. */
	private static long defaultHandler(long[] targets) {
		Map<Long, Integer> uses = new TreeMap<>();
		for (long target : targets) {
			uses.merge(target, 1, Integer::sum);
		}
		return uses.entrySet().stream().max(Map.Entry.comparingByValue()).orElseThrow().getKey();
	}

	/**
	 * Adds to each exception handler's comment that it is not C, which
	 * vectors enter it and how it returns, after the names have given it
	 * theirs, unless that comment already says it is not C.
	 */
	private void commentVectors(long base, String prefix) throws Exception {
		long[] targets = vectorTargets(base);
		long defaultHandler = defaultHandler(targets);
		for (int vector = 1; vector < VECTOR_COUNT; vector++) {
			Function function = getFunctionAt(toAddr(targets[vector]));
			if (function == null) {
				continue;
			}
			String existing = function.getComment();
			if (existing == null) {
				function.setComment(commentText(vectorComment(prefix, targets, vector, defaultHandler)));
			}
			else if (!existing.contains(NOT_C)) {
				function.setComment(existing + "\n"
					+ commentText(vectorComment(prefix, targets, vector, defaultHandler)));
			}
			if (propertyMap(CONVENTION_MAP).getString(function.getEntryPoint()) == null) {
				record(function, EXCEPTION_CONVENTION, "");
			}
		}
	}

	/**
	 * Gives the function at address the comment note, if it has none: a
	 * name from the names file, applied first, keeps its own.  One an
	 * earlier run left with a bare comment end is written again.
	 */
	private void describe(Address address, String note) {
		Function function = getFunctionAt(address);
		if (function != null
			&& (function.getComment() == null || function.getComment().contains(COMMENT_END))) {
			function.setComment(commentText(note));
		}
	}

	/** The end of a C comment, which a Forth word's name can hold: * / is one. */
	private static final String COMMENT_END = "*/";

	/** text with each comment end broken, so that the exported C's comment holds it. */
	private static String commentText(String text) {
		return text.replace(COMMENT_END, "*\\/");
	}

	/** The field at index, or "" past the row's end. */
	private static String field(String[] fields, int index) {
		return fields.length > index ? fields[index] : "";
	}

	/**
	 * A names-file function's comment: its note, its convention's line if
	 * it is not C, and its source, each on its own line; the source is left
	 * out when it is only the function's callers.
	 */
	private static String functionComment(String[] fields, Map<String, String> convention) {
		List<String> lines = new ArrayList<>();
		String note = field(fields, NOTE_FIELD);
		if (!note.isBlank()) {
			lines.add(note);
		}
		String rendered = renderConvention(convention);
		if (rendered != null) {
			lines.add(rendered);
		}
		if (!fields[SOURCE_FIELD].equals(CONTEXT)) {
			lines.add(fields[SOURCE_FIELD]);
		}
		return String.join("\n", lines);
	}

	/**
	 * The convention's key=value pairs, checked: every key known, the kind
	 * and every confidence one of theirs.  Empty for no convention.
	 */
	private static Map<String, String> parseConvention(String text, String line) {
		Map<String, String> convention = new LinkedHashMap<>();
		if (text.isBlank()) {
			return convention;
		}
		for (String pair : text.split(PAIR_SEPARATOR)) {
			int equals = pair.indexOf('=');
			String key = equals < 0 ? pair : pair.substring(0, equals).strip();
			String value = equals < 0 ? "" : pair.substring(equals + 1).strip();
			String base = key.endsWith(CONFIDENCE_SUFFIX)
					? key.substring(0, key.length() - CONFIDENCE_SUFFIX.length())
					: key;
			if (!CONVENTION_KEYS.contains(base) || value.isEmpty()
					|| (key.equals(KIND) && !KINDS.containsKey(value))
					|| ((key.equals(CONFIDENCE) || key.endsWith(CONFIDENCE_SUFFIX))
						&& !CONFIDENCES.contains(value))) {
				throw new IllegalArgumentException("bad convention pair \"" + pair + "\": " + line);
			}
			convention.put(key, value);
		}
		if (!convention.containsKey(KIND) || !convention.containsKey(CONFIDENCE)) {
			throw new IllegalArgumentException("a convention needs its kind and confidence: "
				+ line);
		}
		return convention;
	}

	/** True if the convention says the function is not C. */
	private static boolean notC(Map<String, String> convention) {
		return convention.containsKey(KIND) && !convention.get(KIND).equals(C);
	}

	/** The comment's line for a function that is not C, or null for one that is. */
	private static String renderConvention(Map<String, String> convention) {
		if (!notC(convention)) {
			return null;
		}
		StringBuilder line = new StringBuilder(NOT_C + " (" + convention.get(KIND) + "; "
			+ convention.get(CONFIDENCE) + "): " + KINDS.get(convention.get(KIND)) + ".");
		for (String key : CONVENTION_KEYS.subList(2, CONVENTION_KEYS.size() - 1)) {
			if (convention.containsKey(key)) {
				String own = convention.get(key + CONFIDENCE_SUFFIX);
				line.append("  ").append(Character.toUpperCase(key.charAt(0)))
					.append(key.substring(1)).append(own == null ? "" : " (" + own + ")")
					.append(": ").append(convention.get(key)).append(".");
			}
		}
		return line.toString();
	}

	/** The confidence of one key: its own, or the convention's. */
	private static String confidenceOf(Map<String, String> convention, String key) {
		return convention.getOrDefault(key + CONFIDENCE_SUFFIX, convention.get(CONFIDENCE));
	}

	/**
	 * The prototype to apply: a C function's own, with out's register for
	 * its result if one is given; for one not C, one built from in and out
	 * when every item has a type and both are trusted; otherwise null.
	 */
	private static String prototypeFor(String name, String prototype,
			Map<String, String> convention, String line) {
		if (!notC(convention)) {
			if (prototype.isBlank()) {
				return null;
			}
			String out = convention.get("out");
			if (out == null || !TRUSTED.contains(confidenceOf(convention, "out"))) {
				return prototype;
			}
			return prototype + " @ " + out.split(" ")[0];
		}
		if (!prototype.isBlank()) {
			throw new IllegalArgumentException(
				"a function that is not C takes its prototype from in and out: " + line);
		}
		if (!convention.containsKey("in") && !convention.containsKey("out")) {
			return null;
		}
		for (String key : List.of("in", "out")) {
			if (convention.containsKey(key) && !TRUSTED.contains(confidenceOf(convention, key))) {
				return null;
			}
		}
		List<String> arguments = new ArrayList<>();
		for (String item : items(convention.get("in"))) {
			String[] parts = registerItem(item);
			if (parts == null) {
				return null;
			}
			arguments.add(parts[1] + " @ " + parts[0]);
		}
		String returned = "void";
		String returnRegister = "";
		List<String> outs = items(convention.get("out"));
		if (outs.size() > 1) {
			return null;
		}
		if (outs.size() == 1) {
			// A result's item is its register, its type and optionally a name.
			String[] words = outs.get(0).strip().split("\\s+", 2);
			Matcher register = REGISTER.matcher(words[0]);
			if (!register.matches() || words.length < 2) {
				return null;
			}
			String type = words[1].strip();
			returned = type.contains(" ") ? type.replaceFirst("\\s*\\b\\w+$", "").strip() : type;
			returnRegister = " @ " + register.group(1);
		}
		return returned + " " + name + "(" + (arguments.isEmpty() ? "void"
			: String.join(", ", arguments)) + ")" + returnRegister;
	}

	/** A list's items, none for an absent key. */
	private static List<String> items(String list) {
		return list == null ? List.of() : List.of(list.split(ITEM_SEPARATOR));
	}

	/**
	 * An item "REGISTER TYPE NAME" as its register, without a width, and
	 * "TYPE NAME"; null if it has no register or no type and name.
	 */
	private static String[] registerItem(String item) {
		String[] words = item.strip().split("\\s+", 2);
		Matcher register = REGISTER.matcher(words[0]);
		if (!register.matches() || words.length < 2 || !words[1].strip().contains(" ")) {
			return null;
		}
		return new String[] { register.group(1), words[1].strip() };
	}

	/** The convention as a JSON object, its lists split into items. */
	private static String conventionJson(Map<String, String> convention) {
		List<String> members = new ArrayList<>();
		for (Map.Entry<String, String> pair : convention.entrySet()) {
			String key = pair.getKey();
			String value = pair.getValue();
			String json;
			switch (key) {
				case "in", "out" -> json = itemsJson(value, false);
				case "error" -> json = itemsJson(value, true);
				case "preserves", "results" -> json = "[" + String.join(",", items(value).stream()
					.map(SmartClockSetup::jsonString).toList()) + "]";
				default -> json = jsonString(value);
			}
			members.add(jsonString(key) + ":" + json);
		}
		return "{" + String.join(",", members) + "}";
	}

	/**
	 * Items as JSON objects: an in or out item's register, type and name,
	 * an error item's register, value and meaning; each part only if given.
	 */
	private static String itemsJson(String list, boolean error) {
		List<String> objects = new ArrayList<>();
		for (String item : items(list)) {
			String[] words = item.strip().split("\\s+");
			List<String> members = new ArrayList<>();
			int next = 0;
			if (REGISTER.matcher(words[0]).matches()) {
				members.add("\"register\":" + jsonString(words[0]));
				next = 1;
			}
			if (error) {
				if (next < words.length) {
					members.add("\"value\":" + jsonString(words[next]));
				}
				if (next + 1 < words.length) {
					members.add("\"meaning\":" + jsonString(String.join(" ",
						List.of(words).subList(next + 1, words.length))));
				}
			}
			else if (words.length - next == 1) {
				members.add("\"type\":" + jsonString(words[next]));
			}
			else if (words.length - next > 1) {
				// A pointer's stars, written against the name, belong to the type.
				String name = words[words.length - 1];
				String stars = name.replaceFirst("^(\\**).*$", "$1");
				members.add("\"type\":" + jsonString(String.join(" ",
					List.of(words).subList(next, words.length - 1)) + (stars.isEmpty() ? "" : " " + stars)));
				members.add("\"name\":" + jsonString(name.substring(stars.length())));
			}
			objects.add("{" + String.join(",", members) + "}");
		}
		return "[" + String.join(",", objects) + "]";
	}

	/** text as a JSON string. */
	private static String jsonString(String text) {
		StringBuilder json = new StringBuilder("\"");
		for (char c : text.toCharArray()) {
			switch (c) {
				case '"' -> json.append("\\\"");
				case '\\' -> json.append("\\\\");
				case '\n' -> json.append("\\n");
				default -> {
					if (c < ' ') {
						json.append(String.format("\\u%04x", (int) c));
					}
					else {
						json.append(c);
					}
				}
			}
		}
		return json.append('"').toString();
	}

	/**
	 * Keeps the function's convention, as JSON, and its note for the
	 * sidecar, and tags it if it is not C.  With no convention, any kept
	 * from before is dropped and the tag removed: a later script that
	 * knows its family records it again.
	 */
	private void record(Function function, String conventionJson, String note) throws Exception {
		Address entry = function.getEntryPoint();
		StringPropertyMap conventions = propertyMap(CONVENTION_MAP);
		if (conventionJson == null) {
			conventions.remove(entry);
			function.removeTag(NOT_C_TAG);
		}
		else {
			conventions.add(entry, conventionJson);
			if (conventionJson.contains("\"convention\":\"" + C + "\"")) {
				function.removeTag(NOT_C_TAG);
			}
			else {
				function.addTag(NOT_C_TAG);
			}
		}
		StringPropertyMap notes = propertyMap(NOTE_MAP);
		if (note.isBlank()) {
			notes.remove(entry);
		}
		else {
			notes.add(entry, note);
		}
	}

	/**
	 * Keeps, for a function that is not C, what its signature line cannot
	 * say -- its results, condition codes, errors and preserved registers --
	 * as "out D0:D1 double; returns ccr; ...", for SmartClockExportC.java.
	 */
	private void recordSignature(Function function, Map<String, String> convention)
			throws Exception {
		List<String> parts = new ArrayList<>();
		if (notC(convention)) {
			for (String key : SIGNATURE_KEYS) {
				if (convention.containsKey(key)) {
					parts.add(key + " " + convention.get(key));
				}
			}
		}
		StringPropertyMap signatures = propertyMap(SIGNATURE_MAP);
		if (parts.isEmpty()) {
			signatures.remove(function.getEntryPoint());
		}
		else {
			signatures.add(function.getEntryPoint(), String.join("; ", parts));
		}
	}

	/** The program's string property map name, made if it is not there. */
	private StringPropertyMap propertyMap(String name) throws Exception {
		StringPropertyMap map = currentProgram.getUsrPropertyManager().getStringPropertyMap(name);
		return map != null ? map : currentProgram.getUsrPropertyManager().createStringPropertyMap(name);
	}

	/**
	 * Says that the handler for vector is entered by the processor, through
	 * which vectors of the table, and how it returns.
	 */
	private String vectorComment(String prefix, long[] targets, int vector, long defaultHandler)
			throws Exception {
		String entered;
		if (targets[vector] == defaultHandler) {
			entered = "every vector of the " + prefix + " table without a handler of its own";
		}
		else {
			List<String> vectors = new ArrayList<>();
			for (int other = 1; other < VECTOR_COUNT; other++) {
				if (targets[other] == targets[vector]) {
					vectors.add(other + " (" + vectorName(other) + ")");
				}
			}
			entered = "the " + prefix + " table's vector" + (vectors.size() > 1 ? "s " : " ")
				+ String.join(", ", vectors);
		}
		String returns = "";
		Function function = getFunctionAt(toAddr(targets[vector]));
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(function.getBody(), true)) {
			int opcode = getShort(instruction.getAddress()) & 0xffff;
			if (opcode == RTE) {
				returns = "; returns with rte";
				break;
			}
			if (opcode == TRAP_RETURN_FROM_INTERRUPT) {
				returns = "; returns through trap #5, the kernel's return from interrupt";
				break;
			}
		}
		return "Not a C function: an exception handler, entered by the processor through "
			+ entered + ", not called" + returns + ".";
	}

	/**
	 * Types the ROM's string tables as strings, removing code that analysis
	 * disassembled in them: a 0x61 ('a') is a bsr, so code begun in a
	 * string "calls" on through the table.  A table is a run of text bytes
	 * long enough and holding enough NUL-terminated strings; within it
	 * only those strings are touched, since the run's ends can take in
	 * real code ("NV" and two NULs is link.w A6,#0).  A string under a
	 * function named here or by hand is left alone.
	 */
	private void typeStrings() throws Exception {
		int typed = 0;
		for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
			if (block.isInitialized() && block.isExecute()) {
				typed += typeStrings(block);
			}
		}
		println("strings typed: " + typed);
	}

	/** Types the string tables in block; returns how many strings were typed. */
	private int typeStrings(MemoryBlock block) throws Exception {
		byte[] bytes = new byte[(int) block.getSize()];
		block.getBytes(block.getStart(), bytes);
		Address base = block.getStart();
		int typed = 0;
		int position = 0;
		while (position < bytes.length) {
			if (!isText(bytes[position])) {
				position++;
				continue;
			}
			int start = position;
			while (position < bytes.length && isText(bytes[position])) {
				position++;
			}
			List<int[]> strings = stringsIn(bytes, start, position);
			if (position - start < MIN_STRING_TABLE_BYTES
					|| strings.size() < MIN_STRING_TABLE_STRINGS) {
				continue;
			}
			for (int[] string : strings) {
				if (typeString(base.add(string[0]), base.add(string[1]))) {
					typed++;
				}
			}
		}
		return typed;
	}

	private static boolean isText(byte value) {
		int b = value & 0xff;
		return (b >= ' ' && b <= '~') || b == 0 || b == '\t' || b == '\n' || b == '\r';
	}

	/** Returns [first, NUL] offsets of the strings of MIN_STRING_LENGTH or more in [start, end). */
	private static List<int[]> stringsIn(byte[] bytes, int start, int end) {
		List<int[]> strings = new ArrayList<>();
		int first = start;
		for (int i = start; i < end; i++) {
			if (bytes[i] != 0) {
				continue;
			}
			if (i - first >= MIN_STRING_LENGTH) {
				strings.add(new int[] { first, i });
			}
			first = i + 1;
		}
		return strings;
	}

	/**
	 * Types [first, nul] as a C string unless a named function lies in it
	 * or an array or structure, as a names-file label types a table, holds
	 * it.  Leading bytes that belong to a function begun before the string,
	 * code running into it with no NUL between, are left out.
	 */
	private boolean typeString(Address first, Address nul) throws Exception {
		Data holder = getDataContaining(first);
		if (holder != null && (holder.getDataType() instanceof Array
			|| holder.getDataType() instanceof Structure)) {
			return false;
		}
		Instruction leading = getInstructionContaining(first);
		while (leading != null && first.compareTo(nul) < 0) {
			Function owner = getFunctionContaining(leading.getAddress());
			if (owner == null || owner.getEntryPoint().compareTo(first) >= 0) {
				break;
			}
			first = leading.getMaxAddress().add(1);
			leading = getInstructionContaining(first);
		}
		if (nul.subtract(first) < MIN_STRING_LENGTH) {
			return false;
		}
		AddressSet range = new AddressSet(first, nul);
		for (Function function : currentProgram.getFunctionManager().getFunctions(range, true)) {
			if (function.getSymbol().getSource() != SourceType.DEFAULT) {
				return false;
			}
		}
		Data existing = getDataAt(first);
		if (existing != null && existing.hasStringValue() && existing.getMaxAddress().equals(nul)) {
			return false;
		}
		for (Function function : currentProgram.getFunctionManager().getFunctions(range, true)) {
			println("removing function " + function.getName() + " inside string at " + first);
			removeFunction(function);
		}
		clearListing(first, nul);
		createData(first, TerminatedStringDataType.dataType);
		return true;
	}

	/**
	 * Finds jmp (d8,PC,Xn) instructions not yet handled, and for each one of
	 * the known shape types its table, points the jmp at its cases and
	 * disassembles them.  Returns how many switches were added.
	 */
	private int recoverSwitches(List<Switch> switches, List<Address> unrecognized)
			throws Exception {
		int added = 0;
		ReferenceManager references = currentProgram.getReferenceManager();
		for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
			if (monitor.isCancelled()) {
				break;
			}
			Address jmp = instruction.getAddress();
			if ((getShort(jmp) & 0xffff) != JMP_PC_INDEX || unrecognized.contains(jmp)
					|| switches.stream().anyMatch(s -> s.jmp().equals(jmp))) {
				continue;
			}
			Switch recovered = decodeSwitch(jmp);
			if (recovered == null) {
				println("unrecognized switch shape at " + jmp);
				unrecognized.add(jmp);
				continue;
			}
			for (Reference reference : references.getReferencesFrom(jmp)) {
				if (reference.getReferenceType().isFlow()) {
					references.delete(reference);
				}
			}
			Address tableEnd = recovered.table().add(recovered.targets().size() * 2L - 1);
			removeFunctionsIn(recovered.table(), tableEnd);
			clearListing(recovered.table(), tableEnd);
			for (int i = 0; i < recovered.targets().size(); i++) {
				createData(recovered.table().add(i * 2L), WordDataType.dataType);
			}
			createLabel(recovered.table(), SWITCH_NAMESPACE_PREFIX + jmp, true, SourceType.USER_DEFINED);
			for (Address target : recovered.targets()) {
				references.addMemoryReference(jmp, target, RefType.COMPUTED_JUMP,
					SourceType.USER_DEFINED, 0);
			}
			repairFrom(recovered.targets(), new AddressSet(recovered.table(),
				tableEnd.add(CASE_BODY_WINDOW)), false);
			switches.add(recovered);
			added++;
		}
		return added;
	}

	/**
	 * Disassembles from starts known to be code, through whatever analysis
	 * left in the way.  For a switch, the starts are its cases: analysis
	 * places this idiom's table late, over the case bodies, and the words
	 * it types there, and the code it decodes from the wrong case
	 * addresses, overlap the real instructions.  For a named entry, the
	 * start is what a table in the image vouches for.  Following only flow
	 * from instructions reached from the starts, each address within window
	 * gets its true instruction: whatever overlaps it, but a string, is
	 * cleared first -- with strayOnly, but code in a function too, when
	 * the starts are only where other code flows.
	 */
	private void repairFrom(List<Address> starts, AddressSet window, boolean strayOnly)
			throws Exception {
		PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
		Deque<Address> pending = new ArrayDeque<>(starts);
		Set<Address> visited = new HashSet<>();
		while (!pending.isEmpty()) {
			Address address = pending.pop();
			if (!window.contains(address) || isOdd(address) || !visited.add(address)) {
				continue;
			}
			Instruction instruction = getInstructionAt(address);
			if (instruction == null) {
				PseudoInstruction decoded = decoder.disassemble(address);
				if (decoded == null) {
					continue;
				}
				Address last = address.add(decoded.getLength() - 1);
				if (overlapsString(address, last)
						|| (strayOnly && overlapsFunctionCode(address, last))) {
					continue;
				}
				clearListing(address, last);
				disassemble(address);
				instruction = getInstructionAt(address);
				if (instruction == null) {
					continue;
				}
			}
			pending.addAll(List.of(instruction.getFlows()));
			if (instruction.getFallThrough() != null) {
				pending.add(instruction.getFallThrough());
			}
		}
	}

	private boolean overlapsFunctionCode(Address start, Address end) {
		Instruction first = getInstructionContaining(start);
		if (first != null && getFunctionContaining(first.getAddress()) != null) {
			return true;
		}
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(new AddressSet(start, end), true)) {
			if (getFunctionContaining(instruction.getAddress()) != null) {
				return true;
			}
		}
		return false;
	}

	private boolean overlapsString(Address start, Address end) {
		for (Data data : currentProgram.getListing().getDefinedData(new AddressSet(start, end), true)) {
			if (data.hasStringValue()) {
				return true;
			}
		}
		Data first = getDataContaining(start);
		return first != null && first.hasStringValue();
	}

	/** Removes functions that analysis started inside what is really a table. */
	private void removeFunctionsIn(Address start, Address end) {
		for (Function function : currentProgram.getFunctionManager()
				.getFunctions(new AddressSet(start, end), true)) {
			println("removing function " + function.getName() + " inside table at " + start);
			removeFunction(function);
		}
	}

	/**
	 * Recovers the kernel's service dispatch: disassembles each bra.w in
	 * its table, records the jsr as calling each handler, and makes a
	 * function of each.
	 */
	private void recoverServiceTables() throws Exception {
		int recovered = 0;
		ReferenceManager references = currentProgram.getReferenceManager();
		for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
			Address jsr = instruction.getAddress();
			if ((getShort(jsr) & 0xffff) != JSR_PC_INDEX) {
				continue;
			}
			Address table = jsr.add(2 + (byte) getByte(jsr.add(3)));
			int services = findCompareBound(jsr, SERVICE_BOUND_SEARCH_BYTES);
			List<Address> handlers = new ArrayList<>();
			for (int i = 0; i < services; i++) {
				Address entry = table.add((long) i * BRA_W_LENGTH);
				if ((getShort(entry) & 0xffff) != BRA_W) {
					handlers.clear();
					break;
				}
				handlers.add(entry.add(2 + getShort(entry.add(2))));
			}
			if (handlers.isEmpty()) {
				println("unrecognized service table at " + jsr);
				continue;
			}
			clearListing(table, table.add((long) services * BRA_W_LENGTH - 1));
			for (int i = 0; i < services; i++) {
				disassemble(table.add((long) i * BRA_W_LENGTH));
			}
			for (Address handler : new HashSet<>(handlers)) {
				references.addMemoryReference(jsr, handler, RefType.COMPUTED_CALL,
					SourceType.USER_DEFINED, 0);
				disassemble(handler);
				if (getFunctionAt(handler) == null) {
					createFunction(handler, null);
				}
			}
			recovered++;
		}
		println("service tables recovered: " + recovered);
	}

	/** Decodes the switch at jmp, or returns null if its shape is not the known idiom. */
	private Switch decodeSwitch(Address jmp) throws Exception {
		Address table = jmp.add(2 + (byte) getByte(jmp.add(3)));
		Address move = (getShort(jmp.subtract(2)) & 0xffff) == NOP
				? jmp.subtract(6)
				: jmp.subtract(4);
		if ((getShort(move) & MOVEW_PC_INDEX_MASK) != MOVEW_PC_INDEX) {
			return null;
		}
		long bias = (table.getOffset() - move.add(2 + (byte) getByte(move.add(3))).getOffset());
		if (bias > 0 || bias % 2 != 0) {
			return null;
		}
		int cases;
		if (bias < 0) {
			if (!hasBranchHigh(move)) {
				return null;
			}
			cases = (int) (-bias / 2) + 1;
		}
		else {
			cases = findCompareBound(move, BOUND_SEARCH_BYTES);
		}
		boolean bounded = cases > 0;
		if (!bounded) {
			cases = MAX_UNBOUNDED_CASES;
		}
		MemoryBlock block = currentProgram.getMemory().getBlock(table);
		List<Address> targets = new ArrayList<>();
		Address lowest = null;
		for (int i = 0; i < cases; i++) {
			Address entry = table.add(i * 2L);
			if (lowest != null && entry.compareTo(lowest) >= 0) {
				if (bounded) {
					println("switch at " + jmp + ": " + cases + " cases overrun case bodies");
					return null;
				}
				break;
			}
			Address target = table.add(getShort(entry));
			if (!block.contains(target) || (target.getOffset() & 1) != 0) {
				if (bounded) {
					return null;
				}
				break;
			}
			targets.add(target);
			if (target.compareTo(table) > 0 && (lowest == null || target.compareTo(lowest) < 0)) {
				lowest = target;
			}
		}
		if (!bounded) {
			println("switch at " + jmp + ": no bound check, " + targets.size()
				+ " cases from table extent");
		}
		return targets.isEmpty() ? null : new Switch(jmp, table, (int) (bias / 2), targets);
	}

	/** True if a bhi of any length immediately precedes the table load at move. */
	private boolean hasBranchHigh(Address move) throws Exception {
		for (int length : BHI_LENGTHS) {
			if ((getByte(move.subtract(length)) & 0xff) == BHI_HIGH_BYTE) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Returns the case count from a cmpi #n,Dn or cmpa #n,An followed by a
	 * bhi in the reach bytes before the table load, or 0 if none is found.
	 */
	private int findCompareBound(Address load, int reach) throws Exception {
		for (int back = 2; back <= reach; back += 2) {
			Address at = load.subtract(back);
			int opcode = getShort(at) & 0xffff;
			boolean isLong = (opcode & CMPI_REGISTER_MASK) == CMPI_L
					|| (opcode & CMPA_REGISTER_MASK) == CMPA_L;
			boolean isWord = (opcode & CMPI_REGISTER_MASK) == CMPI_W
					|| (opcode & CMPA_REGISTER_MASK) == CMPA_W;
			if (isLong && ((getByte(at.add(6)) & 0xff) == BHI_HIGH_BYTE)) {
				return getInt(at.add(2)) + 1;
			}
			if (isWord && ((getByte(at.add(4)) & 0xff) == BHI_HIGH_BYTE)) {
				return getShort(at.add(2)) + 1;
			}
		}
		return 0;
	}

	/**
	 * Returns the function owning the switch at jmp, with its body
	 * recomputed now that the case targets are known.  A function made
	 * before its switch was recovered stops at the jmp, so a jmp in no
	 * function is reached through the nearest link.w A6 before it, the
	 * frame setup this compiler opens every non-leaf function with.
	 */
	private Function switchFunction(Address jmp) throws Exception {
		Function function = getFunctionContaining(jmp);
		if (function == null) {
			Address frame = findFrame(jmp);
			if (frame == null) {
				return null;
			}
			function = getFunctionAt(frame);
			if (function == null) {
				disassemble(frame);
				function = createFunction(frame, null);
			}
			if (function == null) {
				return null;
			}
		}
		CreateFunctionCmd.fixupFunctionBody(currentProgram, function, monitor);
		return function.getBody().contains(jmp) ? function : null;
	}

	/**
	 * Returns the nearest function prologue before address in its block, or
	 * null: a link.w A6, or the movem.l register save a frameless function
	 * opens with.  A save directly after a link belongs to the link.
	 */
	private Address findFrame(Address address) throws Exception {
		MemoryBlock block = currentProgram.getMemory().getBlock(address);
		for (long back = 2; back <= FRAME_SEARCH_BYTES; back += 2) {
			Address at = address.subtract(back);
			if (!block.contains(at)) {
				return null;
			}
			int opcode = getShort(at) & 0xffff;
			if (opcode == LINK_A6) {
				return at;
			}
			if (isPrologue(at)) {
				Address link = at.subtract(LINK_LENGTH);
				return block.contains(link) && (getShort(link) & 0xffff) == LINK_A6 ? link : at;
			}
		}
		return null;
	}

	/** True if a function prologue, link.w A6 or a movem.l register save, starts at address. */
	private boolean isPrologue(Address address) throws Exception {
		int opcode = getShort(address) & 0xffff;
		return opcode == LINK_A6 || opcode == MOVEM_L_SAVE;
	}

	/**
	 * Records each switch's targets as a decompiler jump-table override,
	 * and its table, first index and entries in SWITCH_MAP: an override
	 * holds only the targets, so the decompiler labels the cases itself,
	 * and SmartClockExportC.java corrects the labels from this.
	 */
	private void overrideSwitches(List<Switch> switches) throws Exception {
		int stale = 0;
		for (Switch recovered : switches) {
			Function function = switchFunction(recovered.jmp());
			if (function == null) {
				println("switch at " + recovered.jmp() + " is in no function");
				continue;
			}
			new JumpTable(recovered.jmp(), new ArrayList<>(recovered.targets()), true, 0)
					.writeOverride(function);
			propertyMap(SWITCH_MAP).add(recovered.jmp(), recovered.table() + " " + recovered.first()
				+ " " + recovered.targets().size());
			stale += removeStaleCases(recovered);
		}
		if (stale > 0) {
			println("stale case labels " + (List.of(getScriptArgs()).contains(DRY_RUN)
					? "found: " : "removed: ") + stale);
		}
	}

	/**
	 * Removes the case labels analysis put at targets of its late reading of
	 * a switch's table, which are not the switch's cases.  Such a label can
	 * be another function's name, and deleting a function's name deletes
	 * the function, so that function is given back its default name in the
	 * global namespace instead.  With dry-run, only counts.  Returns how
	 * many were removed or given back.
	 */
	private int removeStaleCases(Switch recovered) throws Exception {
		SymbolTable symbols = currentProgram.getSymbolTable();
		Namespace namespace = symbols.getNamespace(SWITCH_NAMESPACE_PREFIX + recovered.jmp(),
			currentProgram.getGlobalNamespace());
		if (namespace == null) {
			return 0;
		}
		List<Symbol> stale = new ArrayList<>();
		for (Symbol symbol : symbols.getSymbols(namespace)) {
			if (symbol.getName().startsWith(CASE_LABEL_PREFIX)
					&& !recovered.targets().contains(symbol.getAddress())) {
				stale.add(symbol);
			}
		}
		if (List.of(getScriptArgs()).contains(DRY_RUN)) {
			return stale.size();
		}
		for (Symbol symbol : stale) {
			Function function = getFunctionAt(symbol.getAddress());
			if (function != null && function.getSymbol().equals(symbol)) {
				function.setParentNamespace(currentProgram.getGlobalNamespace());
				function.setName(null, SourceType.DEFAULT);
				println("function at " + symbol.getAddress() + " was named " + symbol.getName()
					+ "; given back its default name");
			}
			else {
				symbol.delete();
			}
		}
		return stale.size();
	}
}
