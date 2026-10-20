// Gives the pForth console's kernel its register convention, so its
// words decompile as reading their user area and data stack rather than
// unaffected registers.  Run after SmartClockSetup.java with the names
// (the kernel is found by its forth_ names).  docs/firmware/console.md,
// "The kernel's registers", sets out what each register and user
// variable is and which words show it.
//
//   A6  the data stack, growing down: (A6)+ pops, -(A6) pushes
//   A5  just past the user area, read at negative offsets
//   A4  the dictionary pointer, where the next compiled word goes
//   A3  the input, the next character of the text interpreted
//
// A function in the kernel's span that uses one of these, and does not
// use A6 as a frame pointer, gets each register it uses as a parameter
// in custom storage, A5 typed as a pointer to the user area that sits at
// its end.  One with register parameters already, from the names file,
// keeps them and gains the rest; one with stack parameters is a C
// function and is left alone.  Each one not C, whichever registers it
// uses, also has a line added to its comment saying so, unless the
// comment already does, and its convention recorded for the sidecar, as
// SmartClockSetup.java records the names file's, unless it has one.
//
// Argument: dry-run, to report the functions without changing them.
//
//@category SmartClock

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.CharDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.LongDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.PointerTypedefBuilder;
import ghidra.program.model.data.ShortDataType;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.util.StringPropertyMap;

public class SmartClockForth extends GhidraScript {

	/** A user variable: its offset from the start of the area, name and type. */
	private record Field(int offset, String name, DataType type) {}

	/** A kernel register: its name, the parameter's name and its type. */
	private record KernelRegister(String register, String name, DataType type) {}

	private static final CategoryPath CATEGORY = new CategoryPath("/pforth");
	private static final String USER_AREA = "forth_user";
	private static final String USER_POINTER = "forth_user_end";
	/** A5 points this far past the start of the user area. */
	private static final int USER_AREA_SIZE = 0x28;
	private static final String DRY_RUN = "dry-run";
	private static final String KERNEL_PREFIX = "forth_";
	private static final int LINK_A6 = 0x4e56;
	/** Where SmartClockSetup.java keeps the conventions, and the tag it gives one not C. */
	private static final String CONVENTION_MAP = "SmartClock.convention";
	private static final String NOT_C_TAG = "not-c";
	private static final String CONVENTION =
		"{\"convention\":\"pforth\",\"confidence\":\"assumed\",\"family\":\"pforth\"}";
	/** How a comment says its function is not C. */
	private static final String NOT_C_MARK = "Not a C function";
	private static final String NOT_C = NOT_C_MARK + ": a routine of the pForth console's "
		+ "kernel, which runs with the data stack in A6, the end of the user area in A5, the "
		+ "dictionary pointer in A4 and the input in A3, and passes anything else in other "
		+ "registers (docs/firmware/console.md, \"The kernel's registers\").";

	private static final DataType CHAR_POINTER = new PointerDataType(CharDataType.dataType);
	private static final DataType VOID_POINTER = new PointerDataType(VoidDataType.dataType);
	private static final DataType LONG_POINTER = new PointerDataType(LongDataType.dataType);

	/** The user area, from A5 - 0x28; console.md gives the word that shows each. */
	private static final Field[] FIELDS = {
		new Field(0x00, "iodev", ShortDataType.dataType),
		new Field(0x06, "pad", CHAR_POINTER),
		new Field(0x0a, "tib", CHAR_POINTER),
		new Field(0x0e, "fence", VOID_POINTER),
		new Field(0x12, "latest", VOID_POINTER),
		new Field(0x16, "here", VOID_POINTER),
		new Field(0x1a, "base", LongDataType.dataType),
		new Field(0x1e, "state", ShortDataType.dataType),
		new Field(0x20, "stack_base", LONG_POINTER),
		new Field(0x24, "return_stack_base", VOID_POINTER),
	};

