import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;

public class GhidraDump extends GhidraScript {
    public void run() throws Exception {
        String[] targets = {
            "CSGPolyShape", "CSGSpatial", "CSGObject", "CSGGroup", "CSGCamera", "CSGCurve",
            "CSGMeta", "CStreamReader", "CStreamWriter", "CSGCollision", "CSGProjector",
            "CSceneGraphContext", "ParseMetadata", "CSGTexture", "CSGPrim", "SShaderEnv", "CShader"
        };
        File out = new File("C:\\Users\\Kim Malale\\Desktop\\workdesk\\raging thunder\\work\\decomp");
        out.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            String name = f.getName();
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
            try {
                DecompileResults res = decomp.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    String cname = name.replaceAll("[^A-Za-z0-9_.-]", "_") + ".c";
                    File dest = new File(out, cname);
                    if (dest.exists()) {
                        dest = new File(out, name.replaceAll("[^A-Za-z0-9_.-]", "_") + "_" + f.getEntryPoint() + ".c");
                    }
                    PrintWriter pw = new PrintWriter(dest);
                    pw.print(res.getDecompiledFunction().getC());
                    pw.close();
                    count++;
                }
            } catch (Exception e) {
                println("fail " + name + ": " + e);
            }
        }
        println("GhidraDump done: " + count + " functions");
    }
}
