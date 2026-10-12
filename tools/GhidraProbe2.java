import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class GhidraProbe2 extends GhidraScript {
    public void run() throws Exception {
        String[] names = { "PVFS", "ReadFixed", "Hash" };
        for (String n : names) {
            SymbolIterator it = currentProgram.getSymbolTable().getSymbolIterator("*" + n + "*", true);
            int c = 0;
            while (it.hasNext() && c < 12) {
                Symbol s = it.next();
                println("SYM " + n + " : " + s.getName(true) + " @ " + s.getAddress());
                c++;
            }
        }
        String[] addrs = { "0x00228630", "0x000d9850", "0x0010a620", "0x000d91f0" };
        for (String hex : addrs) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
            Function f = getFunctionContaining(a);
            println("AT " + hex + " -> " + (f == null ? "none" : f.getName(true) + " entry=" + f.getEntryPoint()));
        }
    }
}
