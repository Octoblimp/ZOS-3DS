// Facts about every game method, as the evidence for decomp/signatures.txt.
//
// The mangled names give the parameters but not the return type, and not
// whether a method is static: `Simulation::update(Rect const&, float)` mangles
// the same either way. Ghidra's demangler guesses, and guesses static for
// about a third of the game's methods, so their `this` turns into the first
// parameter and every parameter after it is off by one register.
//
// So each method's prototype is unlocked (no parameters, no return type) and
// decompiled again, and the decompiler's own parameter recovery says which
// registers and stack slots the code reads on entry and whether it leaves a
// result in r0 (or r0:r1). tools/signatures.py compares that with the
// mangled parameters.
//
// Nothing is saved: run read-only (python tools/ghidra.py functions).
//
// Writes decomp/layouts/_functions.tsv: address, class, name, symbols,
// calling convention now, parameters now, inputs read, result left.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.pcode.FunctionPrototype;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighParam;
import ghidra.program.model.pcode.HighSymbol;
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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class DumpFunctions extends GhidraScript {

    private final Map<String, Boolean> classes = new LinkedHashMap<>();

    private String classOf(Function f) {
        Namespace ns = f.getParentNamespace();
        if (ns == null || ns.isGlobal()) return null;
        String name = ns.getName(true);
        return classes.containsKey(name) ? name : null;
    }

    // r0, r0+r1 for a pair, s16 for a stack slot at 16.
    private static String where(VariableStorage vs) {
        if (vs.isStackStorage()) return "s" + vs.getStackOffset();
        StringBuilder sb = new StringBuilder();
        for (Varnode v : vs.getVarnodes()) {
            if (sb.length() > 0) sb.append('+');
            if (v.isRegister() && vs.getRegisters() != null) {
                sb.append(v.getAddress().toString().replace("register:", ""));
            }
        }
        if (vs.getRegisters() != null) {
            sb.setLength(0);
            for (Register r : vs.getRegisters()) {
                if (sb.length() > 0) sb.append('+');
                sb.append(r.getName());
            }
        }
        return sb.length() > 0 ? sb.toString() : vs.toString();
    }

    @Override
    public void run() throws Exception {
        Path outDir = Paths.get(System.getenv("ZOS_DECOMP"));
        try (BufferedReader in = Files.newBufferedReader(outDir.resolve("classes.txt"), StandardCharsets.UTF_8)) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] cols = line.split("\t");
                classes.put(cols[0], cols.length > 3 && cols[3].equals("dropped"));
            }
        }

        List<Function> fs = new ArrayList<>();
        List<String> before = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isThunk() || f.isExternal() || classOf(f) == null) continue;
            fs.add(f);
            StringBuilder ps = new StringBuilder();
            for (Parameter p : f.getParameters()) {
                if (ps.length() > 0) ps.append(", ");
                ps.append(p.getDataType().getDisplayName()).append(' ').append(p.getName());
            }
            before.add(f.getCallingConventionName() + "\t" + f.getReturnType().getDisplayName() + "\t" + ps);
        }
        // Unlock them all, so the decompiler recovers each from its code.
        for (Function f : fs) {
            f.setCustomVariableStorage(false);
            f.replaceParameters(new ArrayList<>(), FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.DEFAULT);
            f.setReturnType(DataType.DEFAULT, SourceType.DEFAULT);
            f.setCallingConvention("__softfp");
            f.setSignatureSource(SourceType.DEFAULT);
        }

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.toggleCCode(false);
        decomp.toggleSyntaxTree(true);
        decomp.setSimplificationStyle("decompile");
        decomp.openProgram(currentProgram);

        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(outDir.resolve("layouts").resolve("_functions.tsv"), StandardCharsets.UTF_8))) {
            out.println("address\tclass\tname\tsymbols\tcc\treturn\tparams\tinputs\tresult");
            for (int i = 0; i < fs.size(); i++) {
                monitor.checkCancelled();
                Function f = fs.get(i);
                StringBuilder syms = new StringBuilder();
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(f.getEntryPoint())) {
                    if (!s.getName().startsWith("_Z")) continue;
                    if (syms.length() > 0) syms.append(' ');
                    syms.append(s.getName());
                }
                String inputs = "?", result = "?";
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                HighFunction hf = res == null ? null : res.getHighFunction();
                if (hf != null) {
                    FunctionPrototype proto = hf.getFunctionPrototype();
                    StringBuilder ins = new StringBuilder();
                    for (int k = 0; k < proto.getNumParams(); k++) {
                        HighSymbol p = proto.getParam(k);
                        if (ins.length() > 0) ins.append(',');
                        ins.append(where(p.getStorage())).append('/').append(p.getDataType().getDisplayName());
                    }
                    inputs = ins.toString();
                    DataType rt = proto.getReturnType();
                    result = rt == null || rt.getLength() <= 0 ? "void"
                            : where(proto.getReturnStorage()) + "/" + rt.getDisplayName();
                }
                out.println(f.getEntryPoint() + "\t" + classOf(f) + "\t" + f.getName() + "\t" + syms + "\t" + before.get(i)
                        + "\t" + inputs + "\t" + result);
            }
        }
        decomp.dispose();
        println("Dumped " + fs.size() + " methods.");
    }
}
