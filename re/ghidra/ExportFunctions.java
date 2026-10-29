// Ghidra headless post-script: exports the function list of the analyzed program for recomp/x86rec.py.
//
//   analyzeHeadless <projdir> sh4 -import "SILENT HILL 4.exe" -scriptPath re/ghidra \
//       -postScript ExportFunctions.java <out/functions.json> -deleteProject
//
// Output (JSON, one object per line inside an array):
//   {"a":"0x401000","name":"FUN_00401000","thunk":false,"target":null,"insns":[4198400, ...]}
// "insns" are the addresses of the instructions in the function body (used as descent seeds);
// "target" is the directly thunked function address for thunks ("EXTERNAL" for import thunks). External symbols are skipped.
// The output is derived from the game binary: never commit it (see .gitignore: re/out/).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class ExportFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "functions.json");
        out.getParentFile().mkdirs();
        int n = 0;
        try (PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(out), "UTF-8"))) {
            w.println("[");
            boolean first = true;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.isExternal()) continue;
                StringBuilder sb = new StringBuilder();
                sb.append(first ? " " : ",").append("{\"a\":\"0x").append(Long.toHexString(f.getEntryPoint().getOffset())).append("\"");
                sb.append(",\"name\":\"").append(f.getName().replace("\\", "\\\\").replace("\"", "\\\"")).append("\"");
                Function t = f.isThunk() ? f.getThunkedFunction(false) : null;
                boolean ext = t != null && t.isExternal();
                sb.append(",\"thunk\":").append(f.isThunk());
                sb.append(",\"target\":").append(t == null ? "null" : ext ? "\"EXTERNAL\"" : "\"0x" + Long.toHexString(t.getEntryPoint().getOffset()) + "\"");
                sb.append(",\"insns\":[");
                if (!f.isThunk()) {
                    boolean fi = true;
                    for (Instruction i : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                        if (!fi) sb.append(",");
                        sb.append(i.getAddress().getOffset());
                        fi = false;
                    }
                }
                sb.append("]}");
                w.println(sb);
                first = false;
                n++;
            }
            w.println("]");
        }
        println("ExportFunctions: " + n + " functions -> " + out);
    }
}
