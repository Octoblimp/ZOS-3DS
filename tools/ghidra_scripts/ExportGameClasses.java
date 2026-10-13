// Decompile the game's own classes out of libZombieOutbreakSimulator.so, one
// file per class, into decomp/.
//
// The .so keeps its dynamic symbol table, so Ghidra's demangler puts every
// game method in its class's namespace (Simulation::updatePolice and so on).
// decomp/classes.txt (tools/list_game_classes.py) names the classes. Each
// class's file holds its methods in address order, then the unnamed helpers
// only its methods reach (static functions the symbol table leaves out).
//
// Beside the classes it writes:
//   decomp/_index.tsv           every exported function: address, size,
//                               class, name, signature, symbols at the entry
//   decomp/_external_calls.tsv  every function outside the game that the game
//                               calls directly, with the callers: the API
//                               surface the cocos2d-x shim has to cover
//                               (virtual calls are not here; see
//                               tools/decomp_calls.py for those)
//
// A function the decompiler cannot finish is written as its disassembly.
// The C goes through tidy(), which only drops noise (see there).
//
// Run UseSoftFloatABI.java on the project first: without it every float
// argument and result is decompiled wrongly.
//
// Usage (or python tools/ghidra.py export):
//   ZOS_DECOMP=<repo>/decomp  analyzeHeadless.bat <GhidraProjects>
//     ZombieOutbreakSim -process libZombieOutbreakSimulator.so -noanalysis
//     -scriptPath <repo>/tools/ghidra_scripts -postScript ExportGameClasses.java
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.io.BufferedReader;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.ArrayDeque;
import java.util.Comparator;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;

public class ExportGameClasses extends GhidraScript {

    private final Map<String, Boolean> classes = new LinkedHashMap<>();   // name -> dropped
    private final Map<String, List<Function>> members = new TreeMap<>();
    private final Map<String, List<Function>> helpers = new TreeMap<>();
    private final Map<Function, String> owner = new HashMap<>();
    private final Map<String, Set<String>> external = new TreeMap<>();

    private String classOf(Function f) {
        Namespace ns = f.getParentNamespace();
        if (ns == null || ns.isGlobal()) return null;
        String name = ns.getName(true);
        return classes.containsKey(name) ? name : null;
    }

    private Function resolve(Function f) {
        if (f != null && f.isThunk()) {
            Function t = f.getThunkedFunction(true);
            if (t != null) return t;
        }
        return f;
    }

    private boolean isUnnamed(Function f) {
        return f.getSymbol().getSource() == SourceType.DEFAULT && !f.isExternal();
    }

