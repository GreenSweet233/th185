#!/usr/bin/env node
// TH185 C++ compile check.
//
// WHY THIS EXISTS
// ----------------
// th185_web/cpp/game holds the reconstructed game managers, but until now none
// of it had ever been compiled — correctness was only inferred through Python
// mirrors. This script closes that gap: it runs the real Emscripten toolchain
// over the C++ sources so syntax, type and linkage errors surface immediately.
//
// It deliberately does NOT link a runnable game yet (there is no platform layer
// in this repo). It compiles every translation unit to an object file and then
// links them into a static archive, which is enough to prove the code is valid
// C++ and that the headers agree with each other.
//
// TOOLCHAIN DISCOVERY
// -------------------
// The workspace does not have tools/emsdk (that is the th11 layout). The
// emsdk that exists lives under th11-high-refresh/tools/emsdk, and its
// .emscripten file points NODE_JS at th10_web/tools/node.exe, which is absent.
// So this script generates its own .emscripten config with a NODE_JS that
// actually exists, instead of depending on the broken one.
//
// Usage:
//   node th185_web/scripts/build-cpp.mjs [--verbose] [--clean]
//
// Exit codes: 0 success; 1 compile/link failure; 2 toolchain missing

import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { resolve, relative, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = fileURLToPath(new URL(".", import.meta.url));
const root = resolve(here, "..");             // th185_web
const repo = resolve(root, "..");             // th185
const workspace = resolve(repo, "..");        // thvip  <- toolchain lives here
const verbose = process.argv.includes("--verbose");
const clean = process.argv.includes("--clean");
const harness = process.argv.includes("--harness");
const appBuild = process.argv.includes("--app");

const outDir = resolve(root, "artifacts", "cpp-check");
const objDir = resolve(outDir, "objects");

// --- toolchain discovery -------------------------------------------------

const SDK_CANDIDATES = [
  resolve(workspace, "tools", "emsdk"),
  resolve(workspace, "th11-high-refresh", "tools", "emsdk"),
  resolve(workspace, "th11", "tools", "emsdk"),
];

const sdk = SDK_CANDIDATES.find((p) => existsSync(join(p, "install", "emscripten", "emcc.py")));
if (!sdk) {
  console.error("error: emsdk not found. Looked in:");
  for (const p of SDK_CANDIDATES) console.error("  " + p);
  process.exit(2);
}

const emcc = join(sdk, "install", "emscripten", "emcc.py");

// The bundled .emscripten may reference a node.exe that does not exist here,
// so write our own config pointing at a node that does.
const nodeExe = process.execPath;
const configPath = join(outDir, ".emscripten");

function writeConfig() {
  mkdirSync(outDir, { recursive: true });
  const body = [
    "import os",
    `_sdk = ${JSON.stringify(sdk.replaceAll("\\", "/"))}` ,
    `LLVM_ROOT = os.path.join(_sdk, "install", "bin")`,
    `BINARYEN_ROOT = os.path.join(_sdk, "install")`,
    `NODE_JS = [${JSON.stringify(nodeExe.replaceAll("\\", "/"))}]`,
    `CACHE = os.path.join(_sdk, "install", "emscripten", "cache")`,
    `EMSCRIPTEN_ROOT = os.path.join(_sdk, "install", "emscripten")`,
    "",
  ].join("\n");
  writeFileSync(configPath, body, "utf8");
}

const env = {
  ...process.env,
  EMSDK: sdk,
  EM_CONFIG: configPath,
  EM_CACHE: join(sdk, "install", "emscripten", "cache"),
  EMSDK_NODE: nodeExe,
  PYTHONIOENCODING: "utf-8",
};

// --- sources -------------------------------------------------------------

function collect(dir, extension) {
  const full = resolve(root, dir);
  if (!existsSync(full)) return [];
  const out = [];
  for (const entry of readdirSync(full, { withFileTypes: true })) {
    const path = join(full, entry.name);
    if (entry.isDirectory()) out.push(...collect(relative(root, path), extension));
    else if (entry.name.endsWith(extension)) out.push(path);
  }
  return out;
}

// HarnessRng.cpp has a main() and must not go into the library, so the main
// build skips any file starting with "Harness".
const sources = collect("cpp/game", ".cpp")
  .filter((p) => !p.includes("Harness"))
  .sort();
if (sources.length === 0) {
  console.error("error: no .cpp files found under cpp/game");
  process.exit(2);
}

// Flags mirror th11 is build-sdl.mjs so the two codebases stay comparable.
const common = [
  "-O1",
  "-g0",
  "-std=c++17",
  "-ffp-contract=off",
  "-fno-strict-aliasing",
  "-fno-exceptions",
  "-fno-rtti",
  "-Wall",
  "-Wextra",
  "-Wno-unused-parameter",
  `-I${resolve(root, "cpp", "game")}`,
];

// --- runner --------------------------------------------------------------

function run(args, label) {
  return new Promise((resolveRun, reject) => {
    if (verbose) console.log("  $ emcc " + args.join(" "));
    const child = spawn(process.env.PYTHON || "python", [emcc, ...args], {
      cwd: root,
      env,
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let log = "";
    for (const stream of [child.stdout, child.stderr]) {
      stream.on("data", (chunk) => { log += chunk; });
    }
    child.on("error", reject);
    child.on("exit", (code) => {
      if (code) {
        console.error(`\n[FAIL] ${label}`);
        console.error(log.trim() || "(no output)");
        reject(new Error(`${label} exited ${code}`));
      } else {
        if (log.trim() && verbose) console.log(log.trim());
        resolveRun();
      }
    });
  });
}

// --- main ----------------------------------------------------------------

async function main() {
  console.log("=== TH185 C++ compile check ===");
  console.log();
  console.log("emsdk   : " + sdk);
  console.log("node    : " + nodeExe);
  console.log("sources : " + sources.length);
  console.log();

  if (clean && existsSync(outDir)) {
    rmSync(outDir, { recursive: true, force: true });
  }
  mkdirSync(objDir, { recursive: true });
  writeConfig();

  const objects = [];
  let index = 0;
  for (const source of sources) {
    index += 1;
    const name = relative(root, source).replaceAll("\\", "_").replaceAll("/", "_");
    const object = join(objDir, name + ".o");
    const label = `[${index}/${sources.length}] ${relative(root, source)}`;
    process.stdout.write(label + " ... ");
    await run([...common, "-c", source, "-o", object], label);
    console.log("ok");
    objects.push(object);
  }

  // Partial-link the objects. Emscripten's `ar` is not on PATH (it ships as
  // emar), so use emcc -r, which also proves the translation units link
  // together: duplicate symbols, missing definitions and header/impl
  // mismatches all surface here rather than in the archive step.
  console.log();
  const archive = join(outDir, "libth185game.a");
  process.stdout.write("linking " + relative(root, archive) + " ... ");
  await run([...common, "-r", ...objects, "-o", archive], "partial link");
  console.log("ok");

  // Record what was built so CI can show it.
  const report = {
    toolchain: { emsdk: sdk, node: nodeExe },
    flags: common,
    sources: sources.map((p) => relative(root, p).replaceAll("\\", "/")),
    archive: relative(root, archive).replaceAll("\\", "/"),
  };
  writeFileSync(join(outDir, "build.json"), JSON.stringify(report, null, 2) + "\n", "utf8");

  // Optional: build each Harness*.cpp into a runnable Node script. These let us
  // diff the REAL compiled C++ against the Unicorn probe and against the Python
  // reference parsers, instead of only checking that the code compiles.
  if (harness) {
    console.log();
    // The harness links against the library objects, so it only needs its own
    // translation unit plus the archive.
    const harnesses = [
      { source: "HarnessRng.cpp", output: "harness-rng.cjs" },
      {
        source: "HarnessGame.cpp",
        output: "harness-game.cjs",
      },
    ];
    for (const item of harnesses) {
      const source = resolve(root, "cpp", "game", item.source);
      if (!existsSync(source)) continue;
      const target = join(outDir, item.output);
      process.stdout.write("building " + item.source + " -> " +
        relative(root, target) + " ... ");
      await run([
        ...common,
        source,
        archive,
        "-o", target,
        "-sEXIT_RUNTIME=1",
        // INVOKE_RUN=0: the runner mounts NODEFS and then calls main itself,
        // because the sandbox has no host filesystem at startup.
        "-sINVOKE_RUN=0",
        "-sEXPORTED_RUNTIME_METHODS=FS,NODEFS,callMain,HEAPU8",

        "-sENVIRONMENT=node",
        // MODULARIZE=1 so the runner can mount NODEFS in preRun before main.
        "-sMODULARIZE=1",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sSTACK_SIZE=5242880",
        // Linking against C++ objects pulls in operator new/delete; emcc needs
        // to be told to use the C++ driver for the link step or those stay
        // undefined.
        "-sDEFAULT_TO_CXX=1",
        "-sEXPORT_ES6=0",
        // The harness reads th185.dat with stdio. Inside the WASM sandbox there
        // is no host filesystem, so expose NODEFS and let the runner mount the
        // real directory before calling main.
        "-lnodefs.js",
        "-sFORCE_FILESYSTEM=1",
      ], "harness link " + item.source);
      console.log("ok");
    }
  }

  // Optional: build the browser application module. This is the artifact the
  // web shell (sdl-runtime/th185.html -> /app/shell.mjs) loads. It exports the
  // th185_* C functions declared in cpp/sdl/Exports.cpp.
  if (appBuild) {
    console.log();
    const appSources = collect("cpp/sdl", ".cpp");
    if (appSources.length === 0) {
      console.log("skipping app build: no sources under cpp/sdl");
    } else {
      const target = join(outDir, "th185-app.mjs");
      process.stdout.write("building app module -> " +
        relative(root, target) + " ... ");
      await run([
        ...common,
        ...appSources,
        archive,
        "-o", target,
        "-sMODULARIZE=1",
        "-sEXPORT_ES6=1",
        // web+node so the smoke probe can load the same artifact.
        "-sENVIRONMENT=web,node",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sSTACK_SIZE=5242880",
        "-sDEFAULT_TO_CXX=1",
        "-sFORCE_FILESYSTEM=1",
        "-sEXPORTED_RUNTIME_METHODS=FS,cwrap,HEAPU8",
        // Every th185_* function is EMSCRIPTEN_KEEPALIVE, but exporting them
        // explicitly keeps the JS surface auditable.
        "-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\",\"_th185_initialize\"," +
          "\"_th185_start\",\"_th185_tick\",\"_th185_pause\",\"_th185_resume\",\"_th185_restart\"," +
          "\"_th185_load_stage\",\"_th185_phase\",\"_th185_frame\",\"_th185_ticks\"," +
          "\"_th185_ecl_instructions\",\"_th185_ecl_faults\"," +
          "\"_th185_max_stack_depth\",\"_th185_runnable_subroutines\"," +
          "\"_th185_frame_width\",\"_th185_frame_height\",\"_th185_frame_pixels\",\"_th185_frame_bytes\"," +
          "\"_th185_render_quads\",\"_th185_live_animations\"," +
          "\"_th185_animation_bank\",\"_th185_stage_index\"," +
          "\"_th185_player_x\",\"_th185_player_y\",\"_th185_player_focused\"," +
          "\"_th185_player_shots\",\"_th185_player_bombs\"," +
          "\"_th185_bullets_alive\",\"_th185_bullets_fired\"," +
          "\"_th185_error\"]",
      ], "app link");
      console.log("ok");
    }
  }

  console.log();
  console.log(`compiled ${sources.length} translation unit(s), archive written`);
  console.log("build report: " + relative(root, join(outDir, "build.json")).replaceAll("\\", "/"));
  return 0;
}

try {
  process.exitCode = await main();
} catch (error) {
  console.error();
  console.error("build failed: " + error.message);
  process.exitCode = 1;
}
