// Writes every function's decompiled C, in address order, to the file
// named by the script's first argument, after the program's structures
// and a declaration of each named, typed global, so that the file
// stands alone.  Beside it, with .conventions.jsonl for .c, writes one
// JSON object a line for every function: its address and name, its note
// and the calling convention SmartClockSetup.java and SmartClockForth.java
// recorded, or "unknown".
//
// A function that is not C has its signature line rewritten, so that it
// says so as far as C can: a macro for its kind, each one gcc's
// __attribute__((naked)) and, for an exception handler, interrupt; gcc's
// m68k __asm("d0") on each parameter in a register, where the convention
// is trusted (run, read or documented); and a comment after it for what C
// cannot say, its results, condition codes, errors and preserved
// registers.  Ghidra's "Unknown calling convention" warning is dropped
// for it.  The rest is the decompiler's text as it gives it.
//
//@category SmartClock

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.Iterator;
import java.util.List;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
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
import ghidra.program.model.listing.Data;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
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
		 * A function that is not C has its signature rewritten: a macro for its
		 * kind, gcc's m68k __asm("d0") on each parameter in a register where its
		 * convention is trusted, and a comment after it for what C cannot say.
		 * Its comment, and z3805a-3543b.conventions.jsonl, give the whole of it.
		 */
		#define __naked     __attribute__((naked))
		#define __regcall   __naked  /* arguments and results in registers */
		#define __pforth    __naked  /* A6 data stack, A5 user area's end, A4 dictionary, A3 input */
		#define __exception __attribute__((naked, interrupt)) /* entered by the processor */
		""";
	private static final String SIDECAR_SUFFIX = ".conventions.jsonl";

	@Override
	protected void run() throws Exception {
		File output = new File(getScriptArgs()[0]);
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);
		int written = 0;
		int failed = 0;
		try (PrintWriter writer = new PrintWriter(output)) {
			writer.println("/* " + currentProgram.getName() + ", "
				+ currentProgram.getLanguageID() + " */");
			writer.print(MACROS);
			writeTypes(writer);
			writeGlobals(writer);
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				DecompileResults results =
					decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
				writer.println();
				writer.println("/* " + function.getEntryPoint() + " */");
				if (results.decompileCompleted()) {
					writer.print(notC(function, results.getDecompiledFunction().getC()));
					written++;
				}
				else {
					writer.println("/* " + function.getName() + ": decompilation failed: "
						+ results.getErrorMessage().strip() + " */");
					failed++;
				}
			}
		}
		finally {
			decompiler.dispose();
		}
		File sidecar = new File(output.getPath().replaceFirst(
			Pattern.quote(C_SUFFIX) + "$", "") + SIDECAR_SUFFIX);
		int recorded = writeConventions(sidecar);
		println("decompiled " + written + ", failed " + failed + " -> " + output);
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
		String convention = recorded(CONVENTION_MAP, function);
		Matcher kind = convention == null ? null : KIND.matcher(convention);
		String macro = kind != null && kind.find() ? KIND_MACROS.get(kind.group(1)) : null;
		if (macro == null) {
			return c;
		}
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
						: argument + " __asm(\"" + register + "\")");
				}
				line = macro + " " + line.substring(0, open + function.getName().length() + 1)
					+ String.join(",", annotated) + line.substring(close)
					+ (signature == null ? "" : " /* " + signature + " */");
				rewritten = true;
			}
			out.append(line).append('\n');
		}
		return out.substring(0, out.length() - 1);
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
		new DataTypeWriter(types, writer).write(defined, monitor);
		writer.println("#pragma pack(pop)");
	}

	/**
	 * An extern for each global a script or the names file named and gave
	 * a type, with its address; not the switch tables, which only their
	 * own jmp uses.
	 */
	private void writeGlobals(PrintWriter writer) {
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
			writer.println("extern " + declaration(data, data.getDataType(), symbol.getName())
				+ "; /* " + symbol.getAddress() + " */");
		}
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
