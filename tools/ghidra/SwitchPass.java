// Runs Ghidra's decompiler switch analysis one function at a time, in address order, to a fixed point.
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import ghidra.app.plugin.core.analysis.DecompilerSwitchAnalyzer;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.importer.MessageLog;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.FlowType;

public class SwitchPass extends GhidraScript {
	@Override
	protected void run() throws Exception {
		DecompilerSwitchAnalyzer analyzer = new DecompilerSwitchAnalyzer();
		analyzer.optionsChanged(currentProgram.getOptions("Analyzers").getOptions("Decompiler Switch Analysis"), currentProgram);
		Set<Address> done = new HashSet<>();
		int rounds = 0;
		while (true) {
			List<Address> pending = new ArrayList<>();
			for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
				FlowType flow = instruction.getFlowType();
				if (flow.isJump() && flow.isComputed() && !done.contains(instruction.getAddress())) {
					pending.add(instruction.getAddress());
				}
			}
			if (pending.isEmpty()) {
				break;
			}
			rounds++;
			for (Address jump : pending) {
				done.add(jump);
				if (getInstructionAt(jump) == null) {
					continue;
				}
				analyzer.added(currentProgram, new ghidra.program.model.address.AddressSet(jump), monitor, new MessageLog());
				analyzeChanges(currentProgram);
			}
		}
		println("switch pass rounds: " + rounds + ", jumps: " + done.size());
	}
}
