// Give every function in the .so the calling convention it was compiled with.
//
// Android's armeabi-v7a is softfp: the code uses VFP instructions, but float
// and double arguments and results travel in r0-r3 and on the stack, as with
// soft float. Ghidra's ARM default (__stdcall) is the hard-float AAPCS-VFP,
// which passes them in s0-s15. With that, every call that passes a float is
// decompiled wrongly: `Rect(0, 0, w, h)` came out as `Rect(fVar19, fVar29,
// ..., extraout_s3)`.
//
// Ghidra's own __stdcall_softfp has the right argument rules but leaves
// d8-d15 out of the registers a call preserves, which AAPCS says it does, so
// a value the caller keeps in s16 across a call would turn into junk; and it
// does not say that small arguments arrive widened, so every bool argument
// reads as CONCAT31(in_register, b). So this adds two prototypes to the
// program as specification extensions:
//   __softfp          r0-r3 then the stack; results in r0 (r0:r1 for 64-bit);
//                     r4-r11, d8-d15 and sp preserved
//   __thiscall_softfp the same with an automatic `this`
// and moves every function onto one of them: __thiscall methods onto
// __thiscall_softfp, the rest onto __softfp.
//
// Saves into the project: run without -readOnly. Safe to run again.
import ghidra.app.script.GhidraScript;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class UseSoftFloatABI extends GhidraScript {

    private static String prototype(String name, boolean hasThis) {
        StringBuilder sb = new StringBuilder();
        sb.append("<prototype name=\"").append(name).append("\" extrapop=\"0\" stackshift=\"0\"");
        if (hasThis) sb.append(" hasthis=\"true\"");
        sb.append(">\n");
        sb.append("  <input>\n");
        // AAPCS has the caller widen a char, short or bool argument to 32 bits.
        for (int r = 0; r < 4; r++) {
            sb.append("    <pentry minsize=\"1\" maxsize=\"4\" extension=\"inttype\"><register name=\"r").append(r).append("\"/></pentry>\n");
        }
        sb.append("    <pentry minsize=\"1\" maxsize=\"500\" align=\"4\"><addr offset=\"0\" space=\"stack\"/></pentry>\n");
        sb.append("    <rule><datatype name=\"any\"/><join align=\"true\"/></rule>\n");
        sb.append("  </input>\n");
        sb.append("  <output>\n");
        sb.append("    <pentry minsize=\"1\" maxsize=\"4\" extension=\"inttype\"><register name=\"r0\"/></pentry>\n");
        sb.append("    <pentry minsize=\"1\" maxsize=\"4\" extension=\"inttype\"><register name=\"r1\"/></pentry>\n");
        sb.append("    <rule><datatype name=\"struct\" minsize=\"5\"/><hidden_return/></rule>\n");
        sb.append("    <rule><datatype name=\"union\" minsize=\"5\"/><hidden_return/></rule>\n");
        sb.append("    <rule><datatype name=\"any\"/><join/></rule>\n");
        sb.append("  </output>\n");
        sb.append("  <unaffected>\n");
        for (int r = 4; r <= 11; r++) sb.append("    <register name=\"r").append(r).append("\"/>\n");
        for (int d = 8; d <= 15; d++) sb.append("    <register name=\"d").append(d).append("\"/>\n");
        sb.append("    <register name=\"sp\"/>\n");
        sb.append("  </unaffected>\n");
        sb.append("  <killedbycall>\n");
        for (String r : new String[] { "r0", "r1", "r2", "r3", "r12" }) {
            sb.append("    <register name=\"").append(r).append("\"/>\n");
        }
        sb.append("  </killedbycall>\n");
        sb.append("</prototype>\n");
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        SpecExtension ext = new SpecExtension(currentProgram);
        ext.addReplaceCompilerSpecExtension(prototype("__softfp", false), monitor);
        ext.addReplaceCompilerSpecExtension(prototype("__thiscall_softfp", true), monitor);

        int methods = 0, others = 0, custom = 0, failed = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        FunctionIterator ext2 = currentProgram.getFunctionManager().getExternalFunctions();
        for (FunctionIterator fi : new FunctionIterator[] { it, ext2 }) {
            while (fi.hasNext()) {
                Function f = fi.next();
                if (f.isThunk()) continue;   // a thunk takes its target's signature
                String cc = f.getCallingConventionName();
                String want = cc.equals("__thiscall") || cc.equals("__thiscall_softfp") ? "__thiscall_softfp" : "__softfp";
                if (want.equals(cc)) continue;
                try {
                    if (f.hasCustomVariableStorage()) {
                        f.setCustomVariableStorage(false);
                        custom++;
                    }
                    f.setCallingConvention(want);
                    if (want.equals("__thiscall_softfp")) methods++; else others++;
                } catch (Exception e) {
                    failed++;
                    println("failed: " + f.getName(true) + ": " + e.getMessage());
                }
            }
        }
        println("Calling conventions: " + methods + " methods to __thiscall_softfp, " + others
                + " functions to __softfp (" + custom + " had custom storage), " + failed + " failed.");
    }
}
