#!/usr/bin/env node
// Run a compiled harness against a real file.
//
// The harness uses stdio inside the WASM sandbox, which has no host filesystem.
// So we:
//   1. build the module with INVOKE_RUN=0 (main does not run yet),
//   2. mount the data file is directory with NODEFS into /data,
//   3. call main ourselves with "/data/<name>".
//
// Usage: node scripts/run-harness.mjs <harness.cjs> <file-to-expose>
//
// Exit codes: 0 ok; 1 harness failed; 2 usage error

import { pathToFileURL } from "node:url";
import { resolve, dirname, basename } from "node:path";
import { existsSync } from "node:fs";

const [harnessPath, dataPath, extraPath] = process.argv.slice(2);
if (!harnessPath) {
  console.error("usage: node scripts/run-harness.mjs <harness.cjs> [file]");
  process.exit(2);
}

const harness = resolve(harnessPath);
if (!existsSync(harness)) {
  console.error("error: harness not found: " + harness);
  process.exit(2);
}
// The file argument is optional: HarnessRng takes no input.
const data = dataPath ? resolve(dataPath) : null;
if (data && !existsSync(data)) {
  console.error("error: data file not found: " + data);
  process.exit(2);
}
// An optional second file (e.g. the save) is exposed the same way.
const extra = extraPath ? resolve(extraPath) : null;
if (extra && !existsSync(extra)) {
  console.error("error: extra file not found: " + extra);
  process.exit(2);
}

const factory = (await import(pathToFileURL(harness).href)).default;
if (typeof factory !== "function") {
  console.error("error: harness does not export a factory (needs -sMODULARIZE=1)");
  process.exit(2);
}

const module = await factory({
  print: (line) => process.stdout.write(line + "\n"),
  printErr: (line) => process.stderr.write(line + "\n"),
  locateFile: (name) => resolve(dirname(harness), name),
});

// main has not run yet (INVOKE_RUN=0), so the filesystem is still ours to set up.
if (data) {
  try {
    module.FS.mkdir("/data");
  } catch {
    // already exists
  }
  module.FS.mount(module.NODEFS, { root: dirname(data) }, "/data");
}
if (extra) {
  try {
    module.FS.mkdir("/extra");
  } catch {
    // already exists
  }
  module.FS.mount(module.NODEFS, { root: dirname(extra) }, "/extra");
}

// Now run main; harnesses that take no file get an empty argv.
const mounted = data ? "/data/" + basename(data) : null;
const argv = [];
if (mounted) argv.push(mounted);
if (extra) argv.push("/extra/" + basename(extra));
let code = 0;
try {
  code = module.callMain(argv);
} catch (error) {
  if (error && typeof error.status === "number") {
    code = error.status;
  } else {
    console.error("harness threw: " + (error && error.message ? error.message : error));
    code = 1;
  }
}
process.exitCode = code;
