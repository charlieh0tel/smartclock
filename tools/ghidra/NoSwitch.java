// Turns off Decompiler Switch Analysis before auto-analysis.
import ghidra.app.script.GhidraScript;
public class NoSwitch extends GhidraScript {
	@Override
	protected void run() throws Exception {
		setAnalysisOption(currentProgram, "Decompiler Switch Analysis", "false");
	}
}
