import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class GhidraProbe extends GhidraScript {
    public void run() throws Exception {
        String[] probes = {
            "0x00114450", "0x0011a500", "0x00103d50", "0x000ffe40", "0x0010c9a0",
            "0x000e9150", "0x00114400", "0x0011a480", "0x0011a4e0"
        };
        for (String hex : probes) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
            Function f = getFunctionAt(a);
            Function c = getFunctionContaining(a);
            String fn = f == null ? "null" : (f.getName() + " entry=" + f.getEntryPoint());
            String cn = c == null ? "null" : (c.getName() + " entry=" + c.getEntryPoint());
            println("PROBE " + hex + " at=" + fn + " containing=" + cn + " defined=" + currentProgram.getMemory().contains(a));
        }
    }
}
