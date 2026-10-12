import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;

public class GhidraDump2 extends GhidraScript {
    public void run() throws Exception {
        File listFile = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\dump_targets_focus.txt");
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp2");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        BufferedReader br = new BufferedReader(new FileReader(listFile));
        String line;
        int ok = 0;
        int miss = 0;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) {
                continue;
            }
            String[] parts = line.split("\\s+");
            String hex = parts[0];
            String sym = parts.length > 1 ? parts[1] : ("fun_" + hex);
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
            Function f = getFunctionAt(a);
            if (f == null) {
                try {
                    f = createFunction(a, null);
                } catch (Exception e) {
                    f = null;
                }
            }
            if (f == null) {
                miss++;
                continue;
            }
            try {
                DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                if (res != null && res.decompileCompleted()) {
                    String base = sym.replaceAll("[^A-Za-z0-9_.-]", "_");
                    if (base.length() > 90) {
                        base = base.substring(0, 90);
                    }
                    File dest = new File(out, base + ".c");
                    PrintWriter pw = new PrintWriter(dest);
                    pw.print(res.getDecompiledFunction().getC());
                    pw.close();
                    ok++;
                }
            } catch (Exception e) {
                println("fail " + sym + ": " + e);
            }
        }
        br.close();
        println("GhidraDump2 done: ok=" + ok + " missing=" + miss);
    }
}