	@Override
	protected void run() throws Exception {
		boolean dryRun = List.of(getScriptArgs()).contains(DRY_RUN);
		DataType userPointer = userPointer();
		List<KernelRegister> registers = List.of(
			new KernelRegister("A5", "user", userPointer),
			new KernelRegister("A6", "sp", LONG_POINTER),
			new KernelRegister("A4", "dp", VOID_POINTER),
			new KernelRegister("A3", "input", CHAR_POINTER));
		StringPropertyMap conventions =
			currentProgram.getUsrPropertyManager().getStringPropertyMap(CONVENTION_MAP);
		if (conventions == null) {
			conventions = currentProgram.getUsrPropertyManager().createStringPropertyMap(CONVENTION_MAP);
		}
		AddressSet kernel = kernelSpan();
		println("kernel span " + kernel.getMinAddress() + " - " + kernel.getMaxAddress());
		int changed = 0;
		for (Function function : currentProgram.getFunctionManager().getFunctions(kernel, true)) {
			if ((function.getParameterCount() > 0 && !function.hasCustomVariableStorage())
					|| usesFrame(function)) {
				continue;
			}
			String comment = function.getComment();
			if (!dryRun && comment == null) {
				function.setComment(NOT_C);
			}
			else if (!dryRun && !comment.contains(NOT_C_MARK)) {
				function.setComment(comment + "\n" + NOT_C);
			}
			if (!dryRun && conventions.getString(function.getEntryPoint()) == null) {
				conventions.add(function.getEntryPoint(), CONVENTION);
				function.addTag(NOT_C_TAG);
			}
			Map<String, KernelRegister> used = usedRegisters(function, registers);
			for (Parameter existing : function.getParameters()) {
				Register register = existing.getRegister();
				if (register != null) {
					used.remove(register.getName());
				}
			}
			if (used.isEmpty()) {
				continue;
			}
			changed++;
			if (dryRun) {
				println("would give " + function.getName() + " " + used.keySet());
				continue;
			}
			List<Parameter> parameters = new ArrayList<>(List.of(function.getParameters()));
			for (KernelRegister register : used.values()) {
				Register machine = currentProgram.getRegister(register.register());
				parameters.add(new ParameterImpl(register.name(), register.type(),
					new VariableStorage(currentProgram, machine), currentProgram,
					SourceType.ANALYSIS));
			}
			function.updateFunction(null, function.getReturn(), parameters,
				FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.ANALYSIS);
		}
		println((dryRun ? "would change " : "changed ") + changed + " functions");
	}

	/** The user area structure and the pointer to its end that A5 holds. */
	private DataType userPointer() throws Exception {
		DataTypeManager types = currentProgram.getDataTypeManager();
		StructureDataType area = new StructureDataType(CATEGORY, USER_AREA, USER_AREA_SIZE, types);
		for (Field field : FIELDS) {
			area.replaceAtOffset(field.offset(), field.type(), field.type().getLength(),
				field.name(), null);
		}
		area.replaceAtOffset(0x02, Undefined4DataType.dataType, 4, null, "not known");
		DataType resolved = types.addDataType(area, DataTypeConflictHandler.REPLACE_HANDLER);
		DataType existing = types.getDataType(CATEGORY, USER_POINTER);
		if (existing != null) {
			return existing;
		}
		DataType pointer = types.addDataType(new PointerTypedefBuilder(resolved, 4, types)
				.componentOffset(USER_AREA_SIZE).build(), DataTypeConflictHandler.REPLACE_HANDLER);
		// The builder's name is not kept: Ghidra names a pointer typedef
		// after its settings until it is renamed.
		pointer.setName(USER_POINTER);
		return pointer;
	}

	/** From the first forth_ function to the end of the last. */
	private AddressSet kernelSpan() {
		Address first = null;
		Address last = null;
		for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
			if (!function.getName().startsWith(KERNEL_PREFIX)) {
				continue;
			}
			if (first == null) {
				first = function.getEntryPoint();
			}
			last = function.getBody().getMaxAddress();
		}
		return new AddressSet(first, last);
	}

	/** True if the function sets up a frame with link.w A6. */
	private boolean usesFrame(Function function) throws Exception {
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(function.getBody(), true)) {
			if ((getShort(instruction.getAddress()) & 0xffff) == LINK_A6
					|| instruction.getMnemonicString().equals("unlk")) {
				return true;
			}
		}
		return false;
	}

	/** The kernel registers the function's instructions name, in the order given. */
	private Map<String, KernelRegister> usedRegisters(Function function,
			List<KernelRegister> registers) {
		Map<String, KernelRegister> used = new LinkedHashMap<>();
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(function.getBody(), true)) {
			String text = instruction.toString();
			for (KernelRegister register : registers) {
				if (text.matches(".*\\b" + register.register() + "\\b.*")) {
					used.put(register.register(), register);
				}
			}
		}
		Map<String, KernelRegister> ordered = new LinkedHashMap<>();
		for (KernelRegister register : registers) {
			if (used.containsKey(register.register())) {
				ordered.put(register.register(), register);
			}
		}
		return ordered;
	}
}
