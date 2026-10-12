import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.io.File;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.HashSet;

public class GhidraDump3 extends GhidraScript {
    public void run() throws Exception {
        String[] targets = {
            "CSGPolyShape", "CSGSpatial", "CSGObject", "CSGGroup", "CSGCamera", "CSGCurve",
            "CSGMeta", "CStreamReader", "CStreamWriter", "CSGCollision", "CSGProjector",
            "CSceneGraphContext", "ParseMetadata", "CIndexBuffer", "CVertexBuffer", "CSerializable",
            "CAnimation", "CSimpleAnim", "SG::FindMatrix"
        };
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp3");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        HashMap<Long, String> byAddr = new HashMap<Long, String>();
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
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
            long key = a.getOffset();
            String prev = byAddr.get(key);
            if (prev == null || (prev.startsWith("_Z") && !name.startsWith("_Z"))) {
                byAddr.put(key, name);
            }
        }
        println("unique target addresses: " + byAddr.size());
        int ok = 0;
        HashSet<String> written = new HashSet<String>();
        for (Long key : byAddr.keySet()) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(key);
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
                    String base = byAddr.get(key).replaceAll("[^A-Za-z0-9_.-]", "_");
                    if (base.startsWith("_Z")) {
                        base = "m_" + base;
                    }
                    if (base.length() > 100) {
                        base = base.substring(0, 100);
                    }
                    if (written.contains(base)) {
                        base = base + "_" + Long.toHexString(key);
                    }
                    written.add(base);
                    PrintWriter pw = new PrintWriter(new File(out, base + ".c"));
                    pw.print(res.getDecompiledFunction().getC());
                    pw.close();
                    ok++;
                }
            } catch (Exception e) {
                println("fail " + byAddr.get(key) + ": " + e);
            }
        }
        println("GhidraDump3 done: ok=" + ok);
    }
}
