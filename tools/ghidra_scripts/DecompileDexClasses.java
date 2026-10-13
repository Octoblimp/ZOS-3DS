// Decompiles every method of the Java classes named in ZOS_CLASSES (comma
// separated, matched against the end of the class's namespace path, such as
// "Cocos2dxSound") in an imported classes.dex, into ZOS_OUT.
//
// The game's Java bridge (classes.dex) says how the Android side behaved:
// Cocos2dxSound's SoundPool, the music player. Run on a dex imported into a
// scratch project (see tools/ghidra.py's `dex` command).
//@category ZOS

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;

public class DecompileDexClasses extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] wanted = System.getenv("ZOS_CLASSES").split(",");
        File out = new File(System.getenv("ZOS_OUT"));
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        List<Function> picked = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            Namespace ns = f.getParentNamespace();
            String path = ns == null ? "" : ns.getName(true);
            for (String w : wanted) {
                if (path.endsWith(w) || path.contains(w + "::") || path.contains(w + "$")) {
                    picked.add(f);
                    break;
                }
            }
        }
        try (PrintWriter pw = new PrintWriter(out, "UTF-8")) {
            for (Function f : picked) {
                pw.println("// ==== " + f.getEntryPoint() + "  " + f.getName(true));
                DecompileResults r = decomp.decompileFunction(f, 120, monitor);
                if (r != null && r.decompileCompleted())
                    pw.println(r.getDecompiledFunction().getC());
                else
                    pw.println("// failed: " + (r == null ? "null" : r.getErrorMessage()));
                pw.println();
            }
        }
        println("decompiled " + picked.size() + " functions into " + out);
    }
}
