// Maps the installer the boot code unpacks into RAM, as an overlay, so
// that its code disassembles and decompiles.  Run after
// SmartClockSetup.java.
//
// Arguments: the address of the packed records and the installer's entry
// point, as the boot code's unpacker and jump give them; e.g. 0xbbc
// 0x10135c for the Z3801A 3543 and Z3805A 3543B (docs/firmware/
// restart.md, "Forced installer entry with an unusable primary").
//
// The records are an 'S', then records of a 'C', a 32-bit count, a
// 32-bit RAM destination and that many bytes, up to an 'E'.  Before
// jumping to the installer the boot code copies its own vector table, ROM
// 0x0 to 0x3ff, to the start of RAM and points VBR at it; the overlay
// holds that copy too.
//
// Boot code jumps into plain RAM, which an overlay does not serve; the
// jump is marked a call, so the decompiler shows func_0xADDRESS there.
//
// The packed records are never run where they lie, but analysis finds
// code in them, a copy of each installer routine at its ROM address; the
// script clears it, leaving the overlay the installer's only copy.
// The installer's functions are not labeled at their plain addresses: the
// primary keeps its own data at those addresses, and a label there would
// name the primary's data after installer code.
//
//@category SmartClock

import java.util.Arrays;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class SmartClockInstaller extends GhidraScript {

	private static final String OVERLAY = "installer";
	private static final long RAM = 0x100000;
	private static final int VECTOR_TABLE_BYTES = 0x400;
	private static final int VECTOR_COUNT = VECTOR_TABLE_BYTES / 4;
	private static final byte START = 'S';
	private static final byte COPY = 'C';
	private static final byte END = 'E';
	/** Bytes in a record header after its type: count and destination. */
	private static final int RECORD_HEADER_BYTES = 8;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		Address records = toAddr(Long.decode(args[0]));
		long entry = Long.decode(args[1]);
		MemoryBlock existing = currentProgram.getMemory().getBlock(OVERLAY);
		if (existing != null) {
			println("overlay " + OVERLAY + " exists; calls refreshed only");
			removeMirroredLabels(existing);
			callIntoInstaller(existing);
			clearPackedCode(records);
			return;
		}
		byte[] image = unpack(records);
		MemoryBlock block = currentProgram.getMemory().createInitializedBlock(OVERLAY,
			toAddr(RAM), image.length, (byte) 0, monitor, true);
		block.putBytes(block.getStart(), image);
		block.setRead(true);
		block.setWrite(true);
		block.setExecute(true);
		println("overlay " + block.getStart() + " - " + block.getEnd());

		Address start = block.getStart();
		createLabel(start, OVERLAY + "_vectors", true, SourceType.USER_DEFINED);
		makeFunction(start.getNewAddress(entry), OVERLAY + "_entry");
		for (int vector = 1; vector < VECTOR_COUNT; vector++) {
			Address slot = start.add(vector * 4L);
			clearListing(slot, slot.add(3));
			createData(slot, PointerDataType.dataType);
			long target = getInt(slot) & 0xffffffffL;
			if (block.contains(start.getNewAddress(target))) {
				makeFunction(start.getNewAddress(target), null);
			}
		}
		analyzeChanges(currentProgram);
		callIntoInstaller(block);
		clearPackedCode(records);
	}

	/**
	 * Marks jumps from outside the overlay to an installer function's plain
	 * RAM address as calls that return, so the decompiler shows a call
	 * rather than flowing into RAM it has no code for.
	 * The boot code enters the installer by such a jump and never comes
	 * back, so the call is a tail call.
	 */
	private void callIntoInstaller(MemoryBlock block) throws Exception {
		int count = 0;
		for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
			if (block.contains(instruction.getAddress())
					|| !instruction.getFlowType().isJump()
					|| instruction.getFlowType().isConditional()) {
				continue;
			}
			for (Address target : instruction.getFlows()) {
				Address inOverlay = block.getStart().getNewAddress(target.getOffset());
				if (!target.equals(inOverlay) && block.contains(inOverlay)
						&& getFunctionAt(inOverlay) != null) {
					instruction.setFlowOverride(FlowOverride.CALL_RETURN);
					count++;
				}
			}
		}
		println("jumps into the installer made calls: " + count);
	}

	/** Returns the address of the records' 'E'. */
	private Address recordsEnd(Address records) throws Exception {
		if (getByte(records) != START) {
			throw new IllegalArgumentException("no 'S' at " + records);
		}
		Address at = records.add(1);
		while (getByte(at) == COPY) {
			at = at.add(1 + RECORD_HEADER_BYTES + getInt(at.add(1)));
		}
		if (getByte(at) != END) {
			throw new IllegalArgumentException("record at " + at + " is neither 'C' nor 'E'");
		}
		return at;
	}

	/** Returns the installer's RAM image from RAM up: the vector copy, then the records. */
	private byte[] unpack(Address records) throws Exception {
		Address end = recordsEnd(records);
		byte[] image = new byte[0];
		for (Address at = records.add(1); at.compareTo(end) < 0;
				at = at.add(1 + RECORD_HEADER_BYTES + getInt(at.add(1)))) {
			int count = getInt(at.add(1));
			long destination = getInt(at.add(5)) & 0xffffffffL;
			int offset = (int) (destination - RAM);
			if (offset + count > image.length) {
				image = Arrays.copyOf(image, offset + count);
			}
			System.arraycopy(getBytes(at.add(1 + RECORD_HEADER_BYTES), count), 0, image, offset,
				count);
		}
		System.arraycopy(getBytes(toAddr(0), VECTOR_TABLE_BYTES), 0, image, 0,
			VECTOR_TABLE_BYTES);
		return image;
	}

	/** Removes the functions and code analysis found in the packed records. */
	private void clearPackedCode(Address records) throws Exception {
		Address end = recordsEnd(records);
		int count = 0;
		for (Function function : currentProgram.getFunctionManager()
				.getFunctions(new AddressSet(records, end), true)) {
			removeFunction(function);
			count++;
		}
		clearListing(records, end);
		println("functions in the packed records removed: " + count);
	}

	private void makeFunction(Address address, String name) throws Exception {
		disassemble(address);
		Function function = getFunctionAt(address);
		if (function == null) {
			function = createFunction(address, name);
		}
		else if (name != null) {
			function.setName(name, SourceType.USER_DEFINED);
		}
	}

	/**
	 * Removes the labels an earlier version of this script put at each
	 * installer function's plain RAM address, which named the primary's
	 * data there after installer code.
	 */
	private void removeMirroredLabels(MemoryBlock block) throws Exception {
		int count = 0;
		for (Function function : currentProgram.getFunctionManager()
				.getFunctions(new AddressSet(block.getStart(), block.getEnd()), true)) {
			Address plain = toAddr(function.getEntryPoint().getOffset());
			for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(plain)) {
				if (symbol.getName().equals(function.getName())) {
					symbol.delete();
					count++;
				}
			}
		}
		println("mirrored labels removed: " + count);
	}
}
