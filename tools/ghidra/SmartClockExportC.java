// Writes the program as C for m68k-linux-gnu-gcc -std=gnu17 -mcpu=cpu32,
// to the file named by the script's first argument: the
// program's structures, packed and each size asserted; the decompiler's
// vocabulary; an extern and a .set to its address for every global the
// C names; a prototype for every function; then every function in
// address order.  Beside it, with .conventions.jsonl for .c, one JSON
// object a line for every function: its address and name, its note and
// the calling convention SmartClockSetup.java and SmartClockForth.java
// recorded, or "unknown"; and with .symbols.ld, a linker script placing
// the symbols the assembly makes for addresses in no routine.
//
// A function that is not C is its decompiled C under #if 0, for
// reading, its signature marked with a macro for its kind and each
// register in a comment, then the routine itself as a top-level
// __asm__ in gas's syntax, translated from Ghidra's operands, which
// tools/firmware/decompiled_check.py confirms is the image's own bytes.
// docs/firmware/README.md, "Decompiling with Ghidra", gives the rules.
//
//@category SmartClock

import java.io.File;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.decompiler.ClangNode;
import ghidra.app.decompiler.ClangTokenGroup;
import ghidra.app.decompiler.ClangVariableToken;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.database.data.PointerTypedefInspector;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.data.AbstractIntegerDataType;
import ghidra.program.model.data.AbstractStringDataType;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.Composite;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.DataTypeWriter;
import ghidra.program.model.data.Enum;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.data.Undefined;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighGlobal;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.util.StringPropertyMap;

public class SmartClockExportC extends GhidraScript {

	private static final int TIMEOUT_SECONDS = 120;

	/** SmartClockSetup.java's label for a switch's table. */
	private static final String SWITCH_TABLE_PREFIX = "switchD_";
	/** Where SmartClockSetup.java keeps the conventions, as JSON, and the notes. */
	private static final String CONVENTION_MAP = "SmartClock.convention";
	private static final String NOTE_MAP = "SmartClock.note";
	private static final String UNKNOWN = "{\"convention\":\"unknown\"}";
	private static final String SIGNATURE_MAP = "SmartClock.signature";
	private static final String C_SUFFIX = ".c";
	/** The decompiler's warning for a function with storage of its own. */
	private static final String UNKNOWN_CONVENTION = "/* WARNING: Unknown calling convention */";
	/** Each kind of convention that is not C, and the macro its signature carries. */
	private static final Map<String, String> KIND_MACROS = Map.of(
		"pforth", "__pforth",
		"register", "__regcall",
		"psos-service", "__regcall",
		"exception", "__exception",
		"other", "__naked",
		"undetermined", "__naked");
	private static final Set<String> TRUSTED = Set.of("run", "read", "documented");
	private static final Pattern KIND = Pattern.compile("\"convention\":\"([\\w-]+)\"");
	private static final Pattern CONFIDENCE = Pattern.compile("\"confidence\":\"(\\w+)\"");
	private static final Pattern IN_CONFIDENCE = Pattern.compile("\"in\\.confidence\":\"(\\w+)\"");
	/** The macros, defined once at the head of the file. */
	private static final String MACROS = """

		/*
		 * A function that is not C is its decompiled C under #if 0, for
		 * reading, then the routine itself in assembly.  The macro for its
		 * kind leads the signature; each parameter's register is in a comment.
		 * Its comment, and z3805a-3543b.conventions.jsonl, give the whole of it.
		 */
		#define __naked     __attribute__((naked))
		#define __regcall   __naked  /* arguments and results in registers */
		#define __pforth    __naked  /* A6 data stack, A5 user area's end, A4 dictionary, A3 input */
		#define __exception __attribute__((naked, interrupt)) /* entered by the processor */
		""";
	private static final String SIDECAR_SUFFIX = ".conventions.jsonl";
	/**
	 * Ghidra's own names for sizes and operations, so that gcc takes the
	 * decompiler's text (gnu17: code's empty parentheses take any
	 * arguments).  The CONCAT, SUB, ZEXT and SEXT families follow.
	 */
	private static final String PRELUDE = """

		/*
		 * The decompiler's vocabulary, for m68k-linux-gnu-gcc -std=gnu17.
		 * CONCATxy joins an x-byte and a y-byte value; SUBxy(v, n) is the
		 * y bytes of an x-byte value n bytes up from its least significant
		 * end, taken from its bits whatever its type.
		 */
		typedef unsigned int undefined3;
		typedef unsigned long long undefined5;
		typedef unsigned long long undefined7;
		typedef int int3;
		typedef unsigned int uint3;
		typedef long long int5;
		typedef long long int6;
		typedef long long int7;
		typedef unsigned long long uint5;
		typedef unsigned long long uint6;
		typedef unsigned long long uint7;
		typedef long long longlong;
		typedef unsigned long long ulonglong;
		typedef unsigned char unkfloat1;
		typedef unsigned short unkfloat2;
		typedef unsigned int unkfloat3;
		typedef unsigned long long unkfloat5;
		typedef unsigned long long unkfloat6;
		typedef unsigned long long unkfloat7;
		typedef int code();
		#define true 1
		#define false 0
		extern code *UNRECOVERED_JUMPTABLE;
		#define INFINITY __builtin_inff()
		#define NAN __builtin_nanf("")
		#define __ghidra_bits(v) ({ __typeof__((v) + 0) __v = (v); unsigned long long __b = 0; \
			__builtin_memcpy((char *)&__b + sizeof __b - sizeof __v, &__v, sizeof __v); __b; })
		#define __ghidra_mask(n) ((n) >= 8 ? ~0ULL : (1ULL << (8 * (n))) - 1)
		#define __ghidra_concat(y, a, b) \
			((__ghidra_bits(a) << (8 * (y))) | (__ghidra_bits(b) & __ghidra_mask(y)))
		#define __ghidra_sub(y, v, n) ((__ghidra_bits(v) >> (8 * (n))) & __ghidra_mask(y))
		#define CARRY1(a, b) ((unsigned char)((a) + (b)) < (unsigned char)(a))
		#define CARRY2(a, b) ((unsigned short)((a) + (b)) < (unsigned short)(a))
		#define CARRY4(a, b) ((unsigned int)((a) + (b)) < (unsigned int)(a))
		#define SCARRY1(a, b) ({ signed char __r; __builtin_add_overflow((signed char)(a), (signed char)(b), &__r); })
		#define SCARRY2(a, b) ({ short __r; __builtin_add_overflow((short)(a), (short)(b), &__r); })
		#define SCARRY4(a, b) ({ int __r; __builtin_add_overflow((int)(a), (int)(b), &__r); })
		#define SBORROW1(a, b) ({ signed char __r; __builtin_sub_overflow((signed char)(a), (signed char)(b), &__r); })
		#define SBORROW2(a, b) ({ short __r; __builtin_sub_overflow((short)(a), (short)(b), &__r); })
		#define SBORROW4(a, b) ({ int __r; __builtin_sub_overflow((int)(a), (int)(b), &__r); })
		#define __ghidra_piece(type, v, n) (*(type *)((char *)&(v) + (n)))
		typedef struct __attribute__((packed)) { unsigned int v : 24; } __ghidra_u3;
		typedef struct __attribute__((packed)) { unsigned long long v : 40; } __ghidra_u5;
		typedef struct __attribute__((packed)) { unsigned long long v : 48; } __ghidra_u6;
		typedef struct __attribute__((packed)) { unsigned long long v : 56; } __ghidra_u7;
		#define ABS(x) ((x) < 0 ? -(x) : (x))
		#define __m68k_trap(n) __asm__ volatile ("trap #" #n)
		static inline unsigned char bcdAdjust(unsigned char v) { return v; }
		""";

