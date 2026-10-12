import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.io.File;
import java.io.PrintWriter;

public class GhidraDump6 extends GhidraScript {
    public void run() throws Exception {
        String[] targets = { "Shader13GetShaderName", "Shader14GetShaderCount", "Shader9GetShader",
                             "Shader6Register", "Shader4Init", "Shader::" };
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp6");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        int ok = 0;
        while (it.hasNext()) {
            Symbol s = it.next();
            String name = s.getName(true);
            boolean hit = false;
            for (String t : targets) {
                if (name.contains(t)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
            Address a = s.getAddress();
            if (a == null || !a.isMemoryAddress()) {
                continue;
            }
            Function f = getFunctionAt(a);
            if (f == null) {
                try {
                    f = createFunction(a, null);
                } catch (Exception e) {
                    f = null;
                }
            }
            if (f == null) {
                continue;
            }
            try {
                DecompileResults res = decomp.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    String base = name.replaceAll("[^A-Za-z0-9_.-]", "_");
                    if (base.startsWith("_Z")) {
                        base = "m_" + base;
                    }
                    PrintWriter pw = new PrintWriter(new File(out, base + ".c"));
                    pw.print(res.getDecompiledFunction().getC());
                    pw.close();
                    ok++;
                }
            } catch (Exception e) {
            }
        }
        println("GhidraDump6 done: ok=" + ok);
    }
}
