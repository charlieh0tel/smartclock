// Writes the program as C for m68k-linux-gnu-gcc -std=gnu17 -mcpu=cpu32,
// to the file named by the script's first argument: the
// program's structures, packed and each size asserted, and the #defines
// of each header a further argument defines=HEADER names; the
// decompiler's vocabulary; an extern and a .set to its address for every global the
// C names; a prototype for every function; then every function in
// address order.  Beside it, with .conventions.jsonl for .c, one JSON
// object a line for every function: its address and name, its note, the
// calling convention SmartClockSetup.java and SmartClockForth.java
// recorded, or "unknown", and where its stack arguments are; and with
// .symbols.ld, a linker script placing
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
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.decompiler.ClangCaseToken;
import ghidra.app.decompiler.ClangCommentToken;
import ghidra.app.decompiler.ClangFieldToken;
import ghidra.app.decompiler.ClangFuncNameToken;
import ghidra.app.decompiler.ClangLabelToken;
import ghidra.app.decompiler.ClangLine;
import ghidra.app.decompiler.ClangNode;
import ghidra.app.decompiler.ClangOpToken;
import ghidra.app.decompiler.ClangSyntaxToken;
import ghidra.app.decompiler.ClangToken;
import ghidra.app.decompiler.ClangTokenGroup;
import ghidra.app.decompiler.ClangTypeToken;
import ghidra.app.decompiler.ClangVariableDecl;
import ghidra.app.decompiler.ClangVariableToken;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.PrettyPrinter;
import ghidra.app.script.GhidraScript;
import ghidra.program.database.data.PointerTypedefInspector;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.data.AbstractFloatDataType;
import ghidra.program.model.data.AbstractIntegerDataType;
import ghidra.program.model.data.AbstractStringDataType;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.Composite;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.DataTypeWriter;
import ghidra.program.model.data.DefaultDataType;
import ghidra.program.model.data.Enum;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.data.Undefined;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighGlobal;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.LocalSymbolMap;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.IllegalCharCppTransformer;
import ghidra.program.model.symbol.NameTransformer;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.StackReference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.util.StringPropertyMap;
import ghidra.util.StringUtilities;

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
		#define __ghidra_as(type, v) ({ unsigned long long __a = __ghidra_bits(v); type __r; \
			__builtin_memcpy(&__r, (char *)&__a + sizeof __a - sizeof __r, sizeof __r); __r; })
		#define __ghidra_shl(x, n) ({ __typeof__((x) + 0) __x = (x); unsigned int __n = (n); \
			__n >= 8 * sizeof __x ? (__typeof__(__x))0 : __x << __n; })
		#define __ghidra_shr(x, n) ({ __typeof__((x) + 0) __x = (x); unsigned int __n = (n); \
			__n >= 8 * sizeof __x ? (__x < 0 ? (__typeof__(__x))-1 : (__typeof__(__x))0) : __x >> __n; })
		#define __ghidra_rotl(x, n) ({ __typeof__((x) + 0) __x = (x); unsigned int __n = (n) % (8 * sizeof __x); \
			__n == 0 ? __x : (__typeof__(__x))(__x << __n | __x >> (8 * sizeof __x - __n)); })
		#define __ghidra_mask(n) ((n) >= 8 ? ~0ULL : (1ULL << (8 * (n))) - 1)
		#define __ghidra_concat(y, a, b) \
			((__ghidra_bits(a) << (8 * (y))) | (__ghidra_bits(b) & __ghidra_mask(y)))
		#define __ghidra_sub(y, v, n) ((__ghidra_bits(v) >> (8 * (n))) & __ghidra_mask(y))
		#define __ghidra_also(var, v, n) (var = __ghidra_as(__typeof__(var), __ghidra_sub(sizeof var, v, n)))
		/* the condition codes a routine that returns them left, as its bridge keeps them */
		unsigned short __ghidra_ccr;
		/* the registers a routine also leaves results in, as its bridge keeps them */
		unsigned long __ghidra_result_d0, __ghidra_result_d1, __ghidra_result_d2,
			__ghidra_result_d3, __ghidra_result_a0, __ghidra_result_a1;
		#define __ghidra_flag(bit) ((__ghidra_ccr & (bit)) != 0)
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
			writeDefines(writer);
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
					Printed printed = printed(function, results);
					String c = indexedFunctions(identifiers(nonfiniteBits(stackInputs(function, entryRegisters(pieces(frameEnds(results.getHighFunction(), notC(function, printed.c())),
						printed.signedPieces()))))));
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
						functions.append(registerBits(rotates(registerShifts(reinterpretingCasts(c)))));
						collectGlobals(results.getHighFunction(), referenced);
						String stack = stackArguments(results.getHighFunction());
						if (stack != null) {
							stackLayouts.put(function.getEntryPoint(), stack);
						}
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
		println(reinterpretConversions(output));
		println("bridged for C: " + bridged);
		println("conventions recorded for " + recorded + " -> " + sidecar);
	}

	/**
	 * One line a function: {"address", "name", "note"?, then the recorded
	 * convention's members, then "stack"?}.  Returns how many had a convention recorded.
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
				line.append(",").append(convention, 1, convention.length() - 1);
				String stack = stackLayouts.get(function.getEntryPoint());
				if (stack != null) {
					line.append(",\"stack\":").append(stack);
				}
				writer.println(line.append("}"));
			}
		}
		return recorded;
	}

	/** Each C function's stack arguments, as stackArguments gives them. */
	private final Map<Address, String> stackLayouts = new HashMap<>();

	/**
	 * Where a decompiled function's arguments are, if each is on the
	 * stack: a JSON list of each one's offset from the stack pointer at
	 * entry, where the return address is, and its size, from the
	 * decompiler's prototype, which holds the arguments it found as well
	 * as those set.  Null if it has none or one is elsewhere.
	 */
	private static String stackArguments(HighFunction high) {
		LocalSymbolMap symbols = high.getLocalSymbolMap();
		if (symbols.getNumParams() == 0) {
			return null;
		}
		List<String> items = new ArrayList<>();
		for (int i = 0; i < symbols.getNumParams(); i++) {
			VariableStorage storage = symbols.getParamSymbol(i).getStorage();
			if (!storage.isStackStorage()) {
				return null;
			}
			items.add("{\"offset\":" + storage.getStackOffset() + ",\"size\":"
				+ storage.size() + "}");
		}
		return "[" + String.join(",", items) + "]";
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
	/** A stack argument's first offset from the stack pointer at entry, above the return address. */
	private static final int FIRST_ARGUMENT = RETURN_ADDRESS_BYTES;
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
		// the condition codes first, before the moves that give the result set them
		String keep = returnsConditionCodes(function) ? line("move.w %ccr," + CONDITION_CODES) : "";
		for (String register : results(function)) {
			keep += line("move.l %" + register.toLowerCase() + "," + RESULT_PREFIX + register.toLowerCase());
		}
		for (String part : (load.toString() + line("jsr " + name) + keep + give
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
	/** The argument naming a header whose #defines the C takes. */
	private static final String DEFINES = "defines=";
	/** A #define, with any lines it continues onto. */
	private static final Pattern DEFINE_LINE =
		Pattern.compile("(?m)^#define\\b(?:[^\\n]*\\\\\\n)*[^\\n]*$");

	/** A #define of a value, its name and value the groups. */
	private static final Pattern DEFINE_VALUE =
		Pattern.compile("(?m)^#define\\s+(\\w+)\\s+(0x[0-9a-fA-F]+|[0-9]+)\\b");
	/** The suffix of a structure laying out a module's registers. */
	private static final String REGISTERS_SUFFIX = "_registers";
	/** A register field's assignment, test or constant store: groups variable, field, and the rest. */
	private static final String MODULE_PATH = "\\b([A-Za-z_]\\w*(?:\\.[A-Za-z_]\\w*)*)\\.(\\w+)";
	private static final Pattern REGISTER_ASSIGNMENT = Pattern.compile(
		MODULE_PATH + " = ([^;]*?)([|&]) (0x[0-9a-f]+|[0-9]+);");
	private static final Pattern REGISTER_TEST = Pattern.compile(
		MODULE_PATH + " & (0x[0-9a-f]+|[0-9]+)\\)");
	private static final Pattern REGISTER_STORE = Pattern.compile(
		MODULE_PATH + " = (0x[0-9a-f]+|[0-9]+);");

	/** Each register's single bits, by its name upper-cased, from the defines= headers. */
	private Map<String, TreeMap<Long, String>> registerBitNames;
	/**
	 * Each register's named values but its masks and shifts, by its name,
	 * for a register whose codes are not all single bits -- a command
	 * register's -- which only an exact value may name.
	 */
	private Map<String, Map<Long, String>> registerCodes;
	/** Each module's register fields and their widths in bits, by its path: duart.a for a channel. */
	private Map<String, Map<String, Integer>> moduleFields;

	private void collectRegisterBits() throws Exception {
		registerBitNames = new HashMap<>();
		Map<String, Map<Long, String>> values = new HashMap<>();
		for (String arg : getScriptArgs()) {
			if (!arg.startsWith(DEFINES)) {
				continue;
			}
			Matcher define = DEFINE_VALUE.matcher(Files.readString(new File(
				arg.substring(DEFINES.length())).toPath()));
			while (define.find()) {
				String name = define.group(1);
				long value = Long.decode(define.group(2));
				int underscore = name.indexOf('_');
				if (underscore < 0 || name.endsWith("_MASK") || name.endsWith("_SHIFT") || value == 0) {
					continue;
				}
				String register = name.substring(0, underscore);
				values.computeIfAbsent(register, k -> new HashMap<>()).put(value, name);
				if (Long.bitCount(value) == 1) {
					registerBitNames.computeIfAbsent(register, k -> new TreeMap<>()).put(value, name);
				}
			}
		}
		registerCodes = new HashMap<>();
		for (Map.Entry<String, Map<Long, String>> register : values.entrySet()) {
			if (register.getValue().keySet().stream().anyMatch(v -> Long.bitCount(v) != 1)) {
				registerCodes.put(register.getKey(), register.getValue());
			}
		}
		moduleFields = new HashMap<>();
		for (Data data : currentProgram.getListing().getDefinedData(true)) {
			if (data.getLabel() != null && data.getDataType() instanceof Structure module
				&& module.getName().endsWith(REGISTERS_SUFFIX)) {
				addModuleFields(data.getLabel(), module);
			}
		}
	}

	/** Records a module's fields under its path, and its sub-structures' under theirs. */
	private void addModuleFields(String path, Structure module) {
		Map<String, Integer> fields = new HashMap<>();
		for (DataTypeComponent component : module.getDefinedComponents()) {
			if (component.getFieldName() == null) {
				continue;
			}
			fields.put(component.getFieldName(), 8 * component.getLength());
			if (component.getDataType() instanceof Structure part) {
				addModuleFields(path + "." + component.getFieldName(), part);
			}
		}
		moduleFields.put(path, fields);
	}

	/**
	 * A register's value as the OR of its bits' names, or, for a mask
	 * that clears more bits than it keeps, ~ of the OR of the bits it
	 * clears; for a register of codes, the one code it equals.  Null
	 * unless every bit has a name.  A field one register read and another
	 * written, sr_csr, is named by the one written for a store, the one
	 * read for a test.
	 */
	private String bitNames(String variable, String field, long value, boolean mask, boolean write) {
		Map<String, Integer> fields = moduleFields.get(variable);
		if (fields == null || !fields.containsKey(field)) {
			return null;
		}
		String[] parts = field.toUpperCase().split("_");
		String register = parts.length == 2 ? parts[write ? 1 : 0] : field.toUpperCase();
		if (registerCodes.containsKey(register)) {
			return mask ? null : registerCodes.get(register).get(value);
		}
		TreeMap<Long, String> bits = registerBitNames.get(register);
		if (bits == null) {
			return null;
		}
		int width = fields.get(field);
		long all = width >= 64 ? -1L : (1L << width) - 1;
		if (mask && Long.bitCount(value & all) > width / 2) {
			String cleared = orOfBits(bits, ~value & all);
			return cleared == null ? null : cleared.contains(" ") ? "~(" + cleared + ")" : "~" + cleared;
		}
		return orOfBits(bits, value);
	}

	private static String orOfBits(TreeMap<Long, String> bits, long value) {
		if (value == 0) {
			return null;
		}
		List<String> names = new ArrayList<>();
		for (Map.Entry<Long, String> bit : bits.descendingMap().entrySet()) {
			if ((value & bit.getKey()) != 0) {
				names.add(bit.getValue());
				value &= ~bit.getKey();
			}
		}
		return value != 0 ? null : names.size() == 1 ? names.get(0) : String.join(" | ", names);
	}

	/**
	 * c with the constants written to or tested in a module's register
	 * named by the register's bits, as the defines= headers give them:
	 * qsm.sccr1 = x | 0x80 becomes x | SCCR1_TIE.  Only a constant whose
	 * every bit has a name is rewritten.
	 */
	private String registerBits(String c) throws Exception {
		if (registerBitNames == null) {
			collectRegisterBits();
		}
		Matcher assignment = REGISTER_ASSIGNMENT.matcher(c);
		StringBuilder out = new StringBuilder();
		while (assignment.find()) {
			String names = bitNames(assignment.group(1), assignment.group(2),
				Long.decode(assignment.group(5)), assignment.group(4).equals("&"), true);
			assignment.appendReplacement(out, Matcher.quoteReplacement(names == null
				? assignment.group()
				: assignment.group(1) + "." + assignment.group(2) + " = " + assignment.group(3)
					+ assignment.group(4) + " " + (names.contains(" ") && !names.startsWith("~")
						? "(" + names + ")" : names) + ";"));
		}
		assignment.appendTail(out);
		Matcher test = REGISTER_TEST.matcher(out.toString());
		out = new StringBuilder();
		while (test.find()) {
			String names = bitNames(test.group(1), test.group(2), Long.decode(test.group(3)), false, false);
			test.appendReplacement(out, Matcher.quoteReplacement(names == null ? test.group()
				: test.group(1) + "." + test.group(2) + " & "
					+ (names.contains(" ") ? "(" + names + ")" : names) + ")"));
		}
		test.appendTail(out);
		Matcher store = REGISTER_STORE.matcher(out.toString());
		out = new StringBuilder();
		while (store.find()) {
			String names = bitNames(store.group(1), store.group(2), Long.decode(store.group(3)), false, true);
			store.appendReplacement(out, Matcher.quoteReplacement(names == null ? store.group()
				: store.group(1) + "." + store.group(2) + " = " + names + ";"));
		}
		store.appendTail(out);
		return out.toString();
	}

	/**
	 * Copies the #defines of each header a defines= argument names, which
	 * Ghidra's C parser cannot take: the bits of the registers the types
	 * lay out.
	 */
	private void writeDefines(PrintWriter writer) throws Exception {
		for (String arg : getScriptArgs()) {
			if (!arg.startsWith(DEFINES)) {
				continue;
			}
			String path = arg.substring(DEFINES.length());
			writer.println();
			writer.println("/* the #defines of " + new File(path).getName() + " */");
			Matcher define = DEFINE_LINE.matcher(Files.readString(new File(path).toPath()));
			while (define.find()) {
				writer.println(define.group());
			}
		}
	}

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
		writer.println("/*");
		writer.println(" * The decompiler's views of the stack at entry: stack0xN the bytes N");
		writer.println(" * above the return address, so N + 4 above gcc's frame pointer.  A view");
		writer.println(" * below the return address, into the routine's own frame, or of a");
		writer.println(" * register has no C to stand for it: it is at an address the emulator");
		writer.println(" * harness leaves unmapped, so that a use faults there, until its");
		writer.println(" * function is rewritten.");
		writer.println(" */");
		for (Map.Entry<String, String> view : frames.entrySet()) {
			String name = view.getKey();
			long offset = name.startsWith(STACK_VIEW) ? Long.parseLong(name.substring(STACK_VIEW.length()), 16) : -1;
			if (offset >= 0 && offset < Integer.MAX_VALUE) {
				writer.println("#define " + name + " (*(undefined4 *)((char *)__builtin_frame_address(0) + "
					+ (offset + RETURN_ADDRESS_BYTES) + "))");
			}
			else {
				writer.println("extern " + view.getValue() + ";");
				writer.println("__asm__(\".set " + name + ", 0x" + Long.toHexString(UNEXPRESSED) + "\");");
			}
		}
	}

	/** A local the decompiler reads a register's value at entry from: its type, register and width. */
	private static final Pattern UNAFFECTED = Pattern.compile(
		"^(  (?!return\\b)[A-Za-z_][\\w ]*?\\**) (unaff_([DA][0-5]|D[67])[bw]?);$", Pattern.MULTILINE);

	/**
	 * c with each unaff_ local, the value a register held at entry, given
	 * that value: the decompiler reads it where the routine uses a
	 * register its caller left, as a switch with no case for a value
	 * passes the caller's D2 on, and C left it unset.  A register
	 * variable reads it at the declaration, before the function's own
	 * code sets it.  A6, the frame pointer, and A7 are left unset.
	 */
	private static String entryRegisters(String c) {
		Matcher local = UNAFFECTED.matcher(c);
		StringBuilder out = new StringBuilder();
		while (local.find()) {
			String register = local.group(3).toLowerCase();
			local.appendReplacement(out, Matcher.quoteReplacement(local.group(1) + " " + local.group(2)
				+ " = ({ register long __entry __asm__(\"" + register + "\"); __asm__(\"\" : \"=r\"(__entry)); ("
				+ local.group(1).strip() + ")__entry; });"));
		}
		local.appendTail(out);
		return out.toString();
	}

	/** A local the decompiler reads from the caller's stack above the arguments: its type and offset. */
	private static final Pattern STACK_INPUT = Pattern.compile(
		"^(  (?!return\\b)[A-Za-z_][\\w ]*?\\**) (in_stack_([0-9a-f]{8}));$", Pattern.MULTILINE);

	/**
	 * c with each in_stack_N local, the long N bytes above the return
	 * address that the routine reads past its arguments, given that long
	 * from gcc's frame, where C left it unset: program_end_report prints
	 * its caller's return address from there.  Only where every argument
	 * is a long in its own slot, from the first, so that gcc's frame
	 * holds the same bytes above them as the original's.
	 */
	private static String stackInputs(Function function, String c) {
		int expected = FIRST_ARGUMENT;
		for (Parameter parameter : function.getParameters()) {
			if (!parameter.isStackVariable() || parameter.getStackOffset() != expected
				|| parameter.getLength() != SLOT) {
				return c;
			}
			expected += SLOT;
		}
		Matcher local = STACK_INPUT.matcher(c);
		StringBuilder out = new StringBuilder();
		while (local.find()) {
			String type = local.group(1).strip();
			local.appendReplacement(out, Matcher.quoteReplacement(local.group(1) + " " + local.group(2)
				+ " = *(" + type + " *)((char *)__builtin_frame_address(0) + "
				+ (Long.parseLong(local.group(3), 16) + RETURN_ADDRESS_BYTES) + ");"));
		}
		local.appendTail(out);
		return out.toString();
	}

	/** The address of a view of the routine's own frame, as the decompiler writes one. */
	private static final Pattern FRAME_VIEW_ADDRESS = Pattern.compile("&stack0x([0-9a-f]{8})\\b");

	/**
	 * c with the address of each view of the routine's own frame that ends
	 * a local the local's end, (void *)((char *)&local + sizeof local): a
	 * loop to the end of a record on the stack, which the decompiler
	 * writes as &stack0xfffffffc, the frame offset where it ends (nv_save's
	 * record).  A view that ends no local is left to UNEXPRESSED.
	 */
	private static String frameEnds(HighFunction high, String c) {
		Matcher view = FRAME_VIEW_ADDRESS.matcher(c);
		StringBuilder out = new StringBuilder();
		while (view.find()) {
			int offset = (int) Long.parseLong(view.group(1), 16);
			String end = view.group();
			for (Iterator<HighSymbol> symbols = high.getLocalSymbolMap().getSymbols();
					offset < 0 && symbols.hasNext();) {
				HighSymbol local = symbols.next();
				VariableStorage storage = local.getStorage();
				if (storage.isStackStorage() && storage.getStackOffset() + local.getSize() == offset) {
					end = "(void *)((char *)&" + local.getName() + " + sizeof " + local.getName() + ")";
				}
			}
			view.appendReplacement(out, Matcher.quoteReplacement(end));
		}
		view.appendTail(out);
		return out.toString();
	}

	/** The prefix of the decompiler's name for a view of the stack at entry. */
	private static final String STACK_VIEW = "stack0x";
	/** Where a view with no C stands: unmapped in the emulator harness. */
	private static final long UNEXPRESSED = 0xfffff000L;

	/**
	 * The decompiler's piece of a variable, v._n_m_: the m bytes n bytes
	 * from its start, its first being its most significant.  The base is a
	 * name and any members and subscripts after it.
	 */
	private static final Pattern PIECE = Pattern.compile(
		"([A-Za-z_]\\w*(?:(?:\\.|->)[A-Za-z_]\\w*|\\[[^\\[\\]]*\\])*)\\._(\\d+)_(\\d+)_");

	/** A signed integer cast, as the decompiler writes a sign extension, ending text. */
	private static final Pattern SIGNED_CAST =
		Pattern.compile("\\((?:char|sbyte|short|int|long|longlong|int8_t|int16_t|int32_t|int64_t)\\)$");
	/** How far before a piece its cast may begin. */
	private static final int CAST_REACH = 12;

	/**
	 * c with each piece of a variable a cast at its offset, which gcc
	 * takes.  A piece has no sign of its own: it is read signed where its
	 * p-code reads it so (signedPieces, each piece in c's code in order,
	 * if they number as many) or a signed cast before it, the decompiler's
	 * sign extension, says so, and unsigned anywhere else.
	 */
	private static String pieces(String c, List<Boolean> signedPieces) {
		Matcher piece = PIECE.matcher(c);
		boolean[] code = code(c);
		List<Integer> inCode = new ArrayList<>();
		while (piece.find()) {
			if (code[piece.start()]) {
				inCode.add(piece.start());
			}
		}
		boolean counted = signedPieces != null && signedPieces.size() == inCode.size();
		piece.reset();
		StringBuilder out = new StringBuilder();
		while (piece.find()) {
			int bytes = Integer.parseInt(piece.group(3));
			int index = inCode.indexOf(piece.start());
			boolean signed = counted && index >= 0 && signedPieces.get(index)
				|| SIGNED_CAST.matcher(
					c.substring(Math.max(0, piece.start() - CAST_REACH), piece.start())).find();
			String type = switch (bytes) {
				case 1 -> signed ? "signed char" : "unsigned char";
				case 2 -> signed ? "short" : "unsigned short";
				case 4 -> signed ? "int" : "unsigned int";
				case 8 -> signed ? "long long" : "unsigned long long";
				default -> "__ghidra_u" + bytes;
			};
			String access = "__ghidra_piece(" + type + ", " + piece.group(1) + ", "
				+ piece.group(2) + ")" + (type.startsWith("__ghidra_u") ? ".v" : "");
			piece.appendReplacement(out, Matcher.quoteReplacement(access));
		}
		piece.appendTail(out);
		return out.toString();
	}

	/** A function's C as printed from its tokens, and whether each piece in its code is read signed. */
	private record Printed(String c, List<Boolean> signedPieces) {}

	/** Text put before, in place of or after tokens of the decompiler's C, and its lines left out. */
	private static final class TokenEdits {
		private final Map<ClangToken, String> before = new IdentityHashMap<>();
		private final Map<ClangToken, String> replaced = new IdentityHashMap<>();
		private final Map<ClangToken, String> after = new IdentityHashMap<>();
		private final Set<ClangLine> dropped = Collections.newSetFromMap(new IdentityHashMap<>());
	}

	/** How DecompileResults cleans the names the decompiler prints. */
	private static final NameTransformer NAMES = new IllegalCharCppTransformer();
	/** The decompiler's name for a piece of a variable, the field of v._n_m_. */
	private static final Pattern PIECE_FIELD = Pattern.compile("_\\d+_\\d+_");
	/** The p-code operations that read their operands signed. */
	private static final Set<Integer> SIGNED_READS = Set.of(PcodeOp.INT_SLESS,
		PcodeOp.INT_SLESSEQUAL, PcodeOp.INT_SEXT, PcodeOp.INT_SRIGHT, PcodeOp.INT_SDIV,
		PcodeOp.INT_SREM);

	/**
	 * The function's C, printed from its tokens as the decompiler prints
	 * it, with what its p-code says and its text does not
	 * (unsignedCompares, resultCopies, switchLabels), and whether each
	 * piece of a variable in it is read signed.  The decompiler's own
	 * text, unedited, if its tokens do not print it.
	 */
	private Printed printed(Function function, DecompileResults results) throws Exception {
		String plain = results.getDecompiledFunction().getC();
		List<ClangLine> lines = new PrettyPrinter(function, results.getCCodeMarkup(), null).getLines();
		if (!text(lines, new TokenEdits()).equals(plain)) {
			println(function.getName() + ": its tokens do not print its C, which is left unedited");
			return new Printed(plain, null);
		}
		List<ClangToken> tokens = new ArrayList<>();
		for (ClangLine line : lines) {
			tokens.addAll(line.getAllTokens());
		}
		TokenEdits edits = new TokenEdits();
		unsignedCompares(tokens, edits);
		unsignedEqualities(tokens, edits);
		resultCopies(tokens, results.getHighFunction(), edits);
		conditionCodeResults(tokens, results.getHighFunction(), edits);
		registerResults(tokens, results.getHighFunction(), edits);
		preservedRegisters(tokens, results.getHighFunction(), edits);
		switchLabels(tokens, edits);
		byteConstants(tokens, edits);
		pushedValues(lines, tokens, edits);
		List<Boolean> signedPieces = new ArrayList<>();
		for (int i = 0; i < tokens.size(); i++) {
			ClangToken token = tokens.get(i);
			if (!(token instanceof ClangCommentToken) && PIECE_FIELD.matcher(token.getText()).matches()) {
				signedPieces.add(readSigned(fieldOperator(tokens, i)));
			}
		}
		return new Printed(text(lines, edits), signedPieces);
	}

	/** The lines' text, each token's as edits make it, as PrettyPrinter.print writes it. */
	private static String text(List<ClangLine> lines, TokenEdits edits) {
		StringBuilder out = new StringBuilder();
		for (ClangLine line : lines) {
			if (edits.dropped.contains(line)) {
				continue;
			}
			out.append(line.getIndentString());
			for (ClangToken token : line.getAllTokens()) {
				out.append(edits.before.getOrDefault(token, ""))
					.append(edits.replaced.getOrDefault(token, printedText(token)))
					.append(edits.after.getOrDefault(token, ""));
			}
			out.append(StringUtilities.LINE_SEPARATOR);
		}
		return out.toString();
	}

	/** A token's text as the decompiler prints it: a name cleaned, a constant not. */
	private static String printedText(ClangToken token) {
		boolean name = token instanceof ClangFuncNameToken || token instanceof ClangVariableToken
			|| token instanceof ClangTypeToken || token instanceof ClangFieldToken
			|| token instanceof ClangLabelToken;
		return name && token.getSyntaxType() != ClangToken.CONST_COLOR
			? NAMES.simplify(token.getText()) : token.getText();
	}

	/**
	 * The operation the "." before the field at tokens[at] is printed for:
	 * the field's own token has none, and the operator carries the one
	 * that reads the piece; null if there is no such operator.
	 */
	private static PcodeOp fieldOperator(List<ClangToken> tokens, int at) {
		for (int i = at - 1; i >= 0; i--) {
			ClangToken token = tokens.get(i);
			if (token instanceof ClangOpToken) {
				return token.getText().equals(".") ? token.getPcodeOp() : null;
			}
			if (!token.getText().isEmpty()) {
				return null;
			}
		}
		return null;
	}

	/**
	 * Whether the operation a piece's token is printed for reads it signed:
	 * a signed compare, extension, shift or division, itself or, for the
	 * SUBPIECE that makes the piece, any reader of it.
	 */
	private static boolean readSigned(PcodeOp op) {
		if (op == null) {
			return false;
		}
		if (op.getOpcode() != PcodeOp.SUBPIECE || op.getOutput() == null) {
			return SIGNED_READS.contains(op.getOpcode());
		}
		for (Iterator<PcodeOp> readers = op.getOutput().getDescendants(); readers.hasNext();) {
			if (SIGNED_READS.contains(readers.next().getOpcode())) {
				return true;
			}
		}
		return false;
	}

	/** The p-code's unsigned compares. */
	private static final Set<Integer> UNSIGNED_COMPARES = Set.of(PcodeOp.INT_LESS, PcodeOp.INT_LESSEQUAL);
	/** The operations whose result C types as its promoted operands, int for any narrower. */
	private static final Set<Integer> ARITHMETIC = Set.of(PcodeOp.INT_ADD, PcodeOp.INT_SUB,
		PcodeOp.INT_MULT, PcodeOp.INT_2COMP, PcodeOp.INT_NEGATE, PcodeOp.INT_LEFT);
	/** A constant as the decompiler prints an integer, without a suffix. */
	private static final Pattern UNSUFFIXED = Pattern.compile("0x[0-9a-fA-F]+|[0-9]+");
	/** The most bytes a U suffix makes unsigned; a wider constant takes ULL. */
	private static final int INT_BYTES = 4;

	/**
	 * Makes unsigned the constant of each unsigned compare whose other
	 * operand is arithmetic printed inline.  The decompiler types such an
	 * expression by the compare, but C types arithmetic on narrower values
	 * int, so that param_1 - 1 < 3, of a byte, compares signed and is true
	 * for 0, where the image's subq and bhi are not.  With 3U it compares
	 * unsigned, as the p-code does.
	 */
	private static void unsignedCompares(List<ClangToken> tokens, TokenEdits edits) {
		for (ClangToken token : tokens) {
			PcodeOp op = token.getPcodeOp();
			Varnode constant = token.getVarnode();
			if (!(token instanceof ClangVariableToken) || op == null || constant == null
				|| !UNSIGNED_COMPARES.contains(op.getOpcode()) || !constant.isConstant()
				|| !UNSUFFIXED.matcher(token.getText()).matches()) {
				continue;
			}
			Varnode other = op.getInput(op.getInput(0) == constant ? 1 : 0);
			boolean inline = other.getHigh() == null || other.getHigh().getSymbol() == null;
			if (!other.isConstant() && inline && other.getDef() != null
				&& ARITHMETIC.contains(other.getDef().getOpcode())) {
				edits.after.merge(token, constant.getSize() > INT_BYTES ? "ULL" : "U", String::concat);
			}
		}
	}

	/** A character constant as the decompiler prints a char: the character, or an escape. */
	private static final Pattern CHARACTER = Pattern.compile("'(\\\\(?:x[0-9a-fA-F]+|[0-7]{1,3}|[abfnrtv'\"?\\\\])|[^'\\\\])'");
	/** The escapes a character constant keeps: those text holds. */
	private static final Set<String> TEXT_ESCAPES = Set.of("\\n", "\\r", "\\t", "\\'", "\\\"", "\\\\", "\\?");
	/** The value of each escape that names a control character. */
	private static final Map<Character, Integer> NAMED_ESCAPES =
		Map.of('a', 7, 'b', 8, 'f', 12, 'v', 11);
	/** The first value the decompiler prints in hex. */
	private static final int HEX_FROM = 10;

	/**
	 * Prints as an integer each character constant that is no text: the
	 * decompiler prints every constant of a char type as a character, so
	 * that a byte's 0 and 1 read '\0' and '\x01'.  A printable character
	 * and a text escape stay; one past 0x7f, a negative char to gcc, keeps
	 * its value.
	 */
	private static void byteConstants(List<ClangToken> tokens, TokenEdits edits) {
		for (ClangToken token : tokens) {
			Matcher character = CHARACTER.matcher(token.getText());
			if (token instanceof ClangCommentToken || !character.matches()
				|| TEXT_ESCAPES.contains(character.group(1))) {
				continue;
			}
			String body = character.group(1);
			int value;
			if (!body.startsWith("\\")) {
				value = body.charAt(0);
				if (value >= ' ' && value < 0x7f) {
					continue;
				}
			}
			else if (body.charAt(1) == 'x') {
				value = Integer.parseInt(body.substring(2), 16);
			}
			else if (Character.isDigit(body.charAt(1))) {
				value = Integer.parseInt(body.substring(1), 8);
			}
			else {
				value = NAMED_ESCAPES.get(body.charAt(1));
			}
			int signed = (byte) value;
			edits.replaced.put(token, Math.abs(signed) < HEX_FROM ? Integer.toString(signed)
				: (signed < 0 ? "-" : "") + String.format("0x%x", Math.abs(signed)));
		}
	}

	/** The name of a variable pushedValues gives a value pushed and popped, before its number. */
	private static final String PUSHED = "pushed_";

	/**
	 * Gives each value the code pushes on the stack and pops again a
	 * variable of its own.  Where the stack slot is also written at other
	 * widths, the decompiler takes it a byte or word at a time: the C
	 * stores each piece of the value in a variable and reads the value
	 * back as their CONCAT, (float)CONCAT31(CONCAT21(CONCAT11(uVar12,
	 * uVar13),uVar14),uVar15) for a float holdover pushed while it
	 * converted another.  Where the p-code shows each piece is the
	 * SUBPIECE at its offset of the one value, kept across the calls in
	 * between (INDIRECT), the CONCAT is that value: the first piece's
	 * statement becomes pushed_n = value, the others go, and the CONCAT
	 * and a cast to the value's type become pushed_n.  A piece read
	 * anywhere else leaves the value as it is.
	 */
	private static void pushedValues(List<ClangLine> lines, List<ClangToken> tokens, TokenEdits edits) {
		Map<PcodeOp, ClangLine> statementLines = new IdentityHashMap<>();
		Map<ClangToken, ClangLine> lineOf = new IdentityHashMap<>();
		ClangToken open = null;
		for (ClangLine line : lines) {
			for (ClangToken token : line.getAllTokens()) {
				lineOf.put(token, line);
				if (open == null && token instanceof ClangSyntaxToken && token.getText().equals("{")) {
					open = token;
				}
				if (token instanceof ClangVariableToken && token.getPcodeOp() != null
					&& token.getPcodeOp().getOpcode() == PcodeOp.SUBPIECE
					&& token.getVarnode() == token.getPcodeOp().getOutput()) {
					statementLines.putIfAbsent(token.getPcodeOp(), line);
				}
			}
		}
		Map<Integer, Pushed> roots = new LinkedHashMap<>();
		for (int i = 0; i < tokens.size(); i++) {
			PcodeOp op = tokens.get(i).getPcodeOp();
			if (tokens.get(i) instanceof ClangOpToken && op != null && op.getOpcode() == PcodeOp.PIECE
				&& tokens.get(i).getText().startsWith("CONCAT") && !readByPiece(op.getOutput())) {
				Pushed pushed = pushed(op.getOutput());
				if (pushed != null) {
					roots.put(i, pushed);
				}
			}
		}
		// a CONCAT whose pieces are read elsewhere stays, and its reads are
		// then reads elsewhere of the pieces it shares with the others
		for (boolean changed = true; changed; ) {
			Set<PcodeOp> handled = Collections.newSetFromMap(new IdentityHashMap<>());
			for (Pushed pushed : roots.values()) {
				handled.addAll(pushed.ops());
			}
			changed = roots.values().removeIf(pushed -> !pushed.pieces().stream()
				.allMatch(p -> statementLines.containsKey(p) && singleStatement(statementLines.get(p))
					&& onlyReadBy(p.getOutput(), handled, Collections.newSetFromMap(new IdentityHashMap<>()))));
		}
		Map<Varnode, String> names = new IdentityHashMap<>();
		Set<HighVariable> pieceVariables = Collections.newSetFromMap(new IdentityHashMap<>());
		Set<ClangLine> rewritten = Collections.newSetFromMap(new IdentityHashMap<>());
		Set<PcodeOp> placed = Collections.newSetFromMap(new IdentityHashMap<>());
		for (Map.Entry<Integer, Pushed> root : roots.entrySet()) {
			Pushed pushed = root.getValue();
			String name = names.get(pushed.value());
			if (name == null) {
				name = PUSHED + (names.size() + 1);
				names.put(pushed.value(), name);
				edits.after.merge(open, StringUtilities.LINE_SEPARATOR + "  "
					+ pushed.value().getHigh().getDataType().getDisplayName() + " " + name + ";",
					String::concat);
			}
			for (PcodeOp piece : pushed.pieces()) {
				if (!placed.add(piece)) {
					continue;
				}
				ClangLine line = statementLines.get(piece);
				if (piece == pushed.pieces().get(0)) {
					rewritten.add(line);
					List<ClangToken> statement = line.getAllTokens();
					for (int k = 0; k < statement.size(); k++) {
						edits.replaced.put(statement.get(k),
							k == 0 ? name + " = " + namedHigh(pushed.value()) + ";" : "");
					}
				}
				else {
					edits.dropped.add(line);
				}
			}
			int start = root.getKey();
			int close = closingParenthesis(tokens, start);
			StringBuilder text = new StringBuilder(name);
			for (int k = start; k <= close; k++) {
				take(tokens.get(k), edits);
			}
			// what follows the CONCAT on its last line joins its first
			int first = lines.indexOf(lineOf.get(tokens.get(start)));
			int last = lines.indexOf(lineOf.get(tokens.get(close)));
			if (last > first) {
				List<ClangToken> rest = lines.get(last).getAllTokens();
				for (ClangToken token : rest.subList(rest.indexOf(tokens.get(close)) + 1, rest.size())) {
					text.append(take(token, edits));
				}
				for (int k = first + 1; k <= last; k++) {
					edits.dropped.add(lines.get(k));
				}
			}
			edits.replaced.put(tokens.get(start), text.toString());
			String type = pushed.value().getHigh().getDataType().getDisplayName();
			List<Integer> cast = new ArrayList<>();
			for (int k = start - 1; k >= 0 && cast.size() < 3; k--) {
				if (!tokens.get(k).getText().isBlank()) {
					cast.add(k);
				}
			}
			if (cast.size() == 3 && tokens.get(cast.get(0)).getText().equals(")")
				&& tokens.get(cast.get(1)).getText().equals(type) && tokens.get(cast.get(2)).getText().equals("(")) {
				for (int k = cast.get(2); k < start; k++) {
					take(tokens.get(k), edits);
				}
			}
			for (PcodeOp piece : pushed.pieces()) {
				pieceVariables.add(piece.getOutput().getHigh());
			}
		}
		// a piece's variable nothing else reads is no longer declared
		for (HighVariable variable : pieceVariables) {
			ClangLine declaration = null;
			boolean read = false;
			for (ClangToken token : tokens) {
				if (!(token instanceof ClangVariableToken named) || named.getHighVariable() != variable) {
					continue;
				}
				if (token.Parent() instanceof ClangVariableDecl) {
					declaration = lineOf.get(token);
				}
				else if (!edits.dropped.contains(lineOf.get(token)) && !rewritten.contains(lineOf.get(token))
					&& !"".equals(edits.replaced.get(token))) {
					read = true;
				}
			}
			if (declaration != null && !read) {
				edits.dropped.add(declaration);
			}
		}
	}

	/** A token's text as edits make it, which it then gives up: it prints nothing. */
	private static String take(ClangToken token, TokenEdits edits) {
		String text = edits.before.getOrDefault(token, "") + edits.replaced.getOrDefault(token, printedText(token))
			+ edits.after.getOrDefault(token, "");
		edits.before.remove(token);
		edits.after.remove(token);
		edits.replaced.put(token, "");
		return text;
	}

	/**
	 * A value read back from its pieces: the value, the SUBPIECE that
	 * makes each piece, most significant first, and every operation from
	 * them to the CONCAT.
	 */
	private record Pushed(Varnode value, List<PcodeOp> pieces, Set<PcodeOp> ops) {}

	/**
	 * The value whose pieces a CONCAT's output is, whole and in order,
	 * each kept through copies and calls; null if it is not one.
	 */
	private static Pushed pushed(Varnode concat) {
		List<Varnode> leaves = new ArrayList<>();
		Set<PcodeOp> ops = Collections.newSetFromMap(new IdentityHashMap<>());
		concatLeaves(concat, leaves, ops);
		Varnode value = null;
		List<PcodeOp> pieces = new ArrayList<>();
		int offset = concat.getSize();
		for (Varnode leaf : leaves) {
			offset -= leaf.getSize();
			PcodeOp def = leaf.getDef();
			while (def != null && (def.getOpcode() == PcodeOp.INDIRECT || def.getOpcode() == PcodeOp.COPY)) {
				ops.add(def);
				def = def.getInput(0).getDef();
			}
			if (def == null || def.getOpcode() != PcodeOp.SUBPIECE) {
				return null;
			}
			Varnode whole = def.getInput(0);
			long at = def.getInput(1).getOffset();
			// the decompiler's (char)((uint)value >> 0x18): a shift by whole
			// bytes of the value's bits, within them
			for (PcodeOp shift = whole.getDef(); shift != null; shift = whole.getDef()) {
				int opcode = shift.getOpcode();
				if ((opcode == PcodeOp.INT_RIGHT || opcode == PcodeOp.INT_SRIGHT) && shift.getInput(1).isConstant()
					&& shift.getInput(1).getOffset() % Byte.SIZE == 0 && namedHigh(whole) == null) {
					at += shift.getInput(1).getOffset() / Byte.SIZE;
				}
				else if (opcode != PcodeOp.CAST || namedHigh(whole) != null) {
					break;
				}
				ops.add(shift);
				whole = shift.getInput(0);
			}
			if (at != offset || at + leaf.getSize() > whole.getSize() || value != null && whole != value) {
				return null;
			}
			value = whole;
			pieces.add(def);
			ops.add(def);
		}
		return value != null && leaves.size() > 1 && value.getSize() == concat.getSize()
			&& namedHigh(value) != null && value.getHigh().getDataType() != null ? new Pushed(value, pieces, ops)
				: null;
	}

	/** The pieces a CONCAT joins, most significant first, through the CONCATs printed inside it. */
	private static void concatLeaves(Varnode value, List<Varnode> leaves, Set<PcodeOp> ops) {
		PcodeOp def = value.getDef();
		if (def != null && def.getOpcode() == PcodeOp.PIECE && namedHigh(value) == null) {
			ops.add(def);
			concatLeaves(def.getInput(0), leaves, ops);
			concatLeaves(def.getInput(1), leaves, ops);
		}
		else {
			leaves.add(value);
		}
	}

	/** Whether a CONCAT's output is printed inside another. */
	private static boolean readByPiece(Varnode output) {
		for (Iterator<PcodeOp> readers = output.getDescendants(); readers.hasNext();) {
			if (readers.next().getOpcode() == PcodeOp.PIECE && namedHigh(output) == null) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Whether every reader of value is among ops, through the copies,
	 * calls and joins it passes on by, which may themselves be read by
	 * nothing else.
	 */
	private static boolean onlyReadBy(Varnode value, Set<PcodeOp> ops, Set<PcodeOp> seen) {
		for (Iterator<PcodeOp> readers = value.getDescendants(); readers.hasNext();) {
			PcodeOp reader = readers.next();
			int opcode = reader.getOpcode();
			boolean passes = opcode == PcodeOp.INDIRECT || opcode == PcodeOp.COPY || opcode == PcodeOp.MULTIEQUAL;
			if (!passes && !ops.contains(reader)
				|| passes && seen.add(reader) && !onlyReadBy(reader.getOutput(), ops, seen)) {
				return false;
			}
		}
		return true;
	}

	/** Whether a line holds one statement and nothing else. */
	private static boolean singleStatement(ClangLine line) {
		long ends = line.getAllTokens().stream().filter(t -> t.getText().equals(";")).count();
		return ends == 1 && line.getAllTokens().get(line.getAllTokens().size() - 1).getText().equals(";");
	}

	/** The p-code's equality tests. */
	private static final Set<Integer> EQUALITIES = Set.of(PcodeOp.INT_EQUAL, PcodeOp.INT_NOTEQUAL);
	/** A negative constant as the decompiler prints one. */
	private static final Pattern NEGATIVE = Pattern.compile("-(0x[0-9a-fA-F]+|[0-9]+)");

	/**
	 * Gives a negative constant an equality test compares with a narrower
	 * unsigned operand that operand's value: the decompiler prints the
	 * byte 0xff as -1, but C promotes the operand to int, so that
	 * nv_records[i] != -1 is true for an erased 0xff, which the image's
	 * cmpi.b #-1 (nv_slot_erase) finds equal.
	 */
	private static void unsignedEqualities(List<ClangToken> tokens, TokenEdits edits) {
		for (ClangToken token : tokens) {
			PcodeOp op = token.getPcodeOp();
			Varnode constant = token.getVarnode();
			Matcher negative = NEGATIVE.matcher(token.getText());
			if (!(token instanceof ClangVariableToken) || op == null || constant == null
				|| !EQUALITIES.contains(op.getOpcode()) || !constant.isConstant()
				|| constant.getSize() >= INT_BYTES || !negative.matches()) {
				continue;
			}
			Varnode other = op.getInput(op.getInput(0) == constant ? 1 : 0);
			if (other.getHigh() != null && isUnsigned(other.getHigh().getDataType())) {
				long mask = (1L << (8 * constant.getSize())) - 1;
				edits.replaced.put(token, String.format("0x%x", constant.getOffset() & mask));
			}
		}
	}

	/** Whether C reads a value of the type unsigned: an unsigned integer or the decompiler's undefined. */
	private static boolean isUnsigned(DataType type) {
		DataType base = type instanceof TypeDef typedef ? typedef.getBaseDataType() : type;
		return base instanceof Undefined || base instanceof DefaultDataType
			|| base instanceof AbstractIntegerDataType integer && !integer.isSigned();
	}

	/** The registers a routine's convention says its result is also left in, and their confidence. */
	private static final Pattern COPIES = Pattern.compile("\"copies\":\"([DA][0-7](?::[DA][0-7])*)\"");
	private static final Pattern COPIES_CONFIDENCE = Pattern.compile("\"copies\\.confidence\":\"(\\w+)\"");
	/** Bytes in a data or address register. */
	private static final int REGISTER_BYTES = 4;
	/** The decompiler's name for a register's value after a call that sets it outside its prototype. */
	private static final String EXTRA_OUTPUT = "extraout_";
	/** The macro that gives a variable part of a value: __ghidra_also(var, v, n). */
	private static final String ALSO = "__ghidra_also";
	/**
	 * What a call whose result is also left elsewhere is written in: the
	 * call keeps its result in __c, written out rather than a macro's
	 * argument, since gcc places a diagnostic inside a macro's argument at
	 * the macro and reinterpretConversions would wrap the whole call.
	 */
	private static final String ALSO_OPEN = "({ __auto_type __c = ";

	/**
	 * Gives the variables the C reads a call's extra outputs from, its
	 * extraout_A0 and extraout_A1, the copies of the result its routine
	 * leaves in those registers, as the convention's copies names them,
	 * high first: the call becomes ({ __auto_type __c = call;
	 * __ghidra_also(variable, __c, n); __c; }), the call's result, which
	 * assigns the variable the result's bytes n up from its least
	 * significant end.  The C leaves them uninitialized
	 * otherwise, where the image's next double routine takes them as its
	 * other operand.
	 */
	private void resultCopies(List<ClangToken> tokens, HighFunction high, TokenEdits edits) {
		for (int i = 0; i < tokens.size(); i++) {
			PcodeOp call = tokens.get(i).getPcodeOp();
			if (!(tokens.get(i) instanceof ClangFuncNameToken) || call == null
				|| call.getOpcode() != PcodeOp.CALL) {
				continue;
			}
			List<String> copies = copies(getFunctionAt(call.getInput(0).getAddress()));
			Map<String, ExtraOutput> outputs = copies.isEmpty() ? Map.of() : extraOutputs(high, call);
			int close = outputs.isEmpty() ? -1 : closingParenthesis(tokens, i);
			if (close < 0) {
				continue;
			}
			for (int k = 0; k < copies.size(); k++) {
				ExtraOutput output = outputs.get(copies.get(k));
				if (output != null) {
					edits.before.merge(tokens.get(i), ALSO_OPEN, String::concat);
					edits.after.merge(tokens.get(close), "; " + ALSO + "(" + output.name() + ", __c, "
						+ ((copies.size() - 1 - k) * REGISTER_BYTES + output.low()) + "); __c; })",
						String::concat);
				}
			}
		}
	}

	/** Where a bridge keeps the condition codes its routine returns, a C global. */
	private static final String CONDITION_CODES = "__ghidra_ccr";
	private static final Pattern RETURNS_CCR = Pattern.compile("\"returns\":\"ccr\"");
	private static final Pattern PRESERVES = Pattern.compile("\"preserves\":\\[([^\\]]*)\\]");
	/** What a convention's preserves holds for every register. */
	private static final String ALL_PRESERVED = "all";
	private static final Pattern RESULTS = Pattern.compile("\"results\":\\[([^\\]]*)\\]");
	/** Where a bridge keeps a register its routine also leaves a result in, by its name. */
	private static final String RESULT_PREFIX = "__ghidra_result_";
	/** Each condition-code flag's register, by its bit in the CCR. */
	private static final Map<String, Integer> FLAG_BITS =
		Map.of("CF", 0x1, "VF", 0x2, "ZF", 0x4, "NF", 0x8, "XF", 0x10);

	/** Whether the function's trusted convention says it returns its result in the condition codes. */
	private boolean returnsConditionCodes(Function function) {
		String convention = function == null ? null : recorded(CONVENTION_MAP, function);
		Matcher confidence = convention == null ? null : CONFIDENCE.matcher(convention);
		return convention != null && RETURNS_CCR.matcher(convention).find() && confidence.find()
			&& TRUSTED.contains(confidence.group(1));
	}

	/**
	 * Gives each flag the C reads after a call to a routine that returns
	 * its result in the condition codes the routine's: the decompiler takes
	 * the call to keep the flags it had before, an INDIRECT at the call, so
	 * the C tests a compare made before it.  After the call each such
	 * variable is assigned its flag from __ghidra_ccr, where the routine's
	 * bridge keeps the condition codes.
	 */
	private void conditionCodeResults(List<ClangToken> tokens, HighFunction high, TokenEdits edits) {
		for (int i = 0; i < tokens.size(); i++) {
			PcodeOp call = tokens.get(i).getPcodeOp();
			if (!(tokens.get(i) instanceof ClangFuncNameToken) || call == null
				|| call.getOpcode() != PcodeOp.CALL
				|| !returnsConditionCodes(getFunctionAt(call.getInput(0).getAddress()))) {
				continue;
			}
			List<String> assignments = new ArrayList<>();
			for (PcodeOp op : indirects(high, call)) {
				Register register = currentProgram.getRegister(op.getOutput());
				Integer bit = register == null ? null : FLAG_BITS.get(register.getName());
				String name = namedHigh(op.getOutput());
				if (bit != null && name != null) {
					assignments.add(name + " = __ghidra_flag(" + bit + ")");
				}
			}
			assignAfterCall(tokens, edits, i, call, assignments);
		}
	}

	/**
	 * Gives each register the C reads after a call to a routine whose
	 * convention lists it among its results the routine's: the decompiler
	 * takes it as a register the call happens to change, extraout_, which
	 * nothing sets (psos_ident's id in A0, psos_get_v's value in D1).
	 * After the call each is assigned from __ghidra_result_REGISTER, where
	 * the routine's bridge keeps it, a narrower one its low bytes.
	 */
	private void registerResults(List<ClangToken> tokens, HighFunction high, TokenEdits edits) {
		for (int i = 0; i < tokens.size(); i++) {
			PcodeOp call = tokens.get(i).getPcodeOp();
			if (!(tokens.get(i) instanceof ClangFuncNameToken) || call == null
				|| call.getOpcode() != PcodeOp.CALL) {
				continue;
			}
			List<String> results = results(getFunctionAt(call.getInput(0).getAddress()));
			List<String> assignments = new ArrayList<>();
			for (PcodeOp op : indirects(high, call)) {
				Register register = currentProgram.getRegister(op.getOutput());
				String base = register == null ? null : register.getBaseRegister().getName();
				String name = namedHigh(op.getOutput());
				if (base != null && results.contains(base) && name != null && name.startsWith(EXTRA_OUTPUT)) {
					assignments.add(name + " = (__typeof__(" + name + "))" + RESULT_PREFIX + base.toLowerCase());
				}
			}
			// an argument in a result's register the decompiler takes the
			// call to keep, read again after it (psos_io_read's length in D2)
			Function callee = getFunctionAt(call.getInput(0).getAddress());
			for (int k = 1; k < call.getNumInputs(); k++) {
				Varnode argument = call.getInput(k);
				Parameter parameter = callee == null ? null : callee.getParameter(k - 1);
				Register register = parameter == null ? null : parameter.getRegister();
				String base = register == null ? null : register.getBaseRegister().getName();
				String name = namedHigh(argument);
				String assignment = name + " = (__typeof__(" + name + "))" + RESULT_PREFIX
					+ (base == null ? "" : base.toLowerCase());
				if (base != null && results.contains(base) && name != null && readAfter(argument, call)
					&& !assignments.contains(assignment)) {
					assignments.add(assignment);
				}
			}
			assignAfterCall(tokens, edits, i, call, assignments);
		}
	}

	/**
	 * Whether the variable a call is given is read after the call: by an
	 * operation at a later address, other than the call and the INDIRECTs
	 * that carry it past calls, on any instance of the variable.
	 */
	private static boolean readAfter(Varnode value, PcodeOp call) {
		Address at = call.getSeqnum().getTarget();
		HighVariable variable = value.getHigh();
		Varnode[] instances = variable == null ? new Varnode[] { value } : variable.getInstances();
		for (Varnode instance : instances) {
			for (Iterator<PcodeOp> readers = instance.getDescendants(); readers.hasNext();) {
				PcodeOp reader = readers.next();
				if (reader != call && reader.getOpcode() != PcodeOp.INDIRECT
					&& reader.getSeqnum().getTarget().compareTo(at) > 0) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Gives each register the C reads after a call to a routine that
	 * preserves it the value it had before: the decompiler takes the call
	 * to change a register its prototype does not name, so the C reads an
	 * extraout_ variable nothing set (modf's A0 after float_raise).  The
	 * value before is the INDIRECT's input, followed back through copies
	 * and casts to a variable or a constant the C names.  An INDIRECT
	 * whose input is a constant is one the decompiler made up, the
	 * register having no value it tracked, and the value is the one the
	 * instructions before the call wrote (valueWritten).
	 */
	private void preservedRegisters(List<ClangToken> tokens, HighFunction high, TokenEdits edits) {
		for (int i = 0; i < tokens.size(); i++) {
			PcodeOp call = tokens.get(i).getPcodeOp();
			if (!(tokens.get(i) instanceof ClangFuncNameToken) || call == null
				|| call.getOpcode() != PcodeOp.CALL) {
				continue;
			}
			Set<String> preserved = preserves(getFunctionAt(call.getInput(0).getAddress()));
			List<String> assignments = new ArrayList<>();
			for (PcodeOp op : indirects(high, call)) {
				Register register = currentProgram.getRegister(op.getOutput());
				String name = namedHigh(op.getOutput());
				if (register == null || name == null || !name.startsWith(EXTRA_OUTPUT)
					|| !preserved.contains(ALL_PRESERVED)
						&& !preserved.contains(register.getBaseRegister().getName())) {
					continue;
				}
				String before = op.getInput(0).isConstant()
						? valueWritten(call, register, high.getFunction())
						: valueName(op.getInput(0));
				if (before != null) {
					assignments.add(name + " = (__typeof__(" + name + "))" + before);
				}
			}
			assignAfterCall(tokens, edits, i, call, assignments);
		}
	}

	/**
	 * What the last instruction before call to write register wrote: the
	 * stack variable it read, as the C names it, or the immediate it
	 * moved; null for anything else, or none.  The way back runs through
	 * fall-throughs and, into an instruction nothing falls through to,
	 * the one branch to it, and stops where several paths meet.
	 */
	private String valueWritten(PcodeOp call, Register register, Function function) {
		Instruction at = getInstructionAt(call.getSeqnum().getTarget());
		for (int step = 0; at != null && step < WALK_LIMIT; step++) {
			at = predecessor(at);
			if (at == null || !function.getBody().contains(at.getAddress())) {
				return null;
			}
			for (Object result : at.getResultObjects()) {
				if (!(result instanceof Register written)
					|| !written.getBaseRegister().equals(register.getBaseRegister())) {
					continue;
				}
				for (Reference reference : at.getReferencesFrom()) {
					if (reference instanceof StackReference stack && reference.getReferenceType().isRead()) {
						Variable variable = function.getStackFrame()
								.getVariableContaining(stack.getStackOffset());
						return variable == null ? null : variable.getName();
					}
				}
				Scalar immediate = at.getScalar(0);
				return at.getNumOperands() == 2 && immediate != null
						? "0x" + Long.toHexString(immediate.getUnsignedValue())
						: null;
			}
		}
		return null;
	}

	/** The one instruction control reaches at from; null if there are several, or none. */
	private Instruction predecessor(Instruction at) {
		Instruction before = getInstructionBefore(at.getAddress());
		boolean fallsInto = before != null && at.getAddress().equals(before.getFallThrough());
		List<Address> branches = new ArrayList<>();
		for (Reference reference : currentProgram.getReferenceManager().getReferencesTo(at.getAddress())) {
			if (reference.getReferenceType().isFlow()) {
				branches.add(reference.getFromAddress());
			}
		}
		if (fallsInto && branches.isEmpty()) {
			return before;
		}
		return !fallsInto && branches.size() == 1 ? getInstructionAt(branches.get(0)) : null;
	}

	/** A value as the C names it, followed back through copies and casts; null if it has no name. */
	private static String valueName(Varnode value) {
		for (Varnode at = value; at != null; ) {
			if (at.isConstant()) {
				return "0x" + Long.toHexString(at.getOffset());
			}
			String name = namedHigh(at);
			if (name != null && !name.startsWith(EXTRA_OUTPUT)) {
				return name;
			}
			PcodeOp def = at.getDef();
			at = def != null && (def.getOpcode() == PcodeOp.COPY || def.getOpcode() == PcodeOp.CAST
				|| def.getOpcode() == PcodeOp.INDIRECT) ? def.getInput(0) : null;
		}
		return null;
	}

	/** The registers a function's trusted convention says it preserves; "all" for every one. */
	private Set<String> preserves(Function function) {
		String convention = function == null ? null : recorded(CONVENTION_MAP, function);
		Matcher preserves = convention == null ? null : PRESERVES.matcher(convention);
		Matcher confidence = convention == null ? null : CONFIDENCE.matcher(convention);
		if (preserves == null || !preserves.find() || !confidence.find()
			|| !TRUSTED.contains(confidence.group(1))) {
			return Set.of();
		}
		return Set.of(preserves.group(1).replace("\"", "").split(","));
	}

	/** The INDIRECTs at a call, which give the registers the decompiler takes it to change. */
	private static List<PcodeOp> indirects(HighFunction high, PcodeOp call) {
		List<PcodeOp> found = new ArrayList<>();
		for (Iterator<PcodeOpAST> ops = high.getPcodeOps(call.getSeqnum().getTarget()); ops.hasNext();) {
			PcodeOp op = ops.next();
			if (op.getOpcode() == PcodeOp.INDIRECT && op.getOutput() != null) {
				found.add(op);
			}
		}
		return found;
	}

	/** The name of a varnode's variable in the C, or null if it has none. */
	private static String namedHigh(Varnode varnode) {
		String name = varnode == null || varnode.getHigh() == null ? null : varnode.getHigh().getName();
		return name == null || name.equals("UNNAMED") ? null : name;
	}

	/**
	 * Makes assignments right after the call whose name is tokens[name]: a
	 * call with no result becomes call(...), a = ..., and one with a
	 * result ({ __auto_type __c = call(...); a = ...; __c; }).
	 */
	private void assignAfterCall(List<ClangToken> tokens, TokenEdits edits, int name, PcodeOp call,
			List<String> assignments) {
		int close = assignments.isEmpty() ? -1 : closingParenthesis(tokens, name);
		if (close < 0) {
			return;
		}
		Function callee = getFunctionAt(call.getInput(0).getAddress());
		if (callee.getReturnType() instanceof VoidDataType) {
			edits.after.merge(tokens.get(close), ", " + String.join(", ", assignments), String::concat);
		}
		else {
			edits.before.merge(tokens.get(name), ALSO_OPEN, String::concat);
			edits.after.merge(tokens.get(close), "; " + String.join("; ", assignments) + "; __c; })",
				String::concat);
		}
	}

	/** The registers a function's trusted convention says it also leaves results in. */
	private List<String> results(Function function) {
		String convention = function == null ? null : recorded(CONVENTION_MAP, function);
		Matcher results = convention == null ? null : RESULTS.matcher(convention);
		Matcher confidence = convention == null ? null : CONFIDENCE.matcher(convention);
		if (results == null || !results.find() || !confidence.find()
			|| !TRUSTED.contains(confidence.group(1))) {
			return List.of();
		}
		return List.of(results.group(1).replace("\"", "").split(","));
	}

	/** The registers a function's trusted convention says its result is also in, high first. */
	private List<String> copies(Function function) {
		String convention = function == null ? null : recorded(CONVENTION_MAP, function);
		Matcher copies = convention == null ? null : COPIES.matcher(convention);
		if (copies == null || !copies.find()) {
			return List.of();
		}
		Matcher own = COPIES_CONFIDENCE.matcher(convention);
		Matcher confidence = CONFIDENCE.matcher(convention);
		String level = own.find() ? own.group(1) : confidence.find() ? confidence.group(1) : "";
		return TRUSTED.contains(level) ? List.of(copies.group(1).split(":")) : List.of();
	}

	/** The variable each register the call sets outside its prototype is read from, by register. */
	/** A variable the C reads a register a call changes from, and how many bytes up from the register's low end it starts. */
	private record ExtraOutput(String name, int low) {}

	/**
	 * The variable each register the call changes is read from, by base
	 * register: an extraout_ variable, or one the decompiler named for an
	 * argument the value then feeds (format_duration's remainders).
	 */
	private Map<String, ExtraOutput> extraOutputs(HighFunction high, PcodeOp call) {
		Map<String, ExtraOutput> outputs = new HashMap<>();
		for (PcodeOp op : indirects(high, call)) {
			Register register = currentProgram.getRegister(op.getOutput());
			String name = namedHigh(op.getOutput());
			if (register != null && name != null) {
				Register base = register.getBaseRegister();
				int low = base.getOffset() + base.getMinimumByteSize()
					- register.getOffset() - register.getMinimumByteSize();
				outputs.put(base.getName(), new ExtraOutput(name, low));
			}
		}
		return outputs;
	}

	/** The index of the token closing the parenthesis that opens after tokens[start], or -1. */
	private static int closingParenthesis(List<ClangToken> tokens, int start) {
		int depth = 0;
		for (int i = start + 1; i < tokens.size(); i++) {
			ClangToken token = tokens.get(i);
			String text = token instanceof ClangSyntaxToken ? token.getText() : null;
			if ("(".equals(text)) {
				depth++;
			}
			else if (")".equals(text) && --depth == 0) {
				return i;
			}
			else if (depth == 0 && (text == null || !text.isBlank())) {
				return -1;
			}
		}
		return -1;
	}

	/** SmartClockSetup.java's record of each recovered switch, by its jmp: "TABLE FIRST ENTRIES". */
	private static final String SWITCH_MAP = "SmartClock.switch";
	/** The default's token, a ClangCaseToken too, with no value of its own. */
	private static final String DEFAULT_LABEL = "default";
	/** The most instructions valueWritten looks back through, so that a loop ends it. */
	private static final int WALK_LIMIT = 256;
	/** Bytes in a switch table's entry, a word offset from the table. */
	private static final int ENTRY_BYTES = 2;
	/**
	 * The misses in a row JumpBasicOverride::trialNorm tolerates, and how
	 * many labels past its targets' number it stops at.
	 */
	private static final int TRIAL_MISSES = 10;
	private static final int TRIAL_EXCESS = 100;
	/** The largest magnitude the decompiler prints in decimal. */
	private static final long DECIMAL_MAX = 10;
	/** What joins two labels of one case. */
	private static final String CASE_JOIN = ": case ";

	/**
	 * Gives each recovered switch's cases their own labels.  The override
	 * SmartClockSetup.java writes holds only the targets, so the decompiler
	 * labels them itself, trying index values from 0 up until it has seen
	 * every target, ten misses in a row allowed (JumpBasicOverride::trialNorm).
	 * It never tries an index below 0, which every table after subq #k
	 * has, so such a case has no label; an entry after the last new target
	 * gets none either; and code read as entries past the table gives a
	 * case labels it does not have.  This repeats that trial over the
	 * image and, if it gives the decompiler's labels, less the constant
	 * between index and the printed switch value, labels each case with
	 * every index whose entry targets it.  A target with no case is the
	 * default's.  A trial that misses a target is the decompiler's trivial
	 * model, which switches on the entry itself, labeled by its value: that
	 * is left as it is.
	 */
	private void switchLabels(List<ClangToken> tokens, TokenEdits edits) throws Exception {
		StringPropertyMap records = currentProgram.getUsrPropertyManager().getStringPropertyMap(SWITCH_MAP);
		if (records == null) {
			return;
		}
		Map<Address, List<ClangCaseToken>> switches = new LinkedHashMap<>();
		for (ClangToken token : tokens) {
			if (token instanceof ClangCaseToken label && label.getSwitchOp() != null
				&& label.getVarnode() != null && !label.getText().equals(DEFAULT_LABEL)) {
				switches.computeIfAbsent(label.getSwitchOp().getSeqnum().getTarget(),
					k -> new ArrayList<>()).add(label);
			}
		}
		for (Map.Entry<Address, List<ClangCaseToken>> recovered : switches.entrySet()) {
			String record = records.getString(recovered.getKey());
			if (record != null) {
				relabel(record, recovered.getValue(), edits);
			}
		}
	}

	/** One switch's relabeling, from its record; nothing if any of it cannot be done. */
	private void relabel(String record, List<ClangCaseToken> cases, TokenEdits edits) throws Exception {
		String[] fields = record.split(" ");
		Address table = currentProgram.getAddressFactory().getAddress(fields[0]);
		int first = Integer.parseInt(fields[1]);
		List<Address> targets = new ArrayList<>();
		for (int i = 0; i < Integer.parseInt(fields[2]); i++) {
			targets.add(entryTarget(table, i));
		}
		Map<Long, Address> trial = trialLabels(table, first, targets);
		int size = cases.get(0).getVarnode().getSize();
		long mask = size >= MAX_BYTES ? -1L : (1L << (8 * size)) - 1;
		Set<Long> printed = new HashSet<>();
		for (ClangCaseToken label : cases) {
			printed.add(label.getValue() & mask);
		}
		Long offset = trial == null ? null : labelOffset(printed, trial, mask);
		if (offset == null) {
			return;
		}
		Map<Address, Set<Long>> wanted = new HashMap<>();
		for (int i = 0; i < targets.size(); i++) {
			wanted.computeIfAbsent(targets.get(i), k -> new HashSet<>()).add((i + first + offset) & mask);
		}
		Map<Address, List<ClangCaseToken>> byTarget = new LinkedHashMap<>();
		for (ClangCaseToken label : cases) {
			byTarget.computeIfAbsent(trial.get((label.getValue() - offset) & mask),
				k -> new ArrayList<>()).add(label);
		}
		Scalar scalar = cases.get(0).getScalar();
		boolean signed = scalar == null || scalar.isSigned();
		TokenEdits mine = new TokenEdits();
		for (Map.Entry<Address, List<ClangCaseToken>> target : byTarget.entrySet()) {
			List<ClangCaseToken> labels = target.getValue();
			Set<Long> have = new HashSet<>();
			for (ClangCaseToken label : labels) {
				have.add(label.getValue() & mask);
			}
			if (have.equals(wanted.get(target.getKey()))) {
				continue;
			}
			List<String> texts = wanted.get(target.getKey()).stream()
				.map(value -> labelValue(value, size, signed)).sorted()
				.map(SmartClockExportC::labelText).toList();
			mine.replaced.put(labels.get(0), String.join(CASE_JOIN, texts));
			for (ClangCaseToken extra : labels.subList(1, labels.size())) {
				ClangLine line = extra.getLineParent();
				if (line == null || !line.getAllTokens().stream()
					.allMatch(t -> t == extra || t instanceof ClangSyntaxToken)) {
					return;
				}
				mine.dropped.add(line);
			}
		}
		edits.replaced.putAll(mine.replaced);
		edits.dropped.addAll(mine.dropped);
	}

	/** Where a switch table's entry sends the jmp: the table plus the entry's word. */
	private Address entryTarget(Address table, long index) throws Exception {
		return table.add(currentProgram.getMemory().getShort(table.add(index * ENTRY_BYTES)));
	}

	/**
	 * The labels the decompiler's trial gives, each value from 0 up to the
	 * target its entry gives, or null if the trial does not see every target.
	 */
	private Map<Long, Address> trialLabels(Address table, int first, List<Address> targets) {
		Set<Address> all = new HashSet<>(targets);
		Set<Address> seen = new HashSet<>();
		Map<Long, Address> hits = new LinkedHashMap<>();
		int misses = 0;
		for (long value = 0; seen.size() < all.size(); value++) {
			Address target;
			try {
				target = entryTarget(table, value - first);
			}
			catch (Exception e) {
				break;
			}
			if (all.contains(target)) {
				seen.add(target);
				hits.put(value, target);
				misses = 0;
				if (hits.size() > all.size() + TRIAL_EXCESS) {
					break;
				}
			}
			else if (++misses >= TRIAL_MISSES) {
				break;
			}
		}
		return seen.size() == all.size() ? hits : null;
	}

	/**
	 * The constant between the trial's values and the printed labels, or
	 * null if none gives them: each label a value's, and each value whose
	 * target has a case one of its labels.
	 */
	private static Long labelOffset(Set<Long> printed, Map<Long, Address> trial, long mask) {
		for (long label : printed) {
			for (long value : trial.keySet()) {
				long offset = label - value;
				Set<Address> labeled = new HashSet<>();
				boolean fits = true;
				for (long other : printed) {
					Address target = trial.get((other - offset) & mask);
					fits &= target != null;
					labeled.add(target);
				}
				boolean complete = trial.entrySet().stream()
					.filter(hit -> labeled.contains(hit.getValue()))
					.allMatch(hit -> printed.contains((hit.getKey() + offset) & mask));
				if (fits && complete) {
					return offset;
				}
			}
		}
		return null;
	}

	/** A label's value in the switch value's size, sign extended if it is signed. */
	private static long labelValue(long value, int size, boolean signed) {
		int unused = Long.SIZE - 8 * size;
		return signed && unused > 0 ? (value << unused) >> unused : value;
	}

	/** A label as the decompiler prints it: decimal up to ten, hexadecimal beyond. */
	private static String labelText(long value) {
		return Math.abs(value) <= DECIMAL_MAX ? Long.toString(value)
			: (value < 0 ? "-0x" + Long.toHexString(-value) : "0x" + Long.toHexString(value));
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

	/** An infinity or NaN the C assigns a variable: its indent, the variable and the value. */
	private static final Pattern NONFINITE_ASSIGNED =
		Pattern.compile("^(\\s*)(\\w+) = (-?(?:INFINITY|NAN));$", Pattern.MULTILINE);
	/** A local's declaration, its type and its name. */
	private static final Pattern LOCAL_DECLARATION =
		Pattern.compile("^  ([A-Za-z_]\\w*) (\\w+);$", Pattern.MULTILINE);
	/** The integer types as wide as a double; a narrower one holds a float's bits. */
	private static final Set<String> DOUBLE_WIDE = Set.of("undefined8", "longlong", "ulonglong",
		"int64_t", "uint64_t");

	/**
	 * c with each infinity or NaN it assigns an integer variable that
	 * variable's bits: the decompiler copies the constant's bits where C
	 * converts its value, which for an infinity is undefined, and gcc
	 * gives no diagnostic for reinterpretConversions to find.
	 */
	private static String nonfiniteBits(String c) {
		Map<String, String> types = new HashMap<>();
		Matcher declaration = LOCAL_DECLARATION.matcher(c);
		while (declaration.find()) {
			types.put(declaration.group(2), declaration.group(1));
		}
		Matcher assigned = NONFINITE_ASSIGNED.matcher(c);
		StringBuilder out = new StringBuilder();
		while (assigned.find()) {
			String type = types.get(assigned.group(2));
			String value = assigned.group(3);
			if (type != null && INTEGERS.contains(type)) {
				// INFINITY and NAN are floats; a cast would be made a reinterpretation
				value = "__ghidra_bits(" + (DOUBLE_WIDE.contains(type)
					? value.replace("INFINITY", "__builtin_inf()").replace("NAN", "__builtin_nan(\"\")")
					: value) + ")";
			}
			assigned.appendReplacement(out,
				Matcher.quoteReplacement(assigned.group(1) + assigned.group(2) + " = " + value + ";"));
		}
		assigned.appendTail(out);
		return out.toString();
	}

	/** A cast to a type named in one or two words. */
	private static final Pattern CAST = Pattern.compile("\\(((?:unsigned |signed )?[A-Za-z_]\\w*)\\)");
	/** The decompiler's floating types; its unkfloatN are integers here. */
	private static final Set<String> FLOATING = Set.of("float", "double");
	/** The integer types it casts to. */
	private static final Set<String> INTEGERS = Set.of("char", "uchar", "byte", "sbyte", "short",
		"ushort", "int", "uint", "long", "ulong", "longlong", "ulonglong", "bool", "undefined",
		"undefined1", "undefined2", "undefined4", "undefined8", "int8_t", "uint8_t", "int16_t",
		"uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t", "unsigned int", "unsigned char",
		"unsigned short", "unsigned long", "signed char");
	/** A floating declaration in a function, its name the group. */
	private static final Pattern FLOATING_VARIABLE =
		Pattern.compile("\\b(?:float|double) (\\w+)(?=[;,)\\[])");
	private static final Pattern FLOATING_POINTER =
		Pattern.compile("\\b(?:float|double) \\*(\\w+)(?=[;,)])|\\b(?:float|double) (\\w+)\\[");
	/** The last member of a member access. */
	private static final Pattern LAST_MEMBER = Pattern.compile("(?:\\.|->)(\\w+)$");
	private static final Pattern IDENTIFIER_START = Pattern.compile("^[A-Za-z_]\\w*");

	/** The program's functions that return a floating value, its floating fields and globals. */
	private Set<String> floatingFunctions;
	private Set<String> floatingFields;
	private Set<String> floatingGlobals;
	/** The program's structures and unions, by name. */
	private Set<String> composites;

	private static boolean isFloating(DataType type) {
		DataType base = type instanceof TypeDef typedef ? typedef.getBaseDataType() : type;
		return base instanceof AbstractFloatDataType;
	}

	private void collectFloating() {
		floatingFunctions = new HashSet<>();
		floatingFields = new HashSet<>();
		floatingGlobals = new HashSet<>();
		composites = new HashSet<>();
		for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
			if (isFloating(function.getReturnType())) {
				floatingFunctions.add(function.getName());
			}
		}
		Iterator<DataType> types = currentProgram.getDataTypeManager().getAllDataTypes();
		while (types.hasNext()) {
			if (types.next() instanceof Composite composite) {
				composites.add(composite.getName());
				for (DataTypeComponent component : composite.getDefinedComponents()) {
					if (component.getFieldName() != null && isFloating(component.getDataType())) {
						floatingFields.add(component.getFieldName());
					}
				}
			}
		}
		for (Data data : currentProgram.getListing().getDefinedData(true)) {
			if (data.getLabel() != null && isFloating(data.getDataType())) {
				floatingGlobals.add(data.getLabel());
			}
		}
	}

	/**
	 * c with each cast between a floating type and an integer one made a
	 * reinterpretation of its bits, as the decompiler means it: these
	 * images have no FPU, so it never sees a conversion, which the float
	 * library makes by calls, and a C cast would convert the value.  A
	 * cast to float or double is __ghidra_as(type, v), the integer's bits
	 * as that type; an integer cast of a floating operand -- a floating
	 * variable, a call to a function returning one, a floating pointer
	 * read, a floating field -- is (type)__ghidra_bits(v).  A cast to a
	 * structure, which C has none of, is __ghidra_as too: the decompiler
	 * builds a structure argument from the bytes pushed.  Casts are
	 * rewritten last first, so an operand's own are done before it.
	 */
	private String reinterpretingCasts(String c) {
		if (floatingFunctions == null) {
			collectFloating();
		}
		Set<String> variables = new HashSet<>(floatingGlobals);
		Matcher declared = FLOATING_VARIABLE.matcher(c);
		while (declared.find()) {
			variables.add(declared.group(1));
		}
		Set<String> pointers = new HashSet<>();
		Matcher pointer = FLOATING_POINTER.matcher(c);
		while (pointer.find()) {
			pointers.add(pointer.group(1) != null ? pointer.group(1) : pointer.group(2));
		}
		List<int[]> casts = new ArrayList<>();
		boolean[] code = code(c);
		Matcher cast = CAST.matcher(c);
		while (cast.find()) {
			String type = cast.group(1);
			char before = cast.start() == 0 ? ' ' : c.charAt(cast.start() - 1);
			if (code[cast.start()]
				&& (FLOATING.contains(type) || INTEGERS.contains(type) || composites.contains(type))
				&& !Character.isJavaIdentifierPart(before) && before != ')' && before != ']') {
				casts.add(new int[] { cast.start(), cast.end() });
			}
		}
		StringBuilder out = new StringBuilder(c);
		for (int k = casts.size() - 1; k >= 0; k--) {
			int start = casts.get(k)[0];
			int typeEnd = casts.get(k)[1];
			String type = out.substring(start + 1, typeEnd - 1);
			int end = operandEnd(out, typeEnd);
			if (end < 0) {
				continue;
			}
			String operand = out.substring(typeEnd, end);
			boolean floating = floatingOperand(operand, variables, pointers);
			if (FLOATING.contains(type) && !floating || composites.contains(type)) {
				out.replace(start, end, "__ghidra_as(" + type + ", " + operand + ")");
			}
			else if (INTEGERS.contains(type) && floating) {
				out.replace(start, end, "(" + type + ")__ghidra_bits(" + operand + ")");
			}
		}
		return out.toString();
	}

	/** Which of c's characters are code: not in a comment, a string or a character constant. */
	private static boolean[] code(String c) {
		boolean[] code = new boolean[c.length()];
		int i = 0;
		while (i < c.length()) {
			if (c.startsWith("/*", i)) {
				int close = c.indexOf(COMMENT_END, i + 2);
				i = close < 0 ? c.length() : close + COMMENT_END.length();
			}
			else if (c.startsWith("//", i)) {
				int close = c.indexOf('\n', i);
				i = close < 0 ? c.length() : close;
			}
			else if (c.charAt(i) == '"' || c.charAt(i) == '\'') {
				char quote = c.charAt(i);
				i++;
				while (i < c.length() && c.charAt(i) != quote) {
					i += c.charAt(i) == '\\' ? 2 : 1;
				}
				i++;
			}
			else {
				code[i++] = true;
			}
		}
		return code;
	}

	/** A shift, its operator the group. */
	private static final Pattern SHIFT = Pattern.compile("(<<|>>) ");
	private static final Pattern LITERAL = Pattern.compile("^(?:0x[0-9a-fA-F]+|[0-9]+)[uUlL]*$");
	/**
	 * The decompiler's reading of a 68k rotate of a long by a register,
	 * x << n | x >> (32 - n), its count n & 0x3f, after registerShifts: the
	 * two shifts of the same x by the same count.
	 */
	private static final Pattern ROTATE = Pattern.compile(
		"__ghidra_shl\\(([^()]+), \\(([^()]+ & 0x3f)\\)\\) \\| __ghidra_shr\\(\\1, 0x20 - \\(\\2\\)\\)");

	/**
	 * c with each rotate left the decompiler wrote as two shifts made
	 * __ghidra_rotl(x, n): the 68k's rol by a register rotates by its
	 * count modulo 64, and so by n modulo the width, where the two shifts
	 * give 0 for a count from the width to 63 (rotate_left).
	 */
	private static String rotates(String c) {
		Matcher rotate = ROTATE.matcher(c);
		StringBuilder out = new StringBuilder();
		while (rotate.find()) {
			rotate.appendReplacement(out,
				Matcher.quoteReplacement("__ghidra_rotl(" + rotate.group(1) + ", " + rotate.group(2) + ")"));
		}
		rotate.appendTail(out);
		return out.toString();
	}

	/** Operators of lower precedence than a shift, which end its operands. */
	private static final String LOWER = "=<>&^|?:,;{";

	/**
	 * c with each shift by a count that is not a literal made
	 * __ghidra_shl(x, n) or __ghidra_shr(x, n): the decompiler's shift,
	 * like the 68k's by a register (x << (n & 0x3f), modulo 64), gives 0
	 * for a count as wide as x or wider, or every bit the sign for an
	 * arithmetic shift right, where C leaves it undefined.  Shifts are
	 * rewritten last first, so a count's own are done before it; one
	 * whose left operand is itself a shift is left, as rewriting it would
	 * change their grouping.
	 */
	private static String registerShifts(String c) {
		List<Integer> operators = new ArrayList<>();
		boolean[] code = code(c);
		Matcher shift = SHIFT.matcher(c);
		while (shift.find()) {
			if (code[shift.start()]) {
				operators.add(shift.start());
			}
		}
		StringBuilder out = new StringBuilder(c);
		for (int k = operators.size() - 1; k >= 0; k--) {
			int operator = operators.get(k);
			int countStart = operator + 3;
			int countEnd = rightOperandEnd(out, countStart);
			while (countEnd > countStart && Character.isWhitespace(out.charAt(countEnd - 1))) {
				countEnd--;
			}
			String count = out.substring(countStart, countEnd).strip();
			if (count.isEmpty() || LITERAL.matcher(count).matches()) {
				continue;
			}
			int start = leftOperandStart(out, operator);
			if (start < 0) {
				continue;
			}
			while (start < operator && Character.isWhitespace(out.charAt(start))) {
				start++;
			}
			String left = out.substring(start, operator).strip();
			if (left.isEmpty()) {
				continue;
			}
			String macro = out.charAt(operator) == '<' ? "__ghidra_shl(" : "__ghidra_shr(";
			out.replace(start, countEnd, macro + left + ", " + count + ")");
			// the shifts in left, still to do, move on by the macro's name
			for (int j = 0; j < k; j++) {
				if (operators.get(j) >= start) {
					operators.set(j, operators.get(j) + macro.length());
				}
			}
		}
		return out.toString();
	}

	/**
	 * Where the right operand of a shift that starts at start ends: on
	 * past brackets to the nearest operator of lower precedence, or as
	 * low, or an unmatched closing bracket.
	 */
	private static int rightOperandEnd(CharSequence c, int start) {
		int depth = 0;
		boolean operand = false;
		for (int i = start; i < c.length(); i++) {
			char ch = c.charAt(i);
			if (ch == '"' || ch == '\'') {
				int close = i + 1;
				while (close < c.length() && c.charAt(close) != ch) {
					close += c.charAt(close) == '\\' ? 2 : 1;
				}
				i = close;
				operand = true;
			}
			else if (ch == '(' || ch == '[') {
				depth++;
				operand = false;
			}
			else if (ch == ')' || ch == ']') {
				if (depth == 0) {
					return i;
				}
				depth--;
				operand = true;
			}
			else if (depth == 0 && (LOWER.indexOf(ch) >= 0
				&& !(ch == '>' && i > 0 && c.charAt(i - 1) == '-') && !(ch == '&' && !operand)
				|| ch == '!' && i + 1 < c.length() && c.charAt(i + 1) == '=')) {
				return i;
			}
			else if (Character.isJavaIdentifierPart(ch)) {
				operand = true;
			}
			else if (!Character.isWhitespace(ch)) {
				operand = false;
			}
		}
		return c.length();
	}

	/**
	 * Where the left operand of the shift at operator starts: back past
	 * brackets to the nearest operator of lower precedence, an unmatched
	 * opening bracket or the start of a statement; -1 if it is another
	 * shift, whose grouping the rewrite would change.
	 */
	private static int leftOperandStart(CharSequence c, int operator) {
		int i = operator - 1;
		int depth = 0;
		while (i >= 0) {
			char ch = c.charAt(i);
			if (ch == ')' || ch == ']') {
				depth++;
			}
			else if (ch == '(' || ch == '[') {
				if (depth == 0) {
					return i + 1;
				}
				depth--;
			}
			else if (depth == 0) {
				if ((ch == '<' || ch == '>') && i > 0 && c.charAt(i - 1) == ch) {
					return -1;
				}
				// a binary & ends it; a unary one, after an operator, is the operand's
				if (LOWER.indexOf(ch) >= 0 && !(ch == '>' && i > 0 && c.charAt(i - 1) == '-')
					&& !(ch == '&' && unaryAt(c, i))) {
					return i + 1;
				}
				if (ch == 'n' && i >= 5 && c.subSequence(i - 5, i + 1).toString().equals("return")
					&& (i < 6 || !Character.isJavaIdentifierPart(c.charAt(i - 6)))) {
					return i + 1;
				}
			}
			i--;
		}
		return 0;
	}

	/** Whether the & at i is unary: what precedes it is an operator, not an operand. */
	private static boolean unaryAt(CharSequence c, int i) {
		int j = i - 1;
		while (j >= 0 && Character.isWhitespace(c.charAt(j))) {
			j--;
		}
		if (j < 0) {
			return true;
		}
		char before = c.charAt(j);
		return !(Character.isJavaIdentifierPart(before) || before == ')' || before == ']');
	}

	/**
	 * Where the operand of a cast that ends at start ends: a unary
	 * expression, its prefix operators and casts, then a primary and its
	 * calls, subscripts and members; -1 if it is none of those.
	 */
	private static int operandEnd(CharSequence c, int start) {
		int i = start;
		while (i < c.length() && Character.isWhitespace(c.charAt(i))) {
			i++;
		}
		while (i < c.length() && "-~!*&".indexOf(c.charAt(i)) >= 0
			&& !(c.charAt(i) == '-' && i + 1 < c.length() && c.charAt(i + 1) == '>')) {
			i++;
		}
		if (i >= c.length()) {
			return -1;
		}
		char first = c.charAt(i);
		if (first == '(') {
			int close = matching(c, i);
			if (close < 0) {
				return -1;
			}
			String inner = c.subSequence(i + 1, close).toString().strip();
			if (inner.matches("(?:unsigned |signed )?[A-Za-z_]\\w*(?: \\*+)?")
				&& !inner.matches("[A-Za-z_]\\w*") || FLOATING.contains(inner) || INTEGERS.contains(inner)) {
				return operandEnd(c, close + 1);
			}
			i = close + 1;
		}
		else if (Character.isJavaIdentifierStart(first) || Character.isDigit(first)) {
			while (i < c.length() && (Character.isJavaIdentifierPart(c.charAt(i))
				|| c.charAt(i) == '.' && Character.isDigit(first))) {
				i++;
			}
		}
		else if (first == '"' || first == '\'') {
			int close = i + 1;
			while (close < c.length() && c.charAt(close) != first) {
				close += c.charAt(close) == '\\' ? 2 : 1;
			}
			i = close + 1;
		}
		else {
			return -1;
		}
		while (i < c.length()) {
			// the decompiler may break a line before a call's arguments
			int spaced = i;
			while (spaced < c.length() && Character.isWhitespace(c.charAt(spaced))) {
				spaced++;
			}
			if (spaced < c.length() && (c.charAt(spaced) == '(' || c.charAt(spaced) == '[')) {
				i = spaced;
			}
			char next = c.charAt(i);
			if (next == '(' || next == '[') {
				int close = matching(c, i);
				if (close < 0) {
					return -1;
				}
				i = close + 1;
			}
			else if (next == '.' || next == '-' && i + 1 < c.length() && c.charAt(i + 1) == '>') {
				int member = i + (next == '.' ? 1 : 2);
				int memberEnd = member;
				while (memberEnd < c.length() && Character.isJavaIdentifierPart(c.charAt(memberEnd))) {
					memberEnd++;
				}
				if (memberEnd == member) {
					break;
				}
				i = memberEnd;
			}
			else {
				break;
			}
		}
		return i;
	}

	/** The bracket that closes the one at open, past strings; -1 if none. */
	private static int matching(CharSequence c, int open) {
		int depth = 0;
		for (int i = open; i < c.length(); i++) {
			char ch = c.charAt(i);
			if (ch == '"' || ch == '\'') {
				i++;
				while (i < c.length() && c.charAt(i) != ch) {
					i += c.charAt(i) == '\\' ? 2 : 1;
				}
			}
			else if (ch == '(' || ch == '[') {
				depth++;
			}
			else if (ch == ')' || ch == ']') {
				if (--depth == 0) {
					return i;
				}
			}
		}
		return -1;
	}

	/** Where the statement in text from start ends: its first semicolon outside brackets. */
	private static int statementEnd(String text, int start) {
		int depth = 0;
		for (int i = start; i < text.length(); i++) {
			char ch = text.charAt(i);
			if (ch == '"' || ch == '\'') {
				i++;
				while (i < text.length() && text.charAt(i) != ch) {
					i += text.charAt(i) == '\\' ? 2 : 1;
				}
			}
			else if (ch == '(' || ch == '[') {
				depth++;
			}
			else if (ch == ')' || ch == ']') {
				depth--;
			}
			else if (ch == ';' && depth == 0) {
				return i;
			}
		}
		return text.length();
	}

	/** Whether a cast's operand is a floating value. */
	private boolean floatingOperand(String operand, Set<String> variables, Set<String> pointers) {
		String s = operand.strip();
		if (s.startsWith("__ghidra_as(float") || s.startsWith("__ghidra_as(double")) {
			return true;
		}
		if (s.startsWith(ALSO_OPEN)) {
			// a call whose result is also left elsewhere is its call
			return floatingOperand(s.substring(ALSO_OPEN.length(), statementEnd(s, ALSO_OPEN.length())),
				variables, pointers);
		}
		int stars = 0;
		while (!s.isEmpty() && "-~!*".indexOf(s.charAt(0)) >= 0) {
			stars += s.charAt(0) == '*' ? 1 : 0;
			s = s.substring(1);
		}
		Matcher head = IDENTIFIER_START.matcher(s);
		if (!head.find()) {
			return false;
		}
		String name = head.group();
		String rest = s.substring(name.length());
		if (rest.isEmpty()) {
			return stars == 0 ? variables.contains(name) : stars == 1 && pointers.contains(name);
		}
		if (rest.startsWith("(") && matching(rest, 0) == rest.length() - 1) {
			return stars == 0 && floatingFunctions.contains(name);
		}
		if (rest.startsWith("[") && matching(rest, 0) == rest.length() - 1) {
			return stars == 0 && pointers.contains(name);
		}
		Matcher member = LAST_MEMBER.matcher(rest);
		return stars == 0 && member.find() && floatingFields.contains(member.group(1));
	}

	/** The compiler that finds the conversions reinterpretConversions makes bitwise. */
	private static final String COMPILER = "m68k-linux-gnu-gcc";
	private static final List<String> CONVERSION_FLAGS = List.of("-std=gnu17", "-mcpu=cpu32",
		"-fsyntax-only", "-Wfloat-conversion", "-Wconversion", "-Wno-sign-conversion",
		"-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types",
		"-fdiagnostics-format=json");
	/** A conversion's types in gcc's message, the C name first in each. */
	private static final Pattern CONVERSION =
		Pattern.compile("^conversion from \u2018([^\u2019]+)\u2019(?: \\{aka \u2018[^\u2019]+\u2019\\})?"
			+ " to \u2018([^\u2019]+)\u2019");
	private static final Pattern NUMBER = Pattern.compile("^-?[0-9][0-9a-fA-FxX.eE+-]*[fFlLuU]*$");

	/**
	 * Makes each implicit conversion between a floating type and an
	 * integer one in the written C a reinterpretation, as the cast pass
	 * does each cast: the decompiler copies bits where C, assigning a
	 * double to its undefined8 or passing one as an integer, converts the
	 * value.  gcc finds them, -Wfloat-conversion and -Wconversion, and
	 * gives each expression's range; a floating value bound for an integer
	 * becomes __ghidra_bits(v), an integer bound for a floating type
	 * __ghidra_as(type, v), and a literal is left to convert.  Returns
	 * what it did, or why it could not.
	 */
	private String reinterpretConversions(File output) throws Exception {
		JsonArray found = conversions(output);
		if (found == null) {
			return "conversions left as C makes them: no " + COMPILER;
		}
		StringBuilder c = new StringBuilder(Files.readString(output.toPath()));
		List<Integer> lineStarts = new ArrayList<>(List.of(0));
		for (int i = 0; i < c.length(); i++) {
			if (c.charAt(i) == '\n') {
				lineStarts.add(i + 1);
			}
		}
		// each wrap: its start, its end and its opening text, by start
		TreeMap<Integer, Object[]> wraps = new TreeMap<>();
		for (JsonElement element : found) {
			JsonObject diagnostic = element.getAsJsonObject();
			Matcher conversion = CONVERSION.matcher(diagnostic.get("message").getAsString());
			conversion.find();
			JsonObject caret = diagnostic.getAsJsonArray("locations").get(0).getAsJsonObject()
				.getAsJsonObject("caret");
			int at = lineStarts.get(caret.get("line").getAsInt() - 1)
				+ caret.get("byte-column").getAsInt() - 1;
			// gcc's caret may be inside the expression, at a member's dot
			int first = at;
			while (first > 0 && (Character.isJavaIdentifierPart(c.charAt(first - 1))
				|| c.charAt(first - 1) == '.' || c.charAt(first - 1) == '>' && first > 1
					&& c.charAt(first - 2) == '-')) {
				first -= c.charAt(first - 1) == '>' ? 2 : 1;
			}
			int last = operandEnd(c, first);
			if (last <= at || NUMBER.matcher(c.substring(first, last).strip()).matches()) {
				continue;
			}
			String to = conversion.group(2);
			wraps.put(first, new Object[] { last,
				FLOATING.contains(to) ? "__ghidra_as(" + to + ", " : "__ghidra_bits(" });
		}
		// each opening and close is a point, put in last first so that
		// every position still holds; where a close and an opening meet,
		// the opening goes in first, to come after
		TreeMap<Integer, StringBuilder> points = new TreeMap<>();
		for (Map.Entry<Integer, Object[]> wrap : wraps.entrySet()) {
			points.computeIfAbsent(wrap.getKey(), k -> new StringBuilder())
				.append((String) wrap.getValue()[1]);
			points.computeIfAbsent((int) wrap.getValue()[0], k -> new StringBuilder()).insert(0, ")");
		}
		for (Map.Entry<Integer, StringBuilder> point : points.descendingMap().entrySet()) {
			c.insert(point.getKey(), point.getValue());
		}
		Files.writeString(output.toPath(), c);
		JsonArray left = conversions(output);
		return "conversions made bitwise: " + wraps.size() + ", left: "
			+ (left == null ? "?" : left.size());
	}

	/**
	 * The diagnostics gcc gives for the C's conversions between a floating
	 * type and an integer one; null if there is no gcc.
	 */
	private static JsonArray conversions(File output) throws Exception {
		List<String> command = new ArrayList<>(List.of(COMPILER));
		command.addAll(CONVERSION_FLAGS);
		command.add(output.getPath());
		Process process;
		try {
			process = new ProcessBuilder(command).redirectOutput(ProcessBuilder.Redirect.DISCARD)
				.start();
		}
		catch (IOException e) {
			return null;
		}
		String diagnostics = new String(process.getErrorStream().readAllBytes());
		process.waitFor();
		JsonArray found = new JsonArray();
		for (JsonElement element : JsonParser.parseString(diagnostics).getAsJsonArray()) {
			JsonObject diagnostic = element.getAsJsonObject();
			Matcher conversion = CONVERSION.matcher(diagnostic.get("message").getAsString());
			if (conversion.find() && !diagnostic.getAsJsonArray("locations").isEmpty()
				&& FLOATING.contains(conversion.group(1)) != FLOATING.contains(conversion.group(2))) {
				found.add(diagnostic);
			}
		}
		return found;
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
