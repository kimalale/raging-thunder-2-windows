import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.File;
import java.io.PrintWriter;
import java.util.HashSet;

public class GhidraConstRefs extends GhidraScript {
    public void run() throws Exception {
        long[] consts = { 0x360f10L, 0x360f40L, 0x360f80L, 0x360f30L };
        String[] names = { "ONE", "INV_PI2", "PI", "ZERO" };
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp7");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        HashSet<Long> done = new HashSet<Long>();
        for (int i = 0; i < consts.length; i++) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(consts[i]);
            println("refs to " + names[i] + " @ " + Long.toHexString(consts[i]) + ":");
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(a);
            int n = 0;
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                println("  from " + from.toString());
                Function f = getFunctionContaining(from);
                if (f == null) {
                    continue;
                }
                long key = f.getEntryPoint().getOffset();
                if (!done.add(key)) {
                    continue;
                }
                DecompileResults res = decomp.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    String base = "init_" + Long.toHexString(key);
                    PrintWriter pw = new PrintWriter(new File(out, base + ".c"));
                    pw.print(res.getDecompiledFunction().getC());
                    pw.close();
                    println("  dumped function " + f.getName() + " @" + Long.toHexString(key));
                }
                n++;
            }
            println("  total refs: " + n);
        }
    }
}
