// Gives functions the stack parameters their instructions read.  This
// compiler pushes a char or short argument in a 2-byte slot, its value at
// the slot's start, and the argument after it 2 bytes on
// (docs/firmware/README.md, "The calling convention"); Ghidra's 68000
// convention puts every argument in a 4-byte slot and a small value at
// its end, so where a function takes a char or short its parameters are
// misplaced, and some do not decompile at all ("Cannot properly adjust
// input varnodes").
//
// The parameters' layout comes from the callers where they agree: a call
// site's pushes, last first, give each argument's slot (move.b and clr.b
// a 2-byte slot holding a byte at its start, .w 2 bytes, .l and pea 4),
// and the call is used only when the stack it pops afterwards adds up.
// Without such a caller, the layout comes from the stack references
// analysis puts on the function's own instructions, relative to SP at
// entry and so the same with a frame or without: each read at offset 4
// or above is an argument, sized by the instruction (.b 1, .w 2, .l 4;
// an address taken, 4), reads that overlap are one argument, and a lone
// byte or word read at the end of a 4-byte slot is taken as part of a
// 4-byte argument, an int the function reads only the low part of.  They go into custom storage at those
// offsets and sizes, typed by size only; the decompiler's types for them
// are then committed.  The decompiler's own list of stack inputs is not
// used: it drops an argument that is only copied into a local buffer
// whose address escapes.
//
// A function is given them if it does not decompile, if its arguments do
// not fit the default convention (one at an offset not a multiple of 4,
// or narrower than 4), if its signature is still the analyzer's, with
// none, while it has arguments -- each caller's decompilation would then
// guess its own from what it pushes, and the C would disagree with the
// function -- or if an earlier run gave it stack parameters.  A
// signature from the names file or given by hand is kept, and so is one
// with register parameters; but a kept stack signature is checked
// against its callers' pushes, and reported where its parameters do not
// match them (a double may span two pushed longs, and pushes past the
// last parameter, a variadic call's or ones the function ignores, are
// not compared).  "dry-run" counts without changing anything.
//
//@category SmartClock

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.Undefined;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.pcode.HighFunctionDBUtil.ReturnCommitOption;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.StackReference;

public class SmartClockStackParameters extends GhidraScript {

	private static final int TIMEOUT_SECONDS = 60;
	/**
	 * Stack offsets are from SP at entry, where the return address occupies
	 * 0..3; arguments start at 4.
	 */
	private static final int FIRST_ARGUMENT_OFFSET = 4;
	/** The default convention's slot, which every argument then fills. */
	private static final int DEFAULT_SLOT = 4;
	/** The size given an argument whose address is taken rather than read. */
	private static final int ADDRESS_TAKEN_SIZE = 4;
	private static final String DRY_RUN = "dry-run";

