import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class GhidraProbe3 extends GhidraScript {
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Address tbl = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress("0x002fc540");
        println("TABLE at 0x2fc540:");
        for (int i = 0; i < 8; i++) {
            int v = mem.getInt(tbl.add(i * 4));
            println("  [" + i + "] = " + v + " (0x" + Integer.toHexString(v) + ")");
        }
        Address tbl2 = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress("0x001fc540");
        println("TABLE at 0x1fc540:");
        for (int i = 0; i < 8; i++) {
            int v = mem.getInt(tbl2.add(i * 4));
            println("  [" + i + "] = " + v + " (0x" + Integer.toHexString(v) + ")");
        }
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        SymbolIterator it = currentProgram.getSymbolTable().getSymbolIterator("*CalcFormatSize*", true);
        while (it.hasNext()) {
            Symbol s = it.next();
            println("SYM " + s.getName(true) + " @ " + s.getAddress());
            Function f = getFunctionAt(s.getAddress());
            if (f == null) {
                f = createFunction(s.getAddress(), null);
            }
            if (f != null) {
                DecompileResults res = decomp.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    println("---- DECOMP " + s.getName(true) + " ----");
                    println(res.getDecompiledFunction().getC());
                }
            }
        }
    }
}
