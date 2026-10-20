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
// With the argument "all", a function the decompiler accepts is
// repaired the same way when its parameters are misplaced: when the C
// picks bytes out of one (param_N._0_1_), a char or short read where
// Ghidra put a 4-byte argument.  Only a signature Ghidra made up is
// replaced; one from the names file or given by hand is kept.  With
// "dry-run" as well, the functions are counted, not changed.
//
//@category SmartClock

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.regex.Pattern;

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
	private static final String ALL = "all";
	private static final String DRY_RUN = "dry-run";
	/** A parameter the C picks bytes out of, as param_2._0_1_. */
	private static final Pattern PARTIAL_PARAMETER = Pattern.compile("\\bparam_\\d+\\._\\d+_\\d+_");

	@Override
	protected void run() throws Exception {
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);
		List<String> args = List.of(getScriptArgs());
		boolean all = args.contains(ALL);
		boolean dryRun = args.contains(DRY_RUN);
		int repaired = 0;
		int misplaced = 0;
		List<Function> remaining = new ArrayList<>();
		try {
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				boolean emptied = function.hasCustomVariableStorage()
						&& function.getParameterCount() == 0;
				DecompileResults results = decompile(decompiler, function);
				boolean failed = !results.decompileCompleted()
						&& results.getErrorMessage().contains(FAILURE);
				boolean partial = all && results.decompileCompleted()
						&& function.getSignatureSource() == SourceType.DEFAULT
						&& !function.hasCustomVariableStorage()
						&& PARTIAL_PARAMETER.matcher(results.getDecompiledFunction().getC()).find();
				if (!emptied && !failed && !partial) {
					continue;
				}
				if (partial) {
					misplaced++;
					if (dryRun) {
						continue;
					}
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
		if (all) {
			println((dryRun ? "misplaced parameters found: " : "misplaced parameters repaired: ")
				+ misplaced);
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
