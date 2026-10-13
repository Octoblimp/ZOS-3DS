// Decompile functions by name, for reading code outside the game's classes
// (cocos2d-x's constructors, the STL helpers) with the project's types.
//
// ZOS_NAMES is a comma-separated list. Each entry is a qualified name
// (cocos2d::Node::Node), a mangled symbol (_ZN7cocos2d4NodeC2Ev), or a Ghidra
// address (0019a7d4). A qualified name matches every overload. The C goes to
// ZOS_OUT.
//
// Usage: python tools/ghidra.py decompile <name> ...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Symbol;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class DecompileByName extends GhidraScript {

    @Override
    public void run() throws Exception {
        String out = System.getenv("ZOS_OUT");
        Set<String> wanted = new LinkedHashSet<>(Arrays.asList(System.getenv("ZOS_NAMES").split(",")));
        Set<Function> found = new LinkedHashSet<>();
        for (String w : wanted) {
            List<Function> hits = new ArrayList<>();
            if (w.matches("[0-9a-fA-F]{8}")) {
                Address a = toAddr(Long.parseLong(w, 16));
                Function f = getFunctionContaining(a);
                if (f != null) hits.add(f);
            } else if (w.startsWith("_Z")) {
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(w)) {
                    Function f = getFunctionAt(s.getAddress());
                    if (f != null) hits.add(f);
                }
            }
            if (hits.isEmpty()) {
                FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getName(true).equals(w)) hits.add(f);
                }
            }
            if (hits.isEmpty()) println("not found: " + w);
            for (Function f : hits) {
                if (f.isThunk() && f.getThunkedFunction(true) != null) f = f.getThunkedFunction(true);
                found.add(f);
            }
        }
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(out, StandardCharsets.UTF_8)) {
            for (Function f : found) {
                w.println("// ==== " + f.getEntryPoint() + "  " + f.getName(true) + "  (" + f.getBody().getNumAddresses() + " bytes)");
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted()) {
                    w.println(res.getDecompiledFunction().getC());
                } else {
                    w.println("// decompile failed: " + (res != null ? res.getErrorMessage() : "null result"));
                }
            }
        }
        decomp.dispose();
    }
}
