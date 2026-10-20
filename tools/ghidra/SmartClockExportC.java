// Writes every function's decompiled C, in address order, to the file
// named by the script's first argument.
//
//@category SmartClock

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class SmartClockExportC extends GhidraScript {

	private static final int TIMEOUT_SECONDS = 120;

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
}