    private void readClasses(Path list) throws Exception {
        try (BufferedReader in = Files.newBufferedReader(list, StandardCharsets.UTF_8)) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] cols = line.split("\t");
                classes.put(cols[0], cols.length > 3 && cols[3].equals("dropped"));
            }
        }
    }

    // Walk from each class's methods into the unnamed functions they call,
    // giving each helper to the first class (in address order) that reaches it.
    private void collectHelpers() {
        for (Map.Entry<String, List<Function>> e : members.entrySet()) {
            String cls = e.getKey();
            Deque<Function> work = new ArrayDeque<>(e.getValue());
            while (!work.isEmpty()) {
                Function f = work.pop();
                for (Function c : f.getCalledFunctions(monitor)) {
                    Function callee = resolve(c);
                    if (callee == null || owner.containsKey(callee)) continue;
                    if (classOf(callee) != null) continue;
                    if (isUnnamed(callee)) {
                        owner.put(callee, cls);
                        helpers.computeIfAbsent(cls, k -> new ArrayList<>()).add(callee);
                        work.push(callee);
                    } else {
                        external.computeIfAbsent(callee.getName(true), k -> new TreeSet<>())
                                .add(f.getName(true));
                    }
                }
            }
        }
        for (List<Function> l : helpers.values()) l.sort(Comparator.comparing(Function::getEntryPoint));
    }

    private String symbolsAt(Address a) {
        StringBuilder sb = new StringBuilder();
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) {
            if (sb.length() > 0) sb.append(' ');
            sb.append(s.getName(true));
        }
        return sb.toString();
    }

    // Mechanical clean-ups of Ghidra's C, nothing that needs judgement.
    // vcvt between int and float decompiles as VectorSignedToFloat(x, mode)
    // with the FPSCR's rounding mode as a second argument; the game never
    // changes the mode (round to nearest), so the argument is dropped.
    private static final Pattern ROUNDING_MODE = Pattern.compile(",\\s*\\(byte\\)\\(\\w+ >> 0x16\\) & 3\\s*\\)");

    private static String tidy(String c) {
        return ROUNDING_MODE.matcher(c).replaceAll(")");
    }

    private void writeFunction(PrintWriter out, DecompInterface decomp, Function f, String kind) {
        long size = f.getBody().getNumAddresses();
        out.println("// ==== " + f.getEntryPoint() + "  " + f.getName(true) + "  (" + kind + ", " + size + " bytes)");
        out.println("// symbols: " + symbolsAt(f.getEntryPoint()));
        DecompileResults res = decomp.decompileFunction(f, 120, monitor);
        if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
            out.println(tidy(res.getDecompiledFunction().getC()));
        } else {
            out.println("// decompile failed: " + (res != null ? res.getErrorMessage() : "null result"));
            out.println("/*");
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                out.println(ins.getAddress() + "\t" + ins);
            }
            out.println("*/");
        }
        out.println();
    }

    @Override
    public void run() throws Exception {
        String outEnv = System.getenv("ZOS_DECOMP");
        if (outEnv == null) throw new IllegalArgumentException("set ZOS_DECOMP to the repo's decomp/ folder");
        Path outDir = Paths.get(outEnv);
        readClasses(outDir.resolve("classes.txt"));

        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) {
            Function f = fit.next();
            if (f.isThunk() || f.isExternal()) continue;
            String cls = classOf(f);
            if (cls == null) continue;
            members.computeIfAbsent(cls, k -> new ArrayList<>()).add(f);
            owner.put(f, cls);
        }
        collectHelpers();

        DecompileOptions opts = new DecompileOptions();
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(opts);
        decomp.toggleCCode(true);
        decomp.toggleSyntaxTree(true);
        decomp.setSimplificationStyle("decompile");
        decomp.openProgram(currentProgram);

        Path classDir = outDir.resolve("classes");
        Files.createDirectories(classDir);
        int done = 0;
        try (PrintWriter index = new PrintWriter(Files.newBufferedWriter(outDir.resolve("_index.tsv"), StandardCharsets.UTF_8))) {
            index.println("address\tsize\tclass\tkind\tname\tsignature\tsymbols");
            for (String cls : classes.keySet()) {
                List<Function> fs = members.getOrDefault(cls, new ArrayList<>());
                List<Function> hs = helpers.getOrDefault(cls, new ArrayList<>());
                if (fs.isEmpty() && hs.isEmpty()) continue;
                String file = cls.replace("::", "__") + ".c";
                try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(classDir.resolve(file), StandardCharsets.UTF_8))) {
                    out.println("// " + cls + (classes.get(cls) ? "  [dropped from the port: Android-only]" : ""));
                    out.println("// Ghidra decompilation of libZombieOutbreakSimulator.so (armeabi-v7a),");
                    out.println("// written by tools/ghidra_scripts/ExportGameClasses.java. Not hand-edited.");
                    out.println("// " + fs.size() + " methods, " + hs.size() + " unnamed helpers.");
                    out.println();
                    for (Function f : fs) {
                        writeFunction(out, decomp, f, "method");
                        index.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + cls + "\tmethod\t"
                                + f.getName(true) + "\t" + f.getSignature().getPrototypeString() + "\t" + symbolsAt(f.getEntryPoint()));
                        done++;
                    }
                    for (Function f : hs) {
                        writeFunction(out, decomp, f, "helper");
                        index.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + cls + "\thelper\t"
                                + f.getName(true) + "\t" + f.getSignature().getPrototypeString() + "\t" + symbolsAt(f.getEntryPoint()));
                        done++;
                    }
                }
                println(cls + ": " + fs.size() + " methods, " + hs.size() + " helpers");
            }
        }
        decomp.dispose();

        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(outDir.resolve("_external_calls.tsv"), StandardCharsets.UTF_8))) {
            out.println("callee\tcallers\tcalled_from");
            for (Map.Entry<String, Set<String>> e : external.entrySet()) {
                out.println(e.getKey() + "\t" + e.getValue().size() + "\t" + String.join(", ", e.getValue()));
            }
        }
        println("Exported " + done + " functions from " + members.size() + " classes; "
                + external.size() + " external callees.");
    }
}
