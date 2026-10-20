// Prepares a SmartClock CPU32 firmware image (Z3801A / Z3805A family)
// imported raw at address 0: memory map, peripheral register labels,
// both vector tables, string tables, the compiler's PC-relative switch
// tables, flow that analysis got wrong, and names.  Safe to run again.
//
// Arguments, any of:
//
//   dry-run
//       report the functions in data that would be removed, and do not
//       remove them; everything else is applied.
//   types=tools/ghidra/types/smartclock.h
//       C declarations of the firmware's structures, parsed into the
//       program before the names, so that names can use them.
//   names=tools/ghidra/names/IMAGE.tsv
//       address, "function" or "label", name, source and optionally a
//       function's C prototype or a label's data type, tab-separated:
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
//@category SmartClock

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Pattern;

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
import ghidra.program.model.data.AbstractIntegerDataType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.TerminatedStringDataType;
import ghidra.program.model.data.WordDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.JumpTable;
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
	private record PeripheralRegister(long address, String name, int size) {}

	/** A recovered switch: the jmp, its table, and each case target in index order. */
	private record Switch(Address jmp, Address table, List<Address> targets) {}

	private static final long PRIMARY_VECTORS = 0x10000;
	private static final long BOOT_VECTORS = 0x0;
	private static final int VECTOR_COUNT = 256;

	/** Opcode of jmp (d8,PC,Xn); the extension word follows. */
	private static final int JMP_PC_INDEX = 0x4efb;
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
	/** Naming arguments, each KIND=FILE; see the header. */
	private static final String NAMES = "names";
	private static final String SCPI = "scpi";
	private static final String PFORTH = "pforth";
	private static final String TYPES = "types";
	private static final String COMMENT = "#";
	private static final String NO_HANDLER = "-";
	private static final String FUNCTION = "function";
	/**
	 * Stack offsets are from SP at entry, where the return address occupies
	 * 0..3; arguments start at 4, each in a slot of at least 2 bytes.
	 */
	private static final int FIRST_ARGUMENT_OFFSET = 4;
	private static final int STACK_SLOT_MINIMUM = 2;
	/** A long or pointer argument's slot, the default convention's for every argument. */
	private static final int STACK_SLOT = 4;
	/** A register annotation in a prototype: "@ D0" or "@ D0:D1". */
	private static final Pattern ANNOTATION = Pattern.compile("\\s*@\\s*[A-Za-z0-9:]+");
	/** The names file's optional fifth field: a function's C prototype, or a label's data type. */
	private static final int PROTOTYPE_FIELD = 4;
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

	private static final PeripheralRegister[] REGISTERS = {
		// GPT, MC68331UM Table D-2
		new PeripheralRegister(0xfff900, "GPTMCR", 2), new PeripheralRegister(0xfff904, "GPT_ICR", 2),
		new PeripheralRegister(0xfff906, "DDRGP", 1), new PeripheralRegister(0xfff907, "PORTGP", 1),
		new PeripheralRegister(0xfff908, "OC1M", 1), new PeripheralRegister(0xfff909, "OC1D", 1),
		new PeripheralRegister(0xfff90a, "TCNT", 2), new PeripheralRegister(0xfff90c, "PACTL", 1),
		new PeripheralRegister(0xfff90d, "PACNT", 1), new PeripheralRegister(0xfff90e, "TIC1", 2),
		new PeripheralRegister(0xfff910, "TIC2", 2), new PeripheralRegister(0xfff912, "TIC3", 2),
		new PeripheralRegister(0xfff914, "TOC1", 2), new PeripheralRegister(0xfff916, "TOC2", 2),
		new PeripheralRegister(0xfff918, "TOC3", 2), new PeripheralRegister(0xfff91a, "TOC4", 2),
		new PeripheralRegister(0xfff91c, "TI4_O5", 2), new PeripheralRegister(0xfff91e, "TCTL1", 1),
		new PeripheralRegister(0xfff91f, "TCTL2", 1), new PeripheralRegister(0xfff920, "TMSK1", 1),
		new PeripheralRegister(0xfff921, "TMSK2", 1), new PeripheralRegister(0xfff922, "TFLG1", 1),
		new PeripheralRegister(0xfff923, "TFLG2", 1), new PeripheralRegister(0xfff924, "CFORC", 1),
		new PeripheralRegister(0xfff925, "PWMC", 1), new PeripheralRegister(0xfff926, "PWMA", 1),
		new PeripheralRegister(0xfff927, "PWMB", 1), new PeripheralRegister(0xfff928, "PWMCNT", 2),
		new PeripheralRegister(0xfff92a, "PWMBUFA", 1), new PeripheralRegister(0xfff92b, "PWMBUFB", 1),
		new PeripheralRegister(0xfff92c, "PRESCL", 2),
		// SIM, MC68331UM Table D-3
		new PeripheralRegister(0xfffa00, "SIMCR", 2), new PeripheralRegister(0xfffa02, "SIMTR", 2),
		new PeripheralRegister(0xfffa04, "SYNCR", 2), new PeripheralRegister(0xfffa07, "RSR", 1),
		new PeripheralRegister(0xfffa08, "SIMTRE", 2), new PeripheralRegister(0xfffa11, "PORTE0", 1),
		new PeripheralRegister(0xfffa13, "PORTE1", 1), new PeripheralRegister(0xfffa15, "DDRE", 1),
		new PeripheralRegister(0xfffa17, "PEPAR", 1), new PeripheralRegister(0xfffa19, "PORTF0", 1),
		new PeripheralRegister(0xfffa1b, "PORTF1", 1), new PeripheralRegister(0xfffa1d, "DDRF", 1),
		new PeripheralRegister(0xfffa1f, "PFPAR", 1), new PeripheralRegister(0xfffa21, "SYPCR", 1),
		new PeripheralRegister(0xfffa22, "PICR", 2), new PeripheralRegister(0xfffa24, "PITR", 2),
		new PeripheralRegister(0xfffa27, "SWSR", 1), new PeripheralRegister(0xfffa41, "PORTC", 1),
		new PeripheralRegister(0xfffa44, "CSPAR0", 2), new PeripheralRegister(0xfffa46, "CSPAR1", 2),
		new PeripheralRegister(0xfffa48, "CSBARBT", 2), new PeripheralRegister(0xfffa4a, "CSORBT", 2),
		new PeripheralRegister(0xfffa4c, "CSBAR0", 2), new PeripheralRegister(0xfffa4e, "CSOR0", 2),
		new PeripheralRegister(0xfffa50, "CSBAR1", 2), new PeripheralRegister(0xfffa52, "CSOR1", 2),
		new PeripheralRegister(0xfffa54, "CSBAR2", 2), new PeripheralRegister(0xfffa56, "CSOR2", 2),
		new PeripheralRegister(0xfffa58, "CSBAR3", 2), new PeripheralRegister(0xfffa5a, "CSOR3", 2),
		new PeripheralRegister(0xfffa5c, "CSBAR4", 2), new PeripheralRegister(0xfffa5e, "CSOR4", 2),
		new PeripheralRegister(0xfffa60, "CSBAR5", 2), new PeripheralRegister(0xfffa62, "CSOR5", 2),
		new PeripheralRegister(0xfffa64, "CSBAR6", 2), new PeripheralRegister(0xfffa66, "CSOR6", 2),
		new PeripheralRegister(0xfffa68, "CSBAR7", 2), new PeripheralRegister(0xfffa6a, "CSOR7", 2),
		new PeripheralRegister(0xfffa6c, "CSBAR8", 2), new PeripheralRegister(0xfffa6e, "CSOR8", 2),
		new PeripheralRegister(0xfffa70, "CSBAR9", 2), new PeripheralRegister(0xfffa72, "CSOR9", 2),
		new PeripheralRegister(0xfffa74, "CSBAR10", 2), new PeripheralRegister(0xfffa76, "CSOR10", 2),
		// QSM, MC68331UM Table D-13
		new PeripheralRegister(0xfffc00, "QSMCR", 2), new PeripheralRegister(0xfffc02, "QTEST", 2),
		new PeripheralRegister(0xfffc04, "QILR", 1), new PeripheralRegister(0xfffc05, "QIVR", 1),
		new PeripheralRegister(0xfffc08, "SCCR0", 2), new PeripheralRegister(0xfffc0a, "SCCR1", 2),
		new PeripheralRegister(0xfffc0c, "SCSR", 2), new PeripheralRegister(0xfffc0e, "SCDR", 2),
		new PeripheralRegister(0xfffc15, "PORTQS", 1), new PeripheralRegister(0xfffc16, "PQSPAR", 1),
		new PeripheralRegister(0xfffc17, "DDRQS", 1), new PeripheralRegister(0xfffc18, "SPCR0", 2),
		new PeripheralRegister(0xfffc1a, "SPCR1", 2), new PeripheralRegister(0xfffc1c, "SPCR2", 2),
		new PeripheralRegister(0xfffc1e, "SPCR3", 1), new PeripheralRegister(0xfffc1f, "SPSR", 1),
		// 68681 DUART on an 8-bit port at odd addresses: register n at 2n+1.
		// Read and write names differ; both are given where they do.
		new PeripheralRegister(0x200001, "DUART_MR1A_MR2A", 1), new PeripheralRegister(0x200003, "DUART_SRA_CSRA", 1),
		new PeripheralRegister(0x200005, "DUART_CRA", 1), new PeripheralRegister(0x200007, "DUART_RBA_TBA", 1),
		new PeripheralRegister(0x200009, "DUART_IPCR_ACR", 1), new PeripheralRegister(0x20000b, "DUART_ISR_IMR", 1),
		new PeripheralRegister(0x20000d, "DUART_CUR_CTUR", 1), new PeripheralRegister(0x20000f, "DUART_CLR_CTLR", 1),
		new PeripheralRegister(0x200011, "DUART_MR1B_MR2B", 1), new PeripheralRegister(0x200013, "DUART_SRB_CSRB", 1),
		new PeripheralRegister(0x200015, "DUART_CRB", 1), new PeripheralRegister(0x200017, "DUART_RBB_TBB", 1),
		new PeripheralRegister(0x200019, "DUART_IVR", 1), new PeripheralRegister(0x20001b, "DUART_IP_OPCR", 1),
		new PeripheralRegister(0x20001d, "DUART_START_SOPBC", 1), new PeripheralRegister(0x20001f, "DUART_STOP_ROPBC", 1),
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
		labelRegisters();
		labelVectors(BOOT_VECTORS, "boot");
		labelVectors(PRIMARY_VECTORS, "primary");
		typeStrings();
		analyzeChanges(currentProgram);
		repairFlow();
		removeDataFunctions();
		applyNames();
		repairFlow();
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
	 * further label, so nothing named earlier or by hand is overwritten.
	 */
	private void applyNames() throws Exception {
		for (String arg : getScriptArgs()) {
			if (arg.startsWith(TYPES + "=")) {
				String header = Files.readString(Path.of(arg.substring(TYPES.length() + 1)));
				new CParser(currentProgram.getDataTypeManager(), true, null).parse(header);
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
				Address address = toAddr(Long.decode(fields[0]));
				if (fields[1].equals(FUNCTION)) {
					nameFunction(address, fields[2], true, counts);
					if (fields.length > PROTOTYPE_FIELD && !fields[PROTOTYPE_FIELD].isBlank()) {
						applyPrototype(address, fields[PROTOTYPE_FIELD]);
					}
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
					nameFunction(toAddr(Long.decode(fields[1])), "scpi_query_" + path, false,
						counts);
				}
				if (!fields[2].equals(NO_HANDLER)) {
					nameFunction(toAddr(Long.decode(fields[2])), "scpi_set_" + path, false,
						counts);
				}
			}
			default -> {
				String[] fields = line.split(" ");
				String prefix = fields[0].equals("kernel") ? "forth_" : "diag_";
				nameFunction(toAddr(Long.decode(fields[2])), prefix + spell(fields[1]), false,
					counts);
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
	 * Gives the function at address the C prototype prototype, as the
	 * notes establish it.  A prototype with register annotations --
	 * "float f(float a @ D0, double b @ A0:A1) @ D0", a pair high
	 * register first -- puts each annotated value in those registers, in
	 * custom storage, and each argument without one on the stack, in
	 * order from the return address up.  This compiler pushes a char or
	 * short in a 2-byte slot, its value at the slot's start, where Ghidra's
	 * default convention would look at the end of a 4-byte slot; so a
	 * prototype with an argument narrower than 4 bytes is laid out this way
	 * too.  Any other takes the default convention.  Ghidra's default returns a float or double in FP0, but
	 * these images have no FPU and return them in D0 or D0:D1, so a
	 * function returning one needs its return annotated.
	 */
	private void applyPrototype(Address address, String prototype) throws Exception {
		FunctionDefinitionDataType signature = CParserUtils.parseSignature(
			(DataTypeManagerService) null, currentProgram, ANNOTATION.matcher(prototype).replaceAll(""),
			false);
		if (signature == null) {
			throw new IllegalArgumentException("cannot parse prototype at " + address + ": "
				+ prototype);
		}
		boolean narrow = false;
		for (ParameterDefinition argument : signature.getArguments()) {
			narrow |= argument.getDataType().getLength() < STACK_SLOT;
		}
		if (!prototype.contains("@") && !narrow) {
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
		ReturnParameterImpl result = returned.contains("@")
				? new ReturnParameterImpl(signature.getReturnType(),
					registerStorage(returned, address), currentProgram)
				: new ReturnParameterImpl(signature.getReturnType(), currentProgram);
		getFunctionAt(address).updateFunction(null, result, parameters,
			FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.IMPORTED);
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

	/** Types the data at address as declaration, a type the program knows, as "status_model *". */
	private void applyDataType(Address address, String declaration) throws Exception {
		DataTypeManager types = currentProgram.getDataTypeManager();
		DataType type = new DataTypeParser(types, types, null, AllowedDataTypes.ALL)
				.parse(declaration.strip());
		clearListing(address, address.add(type.getLength() - 1));
		createData(address, type);
	}

	/** Adds name as a label at address, unless it is there already. */
	private void label(Address address, String name, int[] counts) throws Exception {
		if (getSymbols(name, null).stream().anyMatch(s -> s.getAddress().equals(address))) {
			return;
		}
		createLabel(address, name, false, SourceType.IMPORTED);
		counts[NameCount.LABELED.ordinal()]++;
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

	private void labelRegisters() throws Exception {
		for (PeripheralRegister register : REGISTERS) {
			Address address = toAddr(register.address());
			createLabel(address, register.name(), true, SourceType.USER_DEFINED);
			DataType type = register.size() == 2 ? WordDataType.dataType : ByteDataType.dataType;
			clearListing(address, address.add(register.size() - 1));
			createData(address, type);
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
		Map<Long, Integer> uses = new TreeMap<>();
		long[] targets = new long[VECTOR_COUNT];
		for (int vector = 0; vector < VECTOR_COUNT; vector++) {
			Address slot = table.add(vector * 4L);
			clearListing(slot, slot.add(3));
			createData(slot, PointerDataType.dataType);
			targets[vector] = getInt(slot) & 0xffffffffL;
			uses.merge(targets[vector], 1, Integer::sum);
		}
		long defaultHandler = uses.entrySet().stream()
				.max(Map.Entry.comparingByValue()).orElseThrow().getKey();
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
	 * Types [first, nul] as a C string unless a named function lies in it.
	 * Leading bytes that belong to a function begun before the string, code
	 * running into it with no NUL between, are left out.
	 */
	private boolean typeString(Address first, Address nul) throws Exception {
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
			cases = findCompareBound(move);
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
		return targets.isEmpty() ? null : new Switch(jmp, table, targets);
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
	 * bhi in the bytes before the table load, or 0 if none is found.
	 */
	private int findCompareBound(Address move) throws Exception {
		for (int back = 2; back <= BOUND_SEARCH_BYTES; back += 2) {
			Address at = move.subtract(back);
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

	/** Records each switch's targets as a decompiler jump-table override. */
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
