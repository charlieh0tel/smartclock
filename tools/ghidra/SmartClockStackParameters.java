// Repairs functions the decompiler rejects with "Cannot properly adjust
// input varnodes".  This compiler pushes a char or short argument in a
// 2-byte slot, so arguments sit at 2-byte-aligned stack offsets, while
// Ghidra's 68000 compiler spec assumes 4-byte slots and infers inputs
// that overlap.  Each failing function is first given no parameters, in
// custom storage, which the decompiler accepts; the stack inputs it then
// reports (in_stack_*) become the parameters, at their real offsets and
// with the types it inferred.  A function left with no parameters in
// custom storage, by an earlier run, is inferred again.
//
//@category SmartClock

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.symbol.SourceType;

public class SmartClockStackParameters extends GhidraScript {

	private static final int TIMEOUT_SECONDS = 60;
	/**
	 * Stack offsets are from SP at entry, where the return address occupies
	 * 0..3; arguments start at 4.
	 */
	private static final int FIRST_ARGUMENT_OFFSET = 4;
	private static final String FAILURE = "Cannot properly adjust input varnodes";

	@Override
	protected void run() throws Exception {
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);
		int repaired = 0;
		List<Function> remaining = new ArrayList<>();
		try {
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				boolean emptied = function.hasCustomVariableStorage()
						&& function.getParameterCount() == 0;
				DecompileResults results = decompile(decompiler, function);
				if (!emptied && (results.decompileCompleted()
						|| !results.getErrorMessage().contains(FAILURE))) {
					continue;
				}
				setParameters(function, List.of());
				results = decompile(decompiler, function);
				if (!results.decompileCompleted()) {
					remaining.add(function);
					continue;
				}
				List<Parameter> parameters = stackInputs(results.getHighFunction());
				setParameters(function, parameters);
				if (!decompile(decompiler, function).decompileCompleted()) {
					println(function.getEntryPoint() + ": inferred parameters rejected; none kept");
					setParameters(function, List.of());
				}
				else {
					println(function.getEntryPoint() + ": " + parameters.size()
						+ " stack parameters");
				}
				repaired++;
			}
		}
		finally {
			decompiler.dispose();
		}
		println("repaired " + repaired + ", still failing " + remaining.size());
		for (Function function : remaining) {
			println("  " + function.getEntryPoint() + " " + function.getName());
		}
	}

	private DecompileResults decompile(DecompInterface decompiler, Function function) {
		decompiler.flushCache();
		return decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
	}

	private static void setParameters(Function function, List<Parameter> parameters)
			throws Exception {
		function.updateFunction(null, null, parameters,
			FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.ANALYSIS);
	}

	/**
	 * Returns the decompiler's stack inputs at argument offsets, lowest
	 * offset first, as parameters.  An input overlapping the one before it
	 * is dropped.
	 */
	private List<Parameter> stackInputs(HighFunction high) throws Exception {
		List<HighSymbol> inputs = new ArrayList<>();
		Iterator<HighSymbol> symbols = high.getLocalSymbolMap().getSymbols();
		while (symbols.hasNext()) {
			HighSymbol symbol = symbols.next();
			VariableStorage storage = symbol.getStorage();
			if (storage.isStackStorage() && storage.getStackOffset() >= FIRST_ARGUMENT_OFFSET) {
				inputs.add(symbol);
			}
		}
		inputs.sort(Comparator.comparingInt(s -> s.getStorage().getStackOffset()));
		List<Parameter> parameters = new ArrayList<>();
		int end = FIRST_ARGUMENT_OFFSET;
		for (HighSymbol input : inputs) {
			int offset = input.getStorage().getStackOffset();
			if (offset < end) {
				continue;
			}
			parameters.add(new ParameterImpl("param_" + (parameters.size() + 1),
				input.getDataType(), input.getStorage(), currentProgram, SourceType.ANALYSIS));
			end = offset + input.getSize();
		}
		return parameters;
	}
}
