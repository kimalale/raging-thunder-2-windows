import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;

public class GhidraDumpAddr extends GhidraScript {
    public void run() throws Exception {
        long[] targets = { 0x110790L };
        String[] names = { "CSimpleAnim_Update_shared" };
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp7");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (int i = 0; i < targets.length; i++) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(targets[i]);
            Function f = getFunctionAt(a);
            if (f == null) {
                try {
                    f = createFunction(a, null);
                } catch (Exception e) {
                    f = null;
                }
            }
            if (f == null) {
                println("no function at " + Long.toHexString(targets[i]));
                continue;
            }
            DecompileResults res = decomp.decompileFunction(f, 90, monitor);
            if (res != null && res.decompileCompleted()) {
                PrintWriter pw = new PrintWriter(new File(out, names[i] + ".c"));
                pw.print(res.getDecompiledFunction().getC());
                pw.close();
                println("dumped " + names[i] + " at " + Long.toHexString(targets[i]));
            } else {
                println("decompile failed at " + Long.toHexString(targets[i]));
            }
        }
    }
}
