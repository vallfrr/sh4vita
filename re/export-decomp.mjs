// Exports one view per internal function through the REA bridge.
//   node re/export-decomp.mjs            -> pseudo-code   re/out/decomp/<addr>.c
//   node re/export-decomp.mjs asm        -> instructions  re/out/asm/<addr>.asm
// Each file carries its evidence_id; index.tsv (address, name, evidence, size).
// Resumes where it stopped (existing files are skipped).
import { readFileSync, writeFileSync, existsSync, mkdirSync, appendFileSync } from "node:fs";

const MODES = {
  decomp: { tool: "procedure_pseudo_code", dir: "decomp", ext: "c",
    body: (n) => (typeof n === "string" ? n : JSON.stringify(n, null, 1)).replace(/\r\n/g, "\n") },
  asm: { tool: "read_function_instructions", dir: "asm", ext: "asm",
    body: (n) => n.instructions.join("\n") + "\n" },
};
const mode = MODES[process.argv[2] ?? "decomp"];
const OUT = new URL(`./out/${mode.dir}/`, import.meta.url);
mkdirSync(OUT, { recursive: true });

const call = async (name, args) => {
  const r = await fetch("http://127.0.0.1:47800/call", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ name, arguments: args }),
  }).then((x) => x.json());
  if (r.error) throw new Error(JSON.stringify(r.error));
  return r.result.structuredContent ?? JSON.parse(r.result.content[0].text);
};

const procs = JSON.parse(readFileSync(new URL("./out/procs.json", import.meta.url), "utf8").replace(/^﻿/, ""))
  .normalized_result.filter((p) => !p.procedure.external && !p.procedure.thunk);
console.log(`${procs.length} internal functions -> ${mode.tool}`);

let done = 0, failed = 0;
for (const p of procs) {
  const file = new URL(`${p.address}.${mode.ext}`, OUT);
  if (existsSync(file)) { done++; continue; }
  try {
    const ev = await call(mode.tool, { procedure: p.address });
    const body = mode.body(ev.normalized_result);
    writeFileSync(file, `// ${p.value} @ ${p.address}\n// evidence: ${ev.evidence_id}\n${body}`);
    appendFileSync(new URL("index.tsv", OUT), `${p.address}\t${p.value}\t${ev.evidence_id}\t${body.length}\n`);
  } catch (e) {
    failed++;
    appendFileSync(new URL("errors.log", OUT), `${p.address}\t${e.message.slice(0, 300)}\n`);
  }
  if (++done % 500 === 0) console.log(`${done}/${procs.length} (failures: ${failed})`);
}
console.log(`done: ${done}/${procs.length}, failures: ${failed}`);