	@Override
	protected void run() throws Exception {
		File output = new File(getScriptArgs()[0]);
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);
		int written = 0;
		int bridged = 0;
		int failed = 0;
		try (PrintWriter writer = new PrintWriter(output)) {
			writer.println("/* " + currentProgram.getName() + ", "
				+ currentProgram.getLanguageID() + " */");
			writer.print(MACROS);
			writer.print(PRELUDE);
			writer.print(sizedOperations());
			writeTypes(writer);
			Map<String, String> declared = writeGlobals(writer);
			StringBuilder functions = new StringBuilder();
			Map<String, HighSymbol> referenced = new TreeMap<>();
			Map<String, DataType> used = new HashMap<>();
			Map<Address, String> madeSymbols = new TreeMap<>();
			List<String> prototypes = new ArrayList<>();
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				if (function.isThunk()) {
					String target = function.getThunkedFunction(true).getName();
					functions.append("\n/* ").append(function.getEntryPoint()).append(" */\n")
						.append("/* a thunk to ").append(target).append(" */\n");
					if (!function.getName().equals(target)) {
						functions.append("__asm__(\".set ").append(function.getName()).append(", ")
							.append(target).append("\");\n");
						prototypes.add("int " + function.getName() + "();");
					}
					continue;
				}
				DecompileResults results =
					decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
				functions.append("\n/* ").append(function.getEntryPoint()).append(" */\n");
				if (results.decompileCompleted()) {
					String c = indexedFunctions(identifiers(pieces(notC(function,
						results.getDecompiledFunction().getC()))));
					if (notCMacro(function) != null) {
						functions.append(READING).append(c).append("\n#endif\n")
							.append(assembly(function, declared, madeSymbols));
						Bridge bridge = bridge(function);
						if (bridge == null) {
							prototypes.add("int " + function.getName() + "();");
						}
						else {
							prototypes.add(bridge.prototype());
							functions.append(bridge.assembly());
							bridged++;
						}
					}
					else {
						functions.append(c);
						collectGlobals(results.getHighFunction(), referenced);
						collectUsedTypes(results.getCCodeMarkup(), used);
						prototypes.add(signature(c));
					}
					written++;
				}
				else {
					functions.append("/* " + function.getName() + ": decompilation failed: "
						+ results.getErrorMessage().strip() + " */\n");
					failed++;
				}
			}
			writeAdjustments(writer);
			writeReferencedGlobals(writer, referenced, declared, used, functions.toString());
			writeNamedByAddress(writer, functions, declared, used);
			writeSymbols(new File(base(output) + SYMBOLS_SUFFIX), madeSymbols);
			writer.println();
			for (String prototype : prototypes) {
				if (prototype != null) {
					writer.println(prototype);
				}
			}
			writer.print(functions);
		}
		finally {
			decompiler.dispose();
		}
		File sidecar = new File(base(output) + SIDECAR_SUFFIX);
		int recorded = writeConventions(sidecar);
		println("decompiled " + written + ", failed " + failed + " -> " + output);
		println("bridged for C: " + bridged);
		println("conventions recorded for " + recorded + " -> " + sidecar);
	}

	/**
	 * One line a function: {"address", "name", "note"?, then the recorded
	 * convention's members}.  Returns how many had a convention recorded.
	 */
	private int writeConventions(File sidecar) throws Exception {
		StringPropertyMap conventions =
			currentProgram.getUsrPropertyManager().getStringPropertyMap(CONVENTION_MAP);
		StringPropertyMap notes = currentProgram.getUsrPropertyManager().getStringPropertyMap(NOTE_MAP);
		int recorded = 0;
		try (PrintWriter writer = new PrintWriter(sidecar)) {
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				String convention = conventions == null ? null
						: conventions.getString(function.getEntryPoint());
				String note = notes == null ? null : notes.getString(function.getEntryPoint());
				if (convention == null) {
					convention = UNKNOWN;
				}
				else {
					recorded++;
				}
				StringBuilder line = new StringBuilder("{\"address\":")
					.append(jsonString(function.getEntryPoint().toString()))
					.append(",\"name\":").append(jsonString(function.getName()));
				if (note != null) {
					line.append(",\"note\":").append(jsonString(note));
				}
				line.append(",").append(convention, 1, convention.length());
				writer.println(line);
			}
		}
		return recorded;
	}

	/**
	 * The function's C with its signature rewritten if its recorded
	 * convention says it is not C, and the decompiler's warning dropped.
	 */
	private String notC(Function function, String c) {
		String macro = notCMacro(function);
		if (macro == null) {
			return c;
		}
		String convention = recorded(CONVENTION_MAP, function);
		Matcher in = IN_CONFIDENCE.matcher(convention);
		Matcher confidence = CONFIDENCE.matcher(convention);
		boolean trusted = TRUSTED.contains(in.find() ? in.group(1)
			: confidence.find() ? confidence.group(1) : "");
		Map<String, String> registers = new HashMap<>();
		if (trusted) {
			for (Parameter parameter : function.getParameters()) {
				List<Register> storage = parameter.getVariableStorage().getRegisters();
				if (storage != null && !storage.isEmpty()) {
					registers.put(parameter.getName(),
						storage.get(0).getBaseRegister().getName().toLowerCase());
				}
			}
		}
		String signature = recorded(SIGNATURE_MAP, function);
		StringBuilder out = new StringBuilder();
		boolean rewritten = false;
		for (String line : c.split("\n", -1)) {
			if (line.equals(UNKNOWN_CONVENTION)) {
				continue;
			}
			int open = line.indexOf(function.getName() + "(");
			if (!rewritten && open >= 0 && !line.startsWith("/*") && !line.startsWith(" ")) {
				int close = line.lastIndexOf(')');
				String arguments = line.substring(open + function.getName().length() + 1, close);
				List<String> annotated = new ArrayList<>();
				for (String argument : arguments.split(",")) {
					String name = argument.replaceFirst("^.*?(\\w+)\\s*$", "$1");
					String register = registers.get(name);
					annotated.add(register == null ? argument
						: argument + " /* " + register + " */");
				}
				line = (signature == null ? "" : "/* " + signature + " */\n") + macro + " "
					+ line.substring(0, open + function.getName().length() + 1)
					+ String.join(",", annotated) + line.substring(close);
				rewritten = true;
			}
			out.append(line).append('\n');
		}
		return out.substring(0, out.length() - 1);
	}

	/** Bytes in the decompiler's sized operations' operands, at most a long long. */
	private static final int MAX_BYTES = 8;

	/** The CONCAT, SUB, ZEXT and SEXT macros, one for each pair of sizes. */
	private static String sizedOperations() {
		StringBuilder macros = new StringBuilder();
		for (int x = 1; x <= MAX_BYTES; x++) {
			for (int y = 1; y <= MAX_BYTES; y++) {
				if (x + y <= MAX_BYTES) {
					macros.append(String.format(
						"#define CONCAT%d%d(a, b) ((%s)__ghidra_concat(%d, a, b))%n",
						x, y, unsignedOf(x + y), y));
				}
				if (y <= x) {
					macros.append(String.format(
						"#define SUB%d%d(v, n) ((%s)__ghidra_sub(%d, v, n))%n", x, y,
						unsignedOf(y), y));
				}
				if (y > x) {
					macros.append(String.format("#define ZEXT%d%d(v) ((%s)(%s)(v))%n", x, y,
						unsignedOf(y), unsignedOf(x)));
					macros.append(String.format("#define SEXT%d%d(v) ((%s)(%s)(v))%n", x, y,
						signedOf(y), signedOf(x)));
				}
			}
		}
		return macros.toString();
	}

	/** The unsigned C type that holds so many bytes. */
	private static String unsignedOf(int bytes) {
		return bytes == 1 ? "unsigned char" : bytes == 2 ? "unsigned short"
			: bytes <= 4 ? "unsigned int" : "unsigned long long";
	}

	/** The signed C type that holds so many bytes. */
	private static String signedOf(int bytes) {
		return bytes == 1 ? "signed char" : bytes == 2 ? "short" : bytes <= 4 ? "int" : "long long";
	}

	/** A routine's C prototype and the wrapper that calls it from gcc's convention. */
	private record Bridge(String prototype, String assembly) {}

	/** The prefix of a wrapper's symbol: c_ and the routine's name. */
	private static final String BRIDGE_PREFIX = "c_";
	/** The registers gcc's callee saves, which a routine of another convention may not. */
	private static final String SAVED = "%d2-%d7/%a2-%a6";
	/** Bytes the wrapper pushes saving SAVED, and the return address under them. */
	private static final int SAVED_BYTES = 11 * 4;
	private static final int RETURN_ADDRESS_BYTES = 4;
	/** gcc's stack slot for an argument narrower than it. */
	private static final int SLOT = 4;

	/**
	 * The C prototype and wrapper for a routine that takes its arguments
	 * and gives its result in registers, from the prototype Setup gave it
	 * from its convention: the prototype names the wrapper, c_NAME, by an
	 * assembler label, and the wrapper saves what gcc's callee must, loads
	 * each argument from gcc's stack slot into its register, calls the
	 * routine and leaves its result where gcc takes it.  Null unless every
	 * argument and the result are in registers.
	 */
	private Bridge bridge(Function function) {
		if (!function.hasCustomVariableStorage()) {
			return null;
		}
		String name = function.getName();
		List<String> parameters = new ArrayList<>();
		StringBuilder load = new StringBuilder();
		int offset = SAVED_BYTES + RETURN_ADDRESS_BYTES;
		for (Parameter parameter : function.getParameters()) {
			List<String> registers = registers(parameter.getVariableStorage());
			int size = parameter.getLength();
			if (registers == null) {
				return null;
			}
			parameters.add(declaration(parameter.getDataType(), parameter.getName()));
			if (registers.size() == 2 && size == 8) {
				load.append(line("move.l (" + offset + ",%sp),%" + registers.get(0)))
					.append(line("move.l (" + (offset + 4) + ",%sp),%" + registers.get(1)));
			}
			else if (registers.size() == 1 && size <= SLOT) {
				// gcc promotes a narrow argument to int in its slot, so the
				// register gets it extended, as the original's moveq does
				if (size < SLOT && registers.get(0).startsWith("a")) {
					return null;
				}
				load.append(line("move.l (" + offset + ",%sp),%" + registers.get(0)));
			}
			else {
				return null;
			}
			offset += Math.max(size, SLOT);
		}
		Parameter returned = function.getReturn();
		DataType resultType = returned.getDataType();
		String result = declaration(resultType, "").strip();
		StringBuilder give = new StringBuilder();
		if (resultType.getLength() > 0 && !"void".equals(resultType.getName())) {
			List<String> registers = registers(returned.getVariableStorage());
			int size = resultType.getLength();
			if (registers == null) {
				return null;
			}
			if (size == 8 && registers.size() == 2) {
				if (!registers.equals(List.of("d0", "d1"))) {
					give.append(line("move.l %" + registers.get(0) + ",-(%sp)"))
						.append(line("move.l %" + registers.get(1) + ",%d1"))
						.append(line("move.l (%sp)+,%d0"));
				}
			}
			else if (registers.size() == 1 && size <= SLOT) {
				if (!registers.get(0).equals("d0")) {
					give.append(line("move.l %" + registers.get(0) + ",%d0"));
				}
				DataType base = resultType instanceof TypeDef typedef
					? typedef.getBaseDataType() : resultType;
				boolean signed = base instanceof AbstractIntegerDataType integer
					&& integer.isSigned();
				give.append(size == 1 ? line(signed ? "extb.l %d0" : "and.l #0xff,%d0")
					: size == 2 ? line(signed ? "ext.l %d0" : "and.l #0xffff,%d0")
					: "");
				if (base instanceof Pointer) {
					give.append(line("move.l %d0,%a0"));
				}
			}
			else {
				return null;
			}
		}
		String wrapper = BRIDGE_PREFIX + name;
		String prototype = result + (result.endsWith("*") ? "" : " ") + name + "("
			+ (parameters.isEmpty() ? "void" : String.join(", ", parameters)) + ") __asm__(\""
			+ wrapper + "\");";
		StringBuilder assembly = new StringBuilder("/* ").append(name)
			.append(" from gcc's convention: its arguments from the stack into its registers */\n")
			.append("__asm__(\n")
			.append(quoted("\t.pushsection .text." + wrapper + ",\"ax\",@progbits"))
			.append(quoted("\t.globl " + wrapper)).append(quoted("\t.type " + wrapper + ", @function"))
			.append(quoted(wrapper + ":"))
			.append(quoted("\tmovem.l " + SAVED + ",-(%sp)"));
		for (String part : (load.toString() + line("jsr " + name) + give
				+ line("movem.l (%sp)+," + SAVED) + line("rts")).split("\n")) {
			assembly.append(quoted(part));
		}
		assembly.append(quoted("\t.size " + wrapper + ", .-" + wrapper))
			.append(quoted("\t.popsection")).append(");\n");
		return new Bridge(prototype, assembly.toString());
	}

	private static String line(String instruction) {
		return "\t" + instruction + "\n";
	}

	/**
	 * The registers storage is in, lower case and whole, a pair high
	 * register first; null if any part of it is not a register.
	 */
	private List<String> registers(VariableStorage storage) {
		List<String> registers = new ArrayList<>();
		for (Varnode varnode : storage.getVarnodes()) {
			Register register = currentProgram.getRegister(varnode);
			if (register == null) {
				return null;
			}
			registers.add(register.getBaseRegister().getName().toLowerCase());
		}
		return registers.isEmpty() ? null : registers;
	}

	/** The macro for the function's kind if its recorded convention is not C, else null. */
	private String notCMacro(Function function) {
		String convention = recorded(CONVENTION_MAP, function);
		Matcher kind = convention == null ? null : KIND.matcher(convention);
		return kind != null && kind.find() ? KIND_MACROS.get(kind.group(1)) : null;
	}

	/** The function's entry in the named property map, or null. */
	private String recorded(String name, Function function) {
		StringPropertyMap map = currentProgram.getUsrPropertyManager().getStringPropertyMap(name);
		return map == null ? null : map.getString(function.getEntryPoint());
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

	/** The program's structures, unions, enums and typedefs. */
	private void writeTypes(PrintWriter writer) throws Exception {
		DataTypeManager types = currentProgram.getDataTypeManager();
		List<DataType> defined = new ArrayList<>();
		for (Iterator<DataType> it = types.getAllDataTypes(); it.hasNext();) {
			DataType type = it.next();
			if (type instanceof Composite || type instanceof Enum || type instanceof TypeDef) {
				defined.add(type);
			}
		}
		writer.println();
		writer.println("#pragma pack(push, 1)");
		StringWriter declarations = new StringWriter();
		new DataTypeWriter(types, declarations).write(defined, monitor);
		writer.print(PACKED_ENUM.matcher(declarations.toString())
			.replaceAll("typedef enum __attribute__((packed)) $1 {"));
		writer.println("#pragma pack(pop)");
		writer.println();
		writer.println("/* each type's size in the image, which gcc's layout must give */");
		for (DataType type : defined) {
			if ((type instanceof Composite || type instanceof Enum) && type.getLength() > 0
				&& IDENTIFIER.matcher(type.getDisplayName()).matches()) {
				writer.println("_Static_assert(sizeof(" + type.getDisplayName() + ") == "
					+ type.getLength() + ", \"" + type.getDisplayName() + "\");");
			}
		}
	}

	/** Each global the function's C names, by the name it prints. */
	private static void collectGlobals(HighFunction high, Map<String, HighSymbol> referenced) {
		if (high == null) {
			return;
		}
		for (Iterator<HighSymbol> it = high.getGlobalSymbolMap().getSymbols(); it.hasNext();) {
			HighSymbol symbol = it.next();
			referenced.putIfAbsent(symbol.getName(), symbol);
		}
	}

	/**
	 * An extern for each global the functions name that writeGlobals did
	 * not declare, and for every global a .set placing its symbol at its
	 * address, so that the object's references are to the image's own.
	 */
	private void writeReferencedGlobals(PrintWriter writer, Map<String, HighSymbol> referenced,
			Map<String, String> declared, Map<String, DataType> used, String text) {
		writer.println();
		for (HighSymbol symbol : referenced.values()) {
			String name = symbol.getName();
			if (symbol.isGlobal() && IDENTIFIER.matcher(name).matches() && !declared.containsKey(name)
				&& currentProgram.getFunctionManager().getFunctionAt(address(symbol)) == null) {
				String declaration = text.contains("(*" + name + ")(") ? "code *" + name
					: name.startsWith("PTR_") ? "undefined4 *" + name
					: declaration(better(symbol.getDataType(), used.get(name)), name);
				writer.println(externFor(address(symbol)) + declaration + "; /* "
					+ address(symbol) + " */");
				declared.put(name, address(symbol).toString());
			}
		}
		writer.println();
		for (Map.Entry<String, String> global : declared.entrySet()) {
			writer.println("__asm__(\".set " + global.getKey() + ", 0x"
				+ global.getValue().replaceFirst("^.*:", "") + "\");");
		}
	}

	/**
	 * A global the decompiler names by its address but keeps in no symbol
	 * map: a sized access to another's cell (_DAT_, uRam), a string, a
	 * pointer or a label.  Its type comes from its name's prefix.
	 */
	private static final Pattern NAMED_BY_ADDRESS = Pattern.compile(
		"\\b([A-Za-z_]\\w*?)(?:installer__)?([0-9a-f]{8})\\b");
	/** The decompiler's name for a named global accessed at another size: _ and the name. */
	private static final Pattern ALIAS = Pattern.compile("\\b_([A-Za-z]\\w*)\\b");
	/** The decompiler's names for its views of the caller's frame and of registers. */
	private static final Pattern FRAME = Pattern.compile(
		"\\b(?:stack|register|unique)0x[0-9a-f]+\\b");
	/** The C type of a global Ghidra names by its address, by its prefix. */
	private static final Map<String, String> PREFIX_TYPES = Map.ofEntries(
		Map.entry("_DAT_", "undefined4"), Map.entry("DAT_", "undefined1"),
		Map.entry("UNK_", "undefined1"), Map.entry("LAB_", "undefined1"),
		Map.entry("WORD_", "word"), Map.entry("USHORT_", "ushort"),
		Map.entry("Ram", "undefined4"), Map.entry("ram0x", "undefined4"),
		Map.entry("bRam", "byte"), Map.entry("cRam", "char"), Map.entry("sRam", "short"),
		Map.entry("uRam", "uint"), Map.entry("iRam", "int"), Map.entry("lRam", "long"),
		Map.entry("fRam", "float"), Map.entry("dRam", "double"),
		Map.entry("pcRam", "char *"), Map.entry("puRam", "uint *"),
		Map.entry("pvRam", "void *"), Map.entry("plRam", "long *"),
		Map.entry("func_0x", "code"));

	/**
	 * An extern, and a .set to its address, for each global the functions
	 * name by its address that no symbol map gave; strings are char arrays
	 * and pointers void *.  The caller's frame and the registers have no
	 * address: those get the extern alone.
	 */
	private void writeNamedByAddress(PrintWriter writer, CharSequence functions,
			Map<String, String> declared, Map<String, DataType> used) {
		Map<String, String> found = new TreeMap<>();
		String text = functions.toString();
		Matcher named = NAMED_BY_ADDRESS.matcher(functions);
		while (named.find()) {
			String name = named.group(0);
			String prefix = named.group(1);
			String type = prefix.startsWith("s_") ? "char"
				: prefix.startsWith("PTR_") ? "undefined4 *" : PREFIX_TYPES.get(prefix);
			if (type != null && text.contains("(*" + name + ")(")) {
				type = "code *";
			}
			if (type != null && !declared.containsKey(name)
				&& getFunction(name) == null) {
				DataType seen = used.get(name);
				found.put(name, prefix.startsWith("s_") ? "char " + name + "[]"
					: "code *".equals(type) ? type + name
					: seen != null && !Undefined.isUndefined(seen) ? declaration(seen, name)
					: type + " " + name);
				declared.put(name, named.group(2));
			}
		}
		Matcher alias = ALIAS.matcher(functions);
		while (alias.find()) {
			String name = alias.group();
			String target = alias.group(1);
			if (!declared.containsKey(name) && declared.containsKey(target)) {
				found.put(name, "undefined4 " + name);
				declared.put(name, declared.get(target).replaceFirst("^.*:", ""));
			}
		}
		Matcher frame = FRAME.matcher(functions);
		Map<String, String> frames = new TreeMap<>();
		while (frame.find()) {
			frames.put(frame.group(), "undefined4 " + frame.group());
		}
		writer.println();
		for (Map.Entry<String, String> global : found.entrySet()) {
			writer.println(externFor(toAddr(Long.parseLong(declared.get(global.getKey()), 16)))
				+ global.getValue() + ";");
			writer.println("__asm__(\".set " + global.getKey() + ", 0x"
				+ declared.get(global.getKey()) + "\");");
		}
		writer.println("/* the caller's frame and the registers, which have no address */");
		for (String declaration : frames.values()) {
			writer.println("extern " + declaration + ";");
		}
	}

	/**
	 * The decompiler's piece of a variable, v._n_m_: the m bytes n bytes
	 * from its start, its first being its most significant.  The base is a
	 * name and any members and subscripts after it.
	 */
	private static final Pattern PIECE = Pattern.compile(
		"([A-Za-z_]\\w*(?:(?:\\.|->)[A-Za-z_]\\w*|\\[[^\\[\\]]*\\])*)\\._(\\d+)_(\\d+)_");

	/** c with each piece of a variable a cast at its offset, which gcc takes. */
	private static String pieces(String c) {
		Matcher piece = PIECE.matcher(c);
		StringBuilder out = new StringBuilder();
		while (piece.find()) {
			int bytes = Integer.parseInt(piece.group(3));
			String type = switch (bytes) {
				case 1 -> "unsigned char";
				case 2 -> "unsigned short";
				case 4 -> "unsigned int";
				case 8 -> "unsigned long long";
				default -> "__ghidra_u" + bytes;
			};
			String access = "__ghidra_piece(" + type + ", " + piece.group(1) + ", "
				+ piece.group(2) + ")" + (type.startsWith("__ghidra_u") ? ".v" : "");
			piece.appendReplacement(out, Matcher.quoteReplacement(access));
		}
		piece.appendTail(out);
		return out.toString();
	}

	/**
	 * The most bytes after a routine, before the next, that its assembly
	 * carries: its tables, which no function holds.  Larger gaps are data
	 * of their own.
	 */
	private static final long MAX_TRAILING = 0x100;
	/** A local label's prefix: gcc's own are .L and a number. */
	private static final String LOCAL_LABEL = ".Lg_";
	/** The prefix of a symbol made for an address outside every function and label. */
	private static final String ADDRESS_SYMBOL = "__ghidra_";
	/** The mnemonics whose bare address operand is a branch's target, not (a,%pc). */
	private static final Pattern BRANCH = Pattern.compile("^(b[a-z]{2,3}|db[a-z]{1,2})(\\.[bwls])?$");

	/**
	 * The function as gas's m68k syntax, inside a top-level __asm__: each
	 * instruction from Ghidra's operands, each datum in its body as bytes,
	 * a local label at each address in it that an operand names, and each
	 * address outside it as the function, global or made symbol there.
	 * Assembled, it is the image's own bytes but for the relocations.
	 */
	private String assembly(Function function, Map<String, String> declared,
			Map<Address, String> madeSymbols) {
		Address start = function.getBody().getMinAddress();
		Address end = function.getBody().getMaxAddress();
		FunctionIterator after = currentProgram.getFunctionManager().getFunctions(end.next(), true);
		Function next = after.hasNext() ? after.next() : null;
		if (next != null && next.getEntryPoint().getAddressSpace().equals(end.getAddressSpace())
			&& next.getEntryPoint().subtract(end) <= MAX_TRAILING) {
			end = next.getEntryPoint().previous();
		}
		AddressSet body = new AddressSet(start, end);
		for (Iterator<Function> it = currentProgram.getFunctionManager()
				.getFunctionsOverlapping(body); it.hasNext();) {
			Function other = it.next();
			if (!other.equals(function)) {
				body.delete(other.getBody());
			}
		}
		Set<Address> targets = new HashSet<>();
		List<String> lines = new ArrayList<>();
		for (CodeUnit unit : currentProgram.getListing().getCodeUnits(body, true)) {
			if (unit instanceof Instruction instruction && (function.getBody().contains(unit.getAddress())
				|| BRANCH.matcher(instruction.getMnemonicString().toLowerCase()).matches())) {
				lines.add(unit.getAddress() + "\t" + instructionText(instruction, body, targets,
					declared, madeSymbols));
			}
			else if (unit instanceof Instruction instruction) {
				lines.add(unit.getAddress() + "\t" + raw(instruction));
			}
			else {
				StringBuilder bytes = new StringBuilder("\t.byte ");
				try {
					byte[] data = unit.getBytes();
					for (int i = 0; i < data.length; i++) {
						bytes.append(i == 0 ? "" : ",").append(String.format("0x%02x", data[i] & 0xff));
					}
				}
				catch (Exception e) {
					throw new IllegalStateException(e);
				}
				lines.add(unit.getAddress() + bytes.toString());
			}
		}
		String name = function.getName();
		StringBuilder out = new StringBuilder("__asm__(\n");
		out.append(quoted("\t.globl " + name)).append(quoted("\t.type " + name + ", @function"));
		Set<Address> pieceStarts = new HashSet<>();
		for (AddressRange range : body) {
			pieceStarts.add(range.getMinAddress());
		}
		boolean inEntryPiece = false;
		boolean inPiece = false;
		for (String line : lines) {
			int tab = line.indexOf('\t');
			Address at = currentProgram.getAddressFactory().getAddress(line.substring(0, tab));
			if (pieceStarts.contains(at)) {
				if (inEntryPiece) {
					out.append(quoted("\t.size " + name + ", .-" + name));
					inEntryPiece = false;
				}
				if (inPiece) {
					out.append(quoted("\t.popsection"));
				}
				out.append(quoted("\t.pushsection " + pieceSection(name, at) + ",\"ax\",@progbits"));
				inPiece = true;
			}
			if (at.equals(function.getEntryPoint())) {
				out.append(quoted(name + ":"));
				inEntryPiece = true;
			}
			if (targets.contains(at)) {
				out.append(quoted(localLabel(at) + ":"));
			}
			for (String part : line.substring(tab).split("\n")) {
				out.append(quoted(part));
			}
		}
		if (inEntryPiece) {
			out.append(quoted("\t.size " + name + ", .-" + name));
		}
		if (inPiece) {
			out.append(quoted("\t.popsection"));
		}
		return out.append(");\n").toString();
	}

	/**
	 * The section of one contiguous piece of a routine, named for the
	 * routine and the piece's address: a routine whose code lies about
	 * another's is in pieces, each linked where the linker puts it.  Each
	 * is pushed and popped, so that gcc's own code stays in its section.
	 */
	static String pieceSection(String name, Address at) {
		return ".text." + name + "." + Long.toHexString(at.getOffset());
	}

	private static String quoted(String line) {
		return "\t\"" + line.replace("\\", "\\\\").replace("\"", "\\\"") + "\\n\"\n";
	}

	private static String localLabel(Address address) {
		return LOCAL_LABEL + Long.toHexString(address.getOffset());
	}

	/** One instruction in gas's syntax. */
	private String instructionText(Instruction instruction, AddressSetView body,
			Set<Address> targets, Map<String, String> declared, Map<Address, String> madeSymbols) {
		String mnemonic = instruction.getMnemonicString().toLowerCase().replaceFirst(":$", "");
		boolean branch = BRANCH.matcher(mnemonic).matches();
		List<String> operands = new ArrayList<>();
		for (int i = 0; i < instruction.getNumOperands(); i++) {
			operands.add(operandText(instruction.getDefaultOperandRepresentationList(i),
				branch, body, targets, declared, madeSymbols, instruction));
		}
		String implicit = statusRegister(instruction);
		if (implicit != null && operands.size() == 1) {
			operands.add(implicit);
		}
		String moved = movedStatusRegister(instruction);
		if (moved != null && operands.size() == 1) {
			operands.add(moved.startsWith("to ") ? 1 : 0, moved.substring(moved.indexOf(' ') + 1));
		}
		if (operands.stream().anyMatch(o -> o.contains(UNTRANSLATED))) {
			return raw(instruction);
		}
		if (branch && mnemonic.endsWith(".b") && operands.size() == 1
			&& !samePiece(body, instruction)) {
			return shortBranch(instruction, operands.get(0));
		}
		return mnemonic + (operands.isEmpty() ? "" : " " + String.join(",", operands));
	}

	/** A piece of an operand the translation does not take: a suppressed base register. */
	private static final String UNTRANSLATED = ",-,";

	/** Whether a branch's target is in its own contiguous piece of the routine. */
	private static boolean samePiece(AddressSetView body, Instruction branch) {
		Address[] flows = branch.getFlows();
		AddressRange piece = body.getRangeContaining(branch.getAddress());
		return flows.length > 0 && piece != null && piece.contains(flows[0]);
	}

	/**
	 * A short branch to another routine as its opcode byte and an 8-bit
	 * PC-relative relocation for its displacement, which the linker fills;
	 * gas will not always leave a .b branch's target to the linker.
	 */
	private static String shortBranch(Instruction instruction, String target) {
		try {
			return String.format(".byte 0x%02x | %s%n\t.reloc ., R_68K_PC8, %s-1%n\t.byte 0",
				instruction.getByte(0) & 0xff, instruction, target);
		}
		catch (Exception e) {
			throw new IllegalStateException(e);
		}
	}

	/** An instruction as its own bytes, Ghidra's text in a comment. */
	private static String raw(Instruction instruction) {
		StringBuilder out = new StringBuilder(".byte ");
		try {
			byte[] bytes = instruction.getBytes();
			for (int i = 0; i < bytes.length; i++) {
				out.append(i == 0 ? "" : ",").append(String.format("0x%02x", bytes[i] & 0xff));
			}
		}
		catch (Exception e) {
			throw new IllegalStateException(e);
		}
		return out + " | " + instruction;
	}

	/**
	 * An immediate as gas must have it to give the image's bytes: a byte
	 * operation's, the .b forms and those to CCR, as its unsigned byte,
	 * since gas writes a negative one's sign into its word's high byte.
	 */
	private static String immediate(Instruction instruction, Scalar scalar) {
		boolean byteSized = instruction.getMnemonicString().toLowerCase().endsWith(".b")
			|| "%ccr".equals(statusRegister(instruction));
		return byteSized && scalar.getSignedValue() < 0
			? String.format("0x%x", scalar.getSignedValue() & 0xff) : scalar.toString();
	}

	/**
	 * The status register a move to or from SR or CCR names, which Ghidra
	 * leaves out: "from %sr" (0x40c0), "from %ccr" (0x42c0), "to %ccr"
	 * (0x44c0) or "to %sr" (0x46c0), each with its effective address in the
	 * low six bits; null for any other instruction.
	 */
	private static String movedStatusRegister(Instruction instruction) {
		try {
			return switch (instruction.getUnsignedShort(0) & 0xffc0) {
				case 0x40c0 -> "from %sr";
				case 0x42c0 -> "from %ccr";
				case 0x44c0 -> "to %ccr";
				case 0x46c0 -> "to %sr";
				default -> null;
			};
		}
		catch (Exception e) {
			return null;
		}
	}

	/**
	 * The status register an immediate instruction to SR or CCR writes,
	 * which Ghidra leaves out of its operands: ori, andi or eori to
	 * CCR (0x003c, 0x023c, 0x0a3c) or SR (0x007c, 0x027c, 0x0a7c).
	 */
	private static String statusRegister(Instruction instruction) {
		try {
			int opcode = instruction.getUnsignedShort(0);
			return switch (opcode) {
				case 0x003c, 0x023c, 0x0a3c -> "%ccr";
				case 0x007c, 0x027c, 0x0a7c -> "%sr";
				default -> null;
			};
		}
		catch (Exception e) {
			return null;
		}
	}

	/** One operand from Ghidra's pieces of it. */
	private String operandText(List<Object> pieces, boolean branch, AddressSetView body,
			Set<Address> targets, Map<String, String> declared, Map<Address, String> madeSymbols,
			Instruction instruction) {
		if (pieces.size() == 1 && pieces.get(0) instanceof Scalar scalar) {
			return "#" + immediate(instruction, scalar);
		}
		if (!pieces.isEmpty() && "{".equals(pieces.get(0).toString())) {
			List<String> registers = new ArrayList<>();
			for (Object piece : pieces) {
				if (piece instanceof Register register) {
					registers.add(gasRegister(register.getName(), false));
				}
			}
			return String.join("/", registers);
		}
		StringBuilder out = new StringBuilder();
		boolean inParens = false;
		int registersInParens = 0;
		for (int i = 0; i < pieces.size(); i++) {
			Object piece = pieces.get(i);
			boolean pcRelative = inParens && i + 2 < pieces.size()
				&& pieces.get(i + 2) instanceof Register next && next.getName().equals("PC");
			if (piece instanceof Register register) {
				boolean index = inParens && (registersInParens > 0
					|| (i + 1 < pieces.size() && "*".equals(pieces.get(i + 1).toString())));
				out.append(gasRegister(register.getName(), index));
				if (inParens) {
					registersInParens++;
				}
			}
			else if (piece instanceof Scalar scalar && pcRelative) {
				String symbol = target(pcTarget(instruction, scalar), instruction.getAddress(), body, targets, declared,
					madeSymbols, true);
				boolean indexed = i + 4 < pieces.size() && pieces.get(i + 4) instanceof Register;
				out.append(indexed ? symbol.replaceFirst(":w$", ":b") : symbol);
			}
			else if (piece instanceof Scalar scalar && i > 0 && "#".equals(pieces.get(i - 1).toString())) {
				out.append(immediate(instruction, scalar));
			}
			else if (piece instanceof Scalar scalar) {
				out.append(scalar);
			}
			else if (piece instanceof Address address) {
				boolean sized = pieces.size() >= 2
					&& ".".equals(pieces.get(pieces.size() - 2).toString());
				String symbol = target(address, instruction.getAddress(), body, targets, declared,
					madeSymbols, !sized && !branch);
				out.append(sized && !inParens ? "(" + symbol + ")"
					: !sized && !branch && !inParens ? "(" + symbol + ",%pc)" : symbol);
			}
			else {
				String text = piece.toString();
				if (text.equals("(")) {
					inParens = true;
					registersInParens = 0;
				}
				else if (text.equals(")")) {
					inParens = false;
				}
				if (!text.isBlank()) {
					out.append(text.toLowerCase());
				}
			}
		}
		return out.toString();
	}

	/**
	 * Where a PC-relative operand points.  Ghidra shows an indexed one,
	 * (bd,PC,Xn), by its target, but (d16,PC) by its displacement, from
	 * the extension word: the instruction's address plus 2.
	 */
	private static Address pcTarget(Instruction instruction, Scalar scalar) {
		Address at = instruction.getAddress();
		for (int i = 0; i < instruction.getNumOperands(); i++) {
			List<Object> pieces = instruction.getDefaultOperandRepresentationList(i);
			if (pieces.contains(scalar) && pieces.size() == 5) {
				return at.add(2 + scalar.getSignedValue());
			}
		}
		return at.getNewAddress(scalar.getUnsignedValue());
	}

	/**
	 * An address an operand names: a local label inside the function,
	 * else the function, global or made symbol there; a PC-relative one
	 * outside it takes a word's displacement, :w.
	 */
	private String target(Address address, Address from, AddressSetView body, Set<Address> targets,
			Map<String, String> declared, Map<Address, String> madeSymbols, boolean pcRelative) {
		if (body.contains(address)) {
			targets.add(address);
			AddressRange piece = body.getRangeContaining(from);
			return localLabel(address)
				+ (pcRelative && piece != null && !piece.contains(address) ? ":w" : "");
		}
		Function function = currentProgram.getFunctionManager().getFunctionAt(address);
		Function containing = currentProgram.getFunctionManager().getFunctionContaining(address);
		String symbol;
		if (function != null && !function.isThunk()) {
			symbol = function.getName();
		}
		else if (function != null) {
			symbol = function.getThunkedFunction(true).getName();
		}
		else if (containing != null && !containing.isThunk()) {
			long offset = address.subtract(containing.getEntryPoint());
			symbol = containing.getName() + (offset < 0 ? "-0x" : "+0x")
				+ Long.toHexString(Math.abs(offset));
		}
		else {
			Symbol primary = currentProgram.getSymbolTable().getPrimarySymbol(address);
			symbol = !pcRelative && primary != null && declared.containsKey(primary.getName())
				? primary.getName()
				: madeSymbols.computeIfAbsent(address,
					a -> ADDRESS_SYMBOL + Long.toHexString(a.getOffset()));
		}
		return pcRelative ? symbol + ":w" : symbol;
	}

	/**
	 * gas's name for a register: Ghidra's in lower case after %, its b or
	 * w suffix dropped, but an index register's size kept, .w or .l.
	 */
	private static String gasRegister(String name, boolean index) {
		String lower = name.toLowerCase();
		String size = ".l";
		if (lower.matches("[da][0-7][bw]")) {
			size = lower.endsWith("w") ? ".w" : ".b";
			lower = lower.substring(0, 2);
		}
		return "%" + lower + (index ? size : "");
	}

	/** A name followed by a subscript. */
	private static final Pattern SUBSCRIPTED = Pattern.compile("\\b([A-Za-z_]\\w*)\\[");

	/**
	 * c with each function's name that the decompiler subscripts, a table
	 * it addresses from the function's own address, made a char pointer
	 * there.
	 */
	private String indexedFunctions(String c) {
		Matcher subscripted = SUBSCRIPTED.matcher(c);
		StringBuilder out = new StringBuilder();
		while (subscripted.find()) {
			String name = subscripted.group(1);
			boolean function = !getGlobalFunctions(name).isEmpty();
			subscripted.appendReplacement(out, Matcher.quoteReplacement(
				function ? "((char *)" + name + ")[" : subscripted.group()));
		}
		subscripted.appendTail(out);
		return out.toString();
	}

	/** A string's label with characters an identifier cannot hold, from its text. */
	private static final Pattern STRING_LABEL = Pattern.compile("\\bs_[\\w<>]*[<>][\\w<>]*");

	/** c with each string's label made an identifier: its < and > as _. */
	private static String identifiers(String c) {
		Matcher label = STRING_LABEL.matcher(c);
		StringBuilder out = new StringBuilder();
		while (label.find()) {
			label.appendReplacement(out, Matcher.quoteReplacement(label.group().replaceAll("[<>]", "_")));
		}
		label.appendTail(out);
		return out.toString();
	}

	/** Where a C comment ends. */
	private static final String COMMENT_END = "*/";

	/** The linker script beside the C that places the made symbols. */
	private static final String SYMBOLS_SUFFIX = ".symbols.ld";

	private static String base(File output) {
		return output.getPath().replaceFirst(Pattern.quote(C_SUFFIX) + "$", "");
	}

	/**
	 * The symbols the assembly makes for addresses in no routine, as a
	 * linker script: as needs them undefined to give a PC-relative
	 * operand the image's own short displacement, which an absolute .set
	 * would not.
	 */
	private static void writeSymbols(File file, Map<Address, String> madeSymbols)
			throws Exception {
		try (PrintWriter writer = new PrintWriter(file)) {
			writer.println("/* the addresses the decompiled C's assembly names, for the linker */");
			for (Map.Entry<Address, String> made : madeSymbols.entrySet()) {
				writer.println(made.getValue() + " = 0x"
					+ Long.toHexString(made.getKey().getOffset()) + ";");
			}
		}
	}

	/** What comes before a routine that is not C: the decompiler's reading, which gcc skips. */
	private static final String READING =
		"#if 0 /* the decompiler's reading; the routine is the assembly after it */\n";

	/**
	 * A function's prototype, for ahead of the bodies, since the
	 * decompiler writes them in address order: its signature, the text
	 * between its comment and its opening brace; null if it has none.
	 */
	private static String signature(String c) {
		int brace = c.indexOf("\n{\n");
		if (brace < 0) {
			return null;
		}
		String head = c.substring(0, brace);
		int comment = head.lastIndexOf(COMMENT_END + "\n");
		String signature = head.substring(comment < 0 ? 0 : comment + COMMENT_END.length()).strip();
		return signature.isEmpty() ? null : signature + ";";
	}

	/** The type each global has where the C uses it, the more telling kept. */
	private static void collectUsedTypes(ClangTokenGroup markup, Map<String, DataType> used) {
		if (markup == null) {
			return;
		}
		List<ClangNode> tokens = new ArrayList<>();
		markup.flatten(tokens);
		for (ClangNode node : tokens) {
			if (node instanceof ClangVariableToken token
				&& token.getHighVariable() instanceof HighGlobal global) {
				used.put(token.getText(), better(used.get(token.getText()), global.getDataType()));
			}
		}
	}

	/**
	 * Of two types for one global, the one gcc needs: a pointer over what
	 * is not one, a defined type over Ghidra's undefined ones.
	 */
	private static DataType better(DataType kept, DataType seen) {
		if (kept == null) {
			return seen;
		}
		if (seen == null) {
			return kept;
		}
		if (isPointer(seen) && !isPointer(kept)) {
			return seen;
		}
		return Undefined.isUndefined(kept) && !Undefined.isUndefined(seen) ? seen : kept;
	}

	private static boolean isPointer(DataType type) {
		return type instanceof Pointer
			|| (type instanceof TypeDef def && def.getBaseDataType() instanceof Pointer);
	}

	/**
	 * ADJ(p), the decompiler's pointer to the start of what a pointer with
	 * an offset points into, as a _Generic over each such pointer type.
	 */
	private void writeAdjustments(PrintWriter writer) {
		List<String> cases = new ArrayList<>();
		for (Iterator<DataType> it = currentProgram.getDataTypeManager().getAllDataTypes();
				it.hasNext();) {
			if (it.next() instanceof TypeDef def && def.getBaseDataType() instanceof Pointer pointer
				&& PointerTypedefInspector.getPointerComponentOffset(def) != 0) {
				cases.add(def.getDisplayName() + ": (" + pointer.getDataType().getDisplayName()
					+ " *)((char *)(p) - " + PointerTypedefInspector.getPointerComponentOffset(def)
					+ ")");
			}
		}
		if (!cases.isEmpty()) {
			writer.println();
			writer.println("#define ADJ(p) _Generic((p), " + String.join(", ", cases) + ")");
		}
	}

	private static final Pattern IDENTIFIER = Pattern.compile("[A-Za-z_]\\w*");

	/**
	 * An enum's declaration: gcc gives a packed enum the size Ghidra's
	 * does, the smallest that holds its values; #pragma pack does not.
	 */
	private static final Pattern PACKED_ENUM = Pattern.compile("typedef enum (\\w+) \\{");

	/**
	 * "extern ", and "volatile " for a global in a block Setup marks
	 * volatile, the peripherals', so that gcc keeps every access to it.
	 */
	private String externFor(Address address) {
		MemoryBlock block = currentProgram.getMemory().getBlock(address);
		return block != null && block.isVolatile() ? "extern volatile " : "extern ";
	}

	/** Where a global symbol lives. */
	private static Address address(HighSymbol symbol) {
		return symbol.getStorage().getMinAddress();
	}

	/**
	 * An extern for each global a script or the names file named and gave
	 * a type, with its address; not the switch tables, which only their
	 * own jmp uses.  Returns their names and addresses.
	 */
	private Map<String, String> writeGlobals(PrintWriter writer) {
		Map<String, String> declared = new TreeMap<>();
		writer.println();
		for (Symbol symbol : currentProgram.getSymbolTable().getAllSymbols(true)) {
			if (symbol.getSymbolType() != SymbolType.LABEL
				|| !symbol.getParentNamespace().isGlobal()
				|| symbol.getName().startsWith(SWITCH_TABLE_PREFIX)
				|| (symbol.getSource() != SourceType.IMPORTED
					&& symbol.getSource() != SourceType.USER_DEFINED)) {
				continue;
			}
			Data data = currentProgram.getListing().getDefinedDataAt(symbol.getAddress());
			if (data == null || Undefined.isUndefined(data.getDataType())) {
				continue;
			}
			writer.println(externFor(symbol.getAddress())
				+ declaration(data, data.getDataType(), symbol.getName())
				+ "; /* " + symbol.getAddress() + " */");
			declared.put(symbol.getName(), symbol.getAddress().toString());
		}
		return declared;
	}

	/** C's declarator for a type without its data: an array's bounds follow the name. */
	private static String declaration(DataType type, String name) {
		if (type instanceof Array array) {
			return declaration(array.getDataType(), name + "[" + array.getNumElements() + "]");
		}
		if (type instanceof Pointer pointer && pointer.getDataType() == null) {
			return "void *" + name;
		}
		if (type instanceof AbstractStringDataType) {
			return "char " + name + "[]";
		}
		String display = type.getDisplayName();
		return display + (display.endsWith("*") ? "" : " ") + name;
	}

	/**
	 * C's declarator syntax: an array's bounds follow the name.  Ghidra's
	 * untyped pointer has no C name.
	 */
	private static String declaration(Data data, DataType type, String name) {
		if (type instanceof Array array) {
			return declaration(data, array.getDataType(),
				name + "[" + array.getNumElements() + "]");
		}
		if (type instanceof AbstractStringDataType) {
			return "char " + name + "[" + data.getLength() + "]";
		}
		if (type instanceof Pointer pointer && pointer.getDataType() == null) {
			return "void *" + name;
		}
		return type.getDisplayName() + " " + name;
	}
}
