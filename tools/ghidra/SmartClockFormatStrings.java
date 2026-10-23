// Types the arguments of every call to a printf-like function from its
// format string, as call-site signature overrides: Ghidra's own
// "Variadic Function Signature Override" analyzer, run on the whole
// program.  It takes a function whose name contains "printf" and that
// has a char * parameter and varargs, so printf, sprintf, debug_printf,
// debug_sprintf and printf_polled.  Without it a variadic argument has no
// type, so the decompiler prints an address passed to %s as a number:
// printf("Copyright Notice: %s", 0x101059) rather than
// gps.receiver_id.copyright.
//
//@category SmartClock

import ghidra.app.plugin.core.string.variadic.FormatStringAnalyzer;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.importer.MessageLog;

public class SmartClockFormatStrings extends GhidraScript {

	@Override
	protected void run() throws Exception {
		MessageLog log = new MessageLog();
		new FormatStringAnalyzer().added(currentProgram, currentProgram.getMemory(), monitor, log);
		println("format string overrides applied" + (log.hasMessages() ? ": " + log : ""));
	}
}
