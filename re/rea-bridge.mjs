// Keeps an REA MCP (stdio) session open and exposes it over local HTTP.
// POST /call {"name": "<tool>", "arguments": {...}} -> tools/call result
// GET  /tools -> tools/list
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { appendFileSync } from "node:fs";

const PORT = 47800;
const REA = process.env.REA_MJS ?? `${process.env.APPDATA}\\npm\\node_modules\\rea-agents\\scripts\\rea.mjs`;
const LOG = new URL("./out/bridge.log", import.meta.url);

const child = spawn(process.execPath, [REA, "--mcp"], {
  env: {
    ...process.env,
    GHIDRA_INSTALL_DIR: "J:\\tools\\ghidra_12.1.4_PUBLIC",
    JAVA_HOME: "J:\\tools\\jdk-21.0.12.1+1",
    GHIDRA_HEADLESS_MAXMEM: "8G",
    NO_COLOR: "1",
  },
  stdio: ["pipe", "pipe", "pipe"],
});
child.stderr.on("data", (d) => appendFileSync(LOG, d));
child.on("exit", (c) => { appendFileSync(LOG, `rea exited ${c}\n`); process.exit(1); });

let nextId = 1, buf = "";
const pending = new Map();
child.stdout.on("data", (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf("\n")) >= 0) {
    const line = buf.slice(0, i).trim();
    buf = buf.slice(i + 1);
    if (!line) continue;
    const msg = JSON.parse(line);
    const p = pending.get(msg.id);
    if (p) { pending.delete(msg.id); p(msg); }
  }
});

const rpc = (method, params) => new Promise((resolve) => {
  const id = nextId++;
  pending.set(id, resolve);
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
});

const init = await rpc("initialize", {
  protocolVersion: "2025-06-18",
  capabilities: {},
  clientInfo: { name: "sh4vita-bridge", version: "1" },
});
child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
appendFileSync(LOG, `initialized: ${JSON.stringify(init.result?.serverInfo)}\n`);

const server = createServer(async (req, res) => {
  let body = "";
  for await (const c of req) body += c;
  const out = req.url === "/tools"
    ? await rpc("tools/list", {})
    : await rpc("tools/call", JSON.parse(body));
  res.writeHead(200, { "content-type": "application/json" });
  res.end(JSON.stringify(out));
});
server.requestTimeout = 0;
server.listen(PORT, "127.0.0.1", () => console.log(`rea-bridge ready on 127.0.0.1:${PORT}`));
