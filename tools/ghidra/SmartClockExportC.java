// Writes every function's decompiled C, in address order, to the file
// named by the script's first argument, after the program's structures
// and a declaration of each named, typed global, so that the file
// stands alone.
//
//@category SmartClock

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

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
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

public class SmartClockExportC extends GhidraScript {

	private static final int TIMEOUT_SECONDS = 120;

	/** SmartClockSetup.java's label for a switch's table. */
	private static final String SWITCH_TABLE_PREFIX = "switchD_";

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
					writer.print(results.getDecompiledFunction().getC());
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
		println("decompiled " + written + ", failed " + failed + " -> " + output);
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
