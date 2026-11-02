import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.util.*;

public class Decomp extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String labels = args[0], out = args[1];
        BufferedReader br = new BufferedReader(new FileReader(labels));
        String line; List<Address> entries = new ArrayList<>();
        while ((line = br.readLine()) != null) {
            String[] p = line.trim().split(" ");
            if (p[0].equals("TEXTSIZE")) continue;
            Address a = toAddr(Long.parseLong(p[0], 16));
            createLabel(a, p[1], true, SourceType.USER_DEFINED);
            entries.add(a);
        }
        for (Address a : entries) {
            new ArmDisassembleCommand(a, null, false).applyTo(currentProgram, monitor);
            new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
        }
        analyzeAll(currentProgram);
        // second pass: functions for any call targets
        for (Address a : entries) { if (getFunctionAt(a)==null) new CreateFunctionCmd(a).applyTo(currentProgram, monitor); }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(out));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.getName().contains("::")) continue;
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            pw.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            if (r != null && r.getDecompiledFunction() != null) pw.println(r.getDecompiledFunction().getC());
            else pw.println("// decompile failed");
        }
        pw.close();
    }
}
