// Collect every access the game's code makes to a field of one of its own
// classes, as the raw material for the class layouts (decomp/layouts.h).
//
// Each game function (and the unnamed helpers it reaches) is decompiled. Every
// variable the decompiler types as a pointer to a game class is followed
// through its data flow, as Ghidra's "Auto Fill in Structure" does: each load
// and store at (pointer + constant) is a field access, and each (pointer +
// constant) passed to a call is an embedded object, typed by the callee's
// parameter. A store of a constant records the value, so the constructors give
// the defaults.
//
// Also collects the objects' sizes from `operator new(N)` calls whose result
// goes straight into a constructor.
//
// Writes:
//   decomp/layouts/_accesses.tsv  class, offset, kind, size, type, value,
//                                 function, address
//   decomp/layouts/_sizes.tsv     class, size, function
//
// Usage (or python tools/ghidra.py collect): as ExportGameClasses.java, with -postScript
// CollectFieldAccesses.java.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.component.DecompilerUtils;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

public class CollectFieldAccesses extends GhidraScript {

    private final Set<String> classes = new LinkedHashSet<>();
    private final Set<String> rows = new TreeSet<>();
    private final Set<String> sizes = new TreeSet<>();
    private final Set<String> unknownStructs = new TreeSet<>();

    private void readClasses(Path list) throws Exception {
        try (BufferedReader in = Files.newBufferedReader(list, StandardCharsets.UTF_8)) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] cols = line.split("\t");
                if (cols.length > 3 && cols[3].equals("dropped")) continue;
                classes.add(cols[0]);
            }
        }
    }

    private String classOf(Function f) {
        Namespace ns = f.getParentNamespace();
        if (ns == null || ns.isGlobal()) return null;
        String name = ns.getName(true);
        return classes.contains(name) ? name : null;
    }

    // The game class a data type points to, or null. The demangler files a
    // class's structure under /Demangler and a category per enclosing
    // namespace, so Effects::Bomb is the structure "Bomb" in
    // "/Demangler/Effects".
    private String classPointedTo(DataType dt) {
        while (dt instanceof TypeDef) dt = ((TypeDef) dt).getBaseDataType();
        if (!(dt instanceof Pointer)) return null;
        DataType base = ((Pointer) dt).getDataType();
        while (base instanceof TypeDef) base = ((TypeDef) base).getBaseDataType();
        if (base == null || base instanceof Pointer) return null;
        String path = base.getCategoryPath().getPath();
        if (path.startsWith("/Demangler")) path = path.substring("/Demangler".length());
        String name = path.isEmpty() || path.equals("/") ? base.getName()
                : path.substring(1).replace("/", "::") + "::" + base.getName();
        if (classes.contains(name) || EXTRA.contains(name)) return name;
        // A type nested in a game class (Simulation::Stats,
        // EffectsRenderer::BlastRenderInfo) has no methods of its own, so it
        // is not in classes.txt, but it is the game's.
        if (name.contains("::") && classes.contains(name.substring(0, name.indexOf("::")))) return name;
        return null;
    }

    // The game's types outside any class that have no methods: the listener
    // interfaces and plain structs.
    private static final Set<String> EXTRA = Set.of("ActionSelectedListener", "ControlSelectedListener",
            "EverythingSelectedListener", "SettingsSelectedListener", "AnimationIndexImage", "PersonMeasure");

    private static String typeName(DataType dt) {
        return dt == null ? "?" : dt.getDisplayName().replace('\t', ' ');
    }

    private static long signed(Varnode v) {
        long x = v.getOffset();
        int bits = v.getSize() * 8;
        if (bits < 64 && (x & (1L << (bits - 1))) != 0) x -= 1L << bits;
        return x;
    }

    private void row(String cls, long off, String kind, int size, String type, String value, Function f, PcodeOp op) {
        rows.add(cls + "\t" + String.format("0x%04x", off) + "\t" + kind + "\t" + size + "\t" + type + "\t"
                + value + "\t" + f.getName(true) + "\t" + op.getSeqnum().getTarget());
    }

    private Function callee(PcodeOp op) {
        if (op.getOpcode() != PcodeOp.CALL) return null;
        Function f = getFunctionAt(op.getInput(0).getAddress());
        if (f != null && f.isThunk()) {
            Function t = f.getThunkedFunction(true);
            if (t != null) f = t;
        }
        return f;
    }

    // Follow one pointer-to-class variable through the data flow.
    private void walk(HighVariable hv, String cls, Function f) {
        Deque<Object[]> todo = new ArrayDeque<>();
        Set<Varnode> done = new HashSet<>();
        for (Varnode vn : hv.getInstances()) {
            if (done.add(vn)) todo.add(new Object[] { vn, 0L });
        }
        while (!todo.isEmpty()) {
            Object[] cur = todo.pop();
            Varnode vn = (Varnode) cur[0];
            long off = (Long) cur[1];
            Iterator<PcodeOp> it = vn.getDescendants();
            while (it.hasNext()) {
                PcodeOp op = it.next();
                Varnode out = op.getOutput();
                Varnode[] in = op.getInputs();
                long next;
                switch (op.getOpcode()) {
                    case PcodeOp.INT_ADD:
                    case PcodeOp.INT_SUB:
                        if (!in[1].isConstant()) break;
                        next = off + (op.getOpcode() == PcodeOp.INT_ADD ? signed(in[1]) : -signed(in[1]));
                        if (next >= 0 && next < 0x10000 && out != null && done.add(out)) todo.add(new Object[] { out, next });
                        break;
                    case PcodeOp.PTRADD:
                        if (!in[1].isConstant() || !in[2].isConstant()) break;
                        next = off + signed(in[1]) * in[2].getOffset();
                        if (next >= 0 && next < 0x10000 && out != null && done.add(out)) todo.add(new Object[] { out, next });
                        break;
                    case PcodeOp.PTRSUB:
                        if (!in[1].isConstant()) break;
                        next = off + signed(in[1]);
                        if (next >= 0 && next < 0x10000 && out != null && done.add(out)) todo.add(new Object[] { out, next });
                        break;
                    case PcodeOp.CAST:
                    case PcodeOp.COPY:
                    case PcodeOp.MULTIEQUAL:
                        if (out != null && done.add(out)) todo.add(new Object[] { out, off });
                        break;
                    case PcodeOp.LOAD:
                        row(cls, off, "load", out.getSize(), typeName(DecompilerUtils.getDataTypeTraceForward(out)), "", f, op);
                        break;
                    case PcodeOp.STORE: {
                        if (op.getSlot(vn) != 1) {
                            // The pointer itself is stored somewhere.
                            break;
                        }
                        Varnode v = in[2];
                        String value = v.isConstant() ? String.format("0x%x", v.getOffset()) : "";
                        row(cls, off, "store", v.getSize(), typeName(DecompilerUtils.getDataTypeTraceBackward(v)), value, f, op);
                        break;
                    }
                    case PcodeOp.CALL: {
                        Function c = callee(op);
                        int slot = op.getSlot(vn);
                        String cname = c == null ? "?" : c.getName(true);
                        String ptype = "?";
                        if (c != null && slot >= 1 && slot - 1 < c.getParameterCount()) {
                            Parameter p = c.getParameter(slot - 1);
                            if (p != null) ptype = typeName(p.getDataType());
                        }
                        // Calls on the object itself (offset 0, as this) are
                        // only interesting when they reach another class's
                        // method: a base class or an embedded first member.
                        if (off == 0 && slot == 1 && c != null && cls.equals(classOf(c))) break;
                        row(cls, off, "arg" + (slot - 1), 0, ptype, cname, f, op);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    // operator new(N) whose result is passed as `this` to a constructor.
    private void scanNews(HighFunction hf, Function f) {
        Iterator<PcodeOpAST> ops = hf.getPcodeOps();
        while (ops.hasNext()) {
            PcodeOp op = ops.next();
            Function c = callee(op);
            if (c == null || !c.getName().equals("operator_new") && !c.getName().equals("operator.new")
                    && !c.getName(true).contains("operator_new") && !c.getName(true).contains("operator new")) continue;
            if (op.getNumInputs() < 2 || !op.getInput(1).isConstant() || op.getOutput() == null) continue;
            long n = op.getInput(1).getOffset();
            Deque<Varnode> todo = new ArrayDeque<>();
            Set<Varnode> seen = new HashSet<>();
            todo.add(op.getOutput());
            while (!todo.isEmpty()) {
                Varnode v = todo.pop();
                if (!seen.add(v)) continue;
                Iterator<PcodeOp> it = v.getDescendants();
                while (it.hasNext()) {
                    PcodeOp d = it.next();
                    if (d.getOpcode() == PcodeOp.CAST || d.getOpcode() == PcodeOp.COPY) {
                        if (d.getOutput() != null) todo.add(d.getOutput());
                        continue;
                    }
                    Function ctor = callee(d);
                    if (ctor == null || d.getSlot(v) != 1) continue;
                    Namespace ns = ctor.getParentNamespace();
                    if (ns != null && !ns.isGlobal() && ctor.getName().equals(ns.getName())) {
                        sizes.add(ns.getName(true) + "\t" + String.format("0x%x", n) + "\t" + f.getName(true));
                    }
                }
            }
        }
    }

    @Override
    public void run() throws Exception {
        String outEnv = System.getenv("ZOS_DECOMP");
        if (outEnv == null) throw new IllegalArgumentException("set ZOS_DECOMP to the repo's decomp/ folder");
        Path outDir = Paths.get(outEnv);
        readClasses(outDir.resolve("classes.txt"));

        // The game's methods, then the unnamed helpers they reach.
        List<Function> todo = new ArrayList<>();
        Set<Function> seen = new HashSet<>();
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) {
            Function f = fit.next();
            if (f.isThunk() || f.isExternal() || classOf(f) == null) continue;
            if (seen.add(f)) todo.add(f);
        }
        for (int i = 0; i < todo.size(); i++) {
            for (Function c : todo.get(i).getCalledFunctions(monitor)) {
                if (c.isThunk() || c.isExternal()) continue;
                if (c.getSymbol().getSource() == SourceType.DEFAULT && seen.add(c)) todo.add(c);
            }
        }

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.toggleCCode(false);
        decomp.toggleSyntaxTree(true);
        decomp.setSimplificationStyle("decompile");
        decomp.openProgram(currentProgram);

        int n = 0;
        for (Function f : todo) {
            monitor.checkCancelled();
            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            HighFunction hf = res == null ? null : res.getHighFunction();
            if (hf == null) continue;
            Set<HighVariable> walked = new HashSet<>();
            Iterator<PcodeOpAST> ops = hf.getPcodeOps();
            while (ops.hasNext()) {
                PcodeOp op = ops.next();
                List<Varnode> vns = new ArrayList<>();
                if (op.getOutput() != null) vns.add(op.getOutput());
                for (Varnode v : op.getInputs()) vns.add(v);
                for (Varnode v : vns) {
                    HighVariable hv = v.getHigh();
                    if (hv == null || walked.contains(hv)) continue;
                    walked.add(hv);
                    DataType dt = hv.getDataType();
                    String cls = classPointedTo(dt);
                    if (cls != null) {
                        walk(hv, cls, f);
                    } else if (dt instanceof Pointer && ((Pointer) dt).getDataType() != null) {
                        DataType b = ((Pointer) dt).getDataType();
                        String p = b.getCategoryPath().getPath().replaceFirst("^/Demangler", "");
                        if (b instanceof Structure && !p.startsWith("/std") && !p.startsWith("/cocos2d") && !p.isEmpty() && !p.equals("/"))
                            unknownStructs.add(p + " :: " + b.getName());
                    }
                }
            }
            scanNews(hf, f);
            n++;
        }
        decomp.dispose();

        Path dir = outDir.resolve("layouts");
        Files.createDirectories(dir);
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(dir.resolve("_accesses.tsv"), StandardCharsets.UTF_8))) {
            out.println("class\toffset\tkind\tsize\ttype\tvalue\tfunction\taddress");
            for (String r : rows) out.println(r);
        }
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(dir.resolve("_sizes.tsv"), StandardCharsets.UTF_8))) {
            out.println("class\tsize\tfunction");
            for (String r : sizes) out.println(r);
        }
        println("Decompiled " + n + " functions: " + rows.size() + " accesses, " + sizes.size() + " allocations.");
        for (String u : unknownStructs) println("other pointer type: " + u);
    }
}