	@Override
	protected void run() throws Exception {
		boolean dryRun = List.of(getScriptArgs()).contains(DRY_RUN);
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);
		int changed = 0;
		int unchanged = 0;
		List<Function> failing = new ArrayList<>();
		List<Function> disagreeing = new ArrayList<>();
		try {
			for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				if (!eligible(function)) {
					if (keptStackSignature(function) && !agrees(function, callerLayout(function))) {
						disagreeing.add(function);
					}
					continue;
				}
				Map<Integer, Integer> arguments = callerLayout(function);
				if (arguments == null) {
					arguments = stackArguments(function);
				}
				boolean earlier = function.hasCustomVariableStorage();
				boolean misfit = arguments.entrySet().stream()
						.anyMatch(a -> a.getKey() % DEFAULT_SLOT != 0 || a.getValue() < DEFAULT_SLOT);
				// the analyzer's signature has no parameters, and each caller's
				// decompilation then guesses its own from what it pushes
				boolean unset = function.getSignatureSource() == SourceType.DEFAULT
					&& !arguments.isEmpty();
				boolean fails = !earlier && !misfit && !unset
						&& !decompile(decompiler, function).decompileCompleted();
				if (!earlier && !misfit && !unset && !fails) {
					continue;
				}
				if (earlier && matches(function, arguments)) {
					unchanged++;
					continue;
				}
				changed++;
				println(function.getEntryPoint() + " " + function.getName() + ": "
					+ describe(arguments) + (dryRun ? " (dry run)" : ""));
				if (dryRun) {
					continue;
				}
				String convention = function.getCallingConventionName();
				setParameters(function, arguments);
				DecompileResults results = decompile(decompiler, function);
				if (!results.decompileCompleted()) {
					failing.add(function);
					continue;
				}
				HighFunctionDBUtil.commitParamsToDatabase(results.getHighFunction(), true,
					ReturnCommitOption.NO_COMMIT, SourceType.ANALYSIS);
				// the commit gives the decompiler's convention, the default, to
				// one not C, which SmartClockSetup.java leaves unknown
				if (!convention.equals(function.getCallingConventionName())) {
					function.setCallingConvention(convention);
				}
			}
		}
		finally {
			decompiler.dispose();
		}
		println((dryRun ? "would change " : "changed ") + changed + ", unchanged "
			+ unchanged + " (set earlier), still failing " + failing.size());
		for (Function function : failing) {
			println("  " + function.getEntryPoint() + " " + function.getName());
		}
		println("kept signatures that disagree with their callers: " + disagreeing.size());
		for (Function function : disagreeing) {
			println("  " + function.getEntryPoint() + " " + function.getSignature().getPrototypeString()
				+ ": callers push " + describe(callerLayout(function)));
		}
	}

	/**
	 * True unless the function's signature came from the names file or by
	 * hand, or it has register parameters, or it is a thunk.
	 */
	private static boolean eligible(Function function) {
		return own(function) && !kept(function) && stackOnly(function);
	}

	/** True if the function's signature is kept and has only stack parameters. */
	private static boolean keptStackSignature(Function function) {
		return own(function) && kept(function) && stackOnly(function);
	}

	/** True if the function is not a thunk of, or external to, another. */
	private static boolean own(Function function) {
		return !function.isThunk() && !function.isExternal();
	}

	/** True if the signature came from the names file or by hand. */
	private static boolean kept(Function function) {
		SourceType source = function.getSignatureSource();
		return source == SourceType.IMPORTED || source == SourceType.USER_DEFINED;
	}

	private static boolean stackOnly(Function function) {
		for (Parameter parameter : function.getParameters()) {
			if (!parameter.isStackVariable()) {
				return false;
			}
		}
		return true;
	}

	/**
	 * True if each parameter is one pushed argument of its size, or
	 * consecutive pushed longs that together make it up, or if no callers
	 * agree on a layout to compare with.
	 */
	private static boolean agrees(Function function, Map<Integer, Integer> pushed) {
		if (pushed == null) {
			return true;
		}
		for (Parameter parameter : function.getParameters()) {
			int offset = parameter.getStackOffset();
			int covered = 0;
			while (covered < parameter.getLength()) {
				Integer size = pushed.get(offset + covered);
				if (size == null || (covered > 0 && size != DEFAULT_SLOT)) {
					return false;
				}
				covered += size;
			}
			if (covered != parameter.getLength()) {
				return false;
			}
		}
		return true;
	}

	/**
	 * The arguments the function's instructions read, offset to size, from
	 * the stack references on them; overlapping reads are merged.
	 */
	private Map<Integer, Integer> stackArguments(Function function) {
		TreeMap<Integer, Integer> reads = new TreeMap<>();
		for (Instruction instruction : currentProgram.getListing()
				.getInstructions(function.getBody(), true)) {
			for (Reference reference : instruction.getReferencesFrom()) {
				if (!(reference instanceof StackReference stack)) {
					continue;
				}
				int offset = stack.getStackOffset();
				if (offset < FIRST_ARGUMENT_OFFSET || reference.getReferenceType().isWrite()) {
					continue;
				}
				reads.merge(offset, readSize(instruction), Math::max);
			}
		}
		TreeMap<Integer, Integer> merged = new TreeMap<>();
		for (Map.Entry<Integer, Integer> read : reads.entrySet()) {
			int offset = read.getKey();
			int size = read.getValue();
			int slot = offset - (offset - FIRST_ARGUMENT_OFFSET) % DEFAULT_SLOT;
			Map.Entry<Integer, Integer> previous = merged.lastEntry();
			boolean slotEmpty = previous == null
					|| previous.getKey() + previous.getValue() <= slot;
			if (offset + size == slot + DEFAULT_SLOT && offset != slot && slotEmpty) {
				offset = slot;
				size = DEFAULT_SLOT;
			}
			if (previous != null && offset < previous.getKey() + previous.getValue()) {
				int end = Math.max(previous.getKey() + previous.getValue(), offset + size);
				merged.put(previous.getKey(), end - previous.getKey());
			}
			else {
				merged.put(offset, size);
			}
		}
		return merged;
	}

	/**
	 * The argument layout the function's callers push, offset to size, if
	 * every caller whose pushes add up to what it pops agrees, and there is
	 * one; null otherwise.
	 */
	private Map<Integer, Integer> callerLayout(Function function) {
		Map<Integer, Integer> layout = null;
		for (Reference reference : getReferencesTo(function.getEntryPoint())) {
			if (!reference.getReferenceType().isCall()) {
				continue;
			}
			Instruction call = getInstructionAt(reference.getFromAddress());
			Map<Integer, Integer> pushed = pushedArguments(call);
			if (pushed == null) {
				continue;
			}
			if (layout != null && !layout.equals(pushed)) {
				return null;
			}
			layout = pushed;
		}
		return layout;
	}

	/**
	 * The arguments pushed before call, offset to size, or null unless the
	 * stack popped after it is exactly what they occupy.
	 */
	private static Map<Integer, Integer> pushedArguments(Instruction call) {
		if (call == null) {
			return null;
		}
		List<int[]> pushes = new ArrayList<>();
		for (Instruction before = call.getPrevious(); before != null;
				before = before.getPrevious()) {
			int[] push = push(before);
			if (push == null) {
				break;
			}
			pushes.add(push);
		}
		int popped = popped(call.getNext());
		int total = pushes.stream().mapToInt(p -> p[0]).sum();
		if (pushes.isEmpty() || popped != total) {
			return null;
		}
		Map<Integer, Integer> layout = new TreeMap<>();
		int offset = FIRST_ARGUMENT_OFFSET;
		for (int[] push : pushes) {
			layout.put(offset, push[1]);
			offset += push[0];
		}
		return layout;
	}

	/** An instruction's push onto the stack as {slot, value size}, or null. */
	private static int[] push(Instruction instruction) {
		String text = instruction.toString();
		String mnemonic = instruction.getMnemonicString();
		if (mnemonic.equals("pea")) {
			return new int[] { 4, 4 };
		}
		if (!text.endsWith("-(SP)") || !(mnemonic.startsWith("move.") || mnemonic.startsWith("clr."))) {
			return null;
		}
		if (mnemonic.endsWith(".b")) {
			return new int[] { 2, 1 };
		}
		if (mnemonic.endsWith(".w")) {
			return new int[] { 2, 2 };
		}
		return mnemonic.endsWith(".l") ? new int[] { 4, 4 } : null;
	}

	/** The bytes the instruction after a call pops, or 0 if it pops none. */
	private static int popped(Instruction instruction) {
		if (instruction == null) {
			return 0;
		}
		String text = instruction.toString();
		java.util.regex.Matcher addq =
			java.util.regex.Pattern.compile("^addq\\.[wl] #?(0x)?([0-9a-f]+),SP$").matcher(text);
		if (addq.find()) {
			return Integer.parseInt(addq.group(2), addq.group(1) == null ? 10 : 16);
		}
		java.util.regex.Matcher lea =
			java.util.regex.Pattern.compile("^lea \\((0x)?([0-9a-f]+),SP\\),SP$").matcher(text);
		if (lea.find()) {
			return Integer.parseInt(lea.group(2), lea.group(1) == null ? 10 : 16);
		}
		return 0;
	}

	/** The size an instruction reads, from its mnemonic's suffix. */
	private static int readSize(Instruction instruction) {
		String mnemonic = instruction.getMnemonicString();
		if (mnemonic.endsWith(".b")) {
			return 1;
		}
		if (mnemonic.endsWith(".w")) {
			return 2;
		}
		return mnemonic.endsWith(".l") ? 4 : ADDRESS_TAKEN_SIZE;
	}

	/** True if the function's parameters already have these offsets and sizes. */
	private static boolean matches(Function function, Map<Integer, Integer> arguments) {
		Parameter[] parameters = function.getParameters();
		if (parameters.length != arguments.size()) {
			return false;
		}
		int i = 0;
		for (Map.Entry<Integer, Integer> argument : arguments.entrySet()) {
			Parameter parameter = parameters[i++];
			if (parameter.getStackOffset() != argument.getKey()
					|| parameter.getLength() != argument.getValue()) {
				return false;
			}
		}
		return true;
	}

	private static String describe(Map<Integer, Integer> arguments) {
		StringBuilder text = new StringBuilder();
		arguments.forEach((offset, size) -> text.append(text.isEmpty() ? "" : ", ")
				.append(offset).append(':').append(size));
		return text.isEmpty() ? "no arguments" : text.toString();
	}

	private DecompileResults decompile(DecompInterface decompiler, Function function) {
		decompiler.flushCache();
		return decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
	}

	private void setParameters(Function function, Map<Integer, Integer> arguments)
			throws Exception {
		List<Parameter> parameters = new ArrayList<>();
		for (Map.Entry<Integer, Integer> argument : arguments.entrySet()) {
			parameters.add(new ParameterImpl("param_" + (parameters.size() + 1),
				Undefined.getUndefinedDataType(argument.getValue()),
				new VariableStorage(currentProgram, argument.getKey(), argument.getValue()),
				currentProgram, SourceType.ANALYSIS));
		}
		function.updateFunction(null, null, parameters,
			FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.ANALYSIS);
	}
}
