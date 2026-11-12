#!/usr/bin/env node
// Serve the TH185 web shell locally.
//
// The HTML expects these paths:
//
//   /                      -> sdl-runtime/th185.html
//   /app/style.css         -> sdl-runtime/style.css
//   /app/shell.mjs         -> sdl-runtime/shell.mjs
//   /th185-app.mjs         -> artifacts/cpp-check/th185-app.mjs
//   /th185-app.wasm        -> artifacts/cpp-check/th185-app.wasm
//   /th185.dat             -> the user's own copy (never committed)
//
// Usage: node th185_web/scripts/serve-shell.mjs [--port 8170] [--data <path>]

import { createServer } from 'node:http';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { resolve, dirname, extname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');

const args = process.argv.slice(2);
function option(name, fallback) {
  const at = args.indexOf(name);
  return at >= 0 && at + 1 < args.length ? args[at + 1] : fallback;
}
const port = Number(option('--port', '8170'));
const dataPath = option('--data', null);

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.dat': 'application/octet-stream',
  '.png': 'image/png',
  '.json': 'application/json',
};

function route(url) {
  const path = url.split('?')[0];
  if (path === '/' || path === '/index.html') {
    return resolve(root, 'sdl-runtime', 'th185.html');
  }
  if (path.startsWith('/app/')) {
    return resolve(root, 'sdl-runtime', path.slice('/app/'.length));
  }
  if (path === '/th185-app.mjs' || path === '/th185-app.wasm') {
    return resolve(root, 'artifacts', 'cpp-check', basename(path));
  }
  if (path === '/th185.dat') {
    if (dataPath && existsSync(dataPath)) return resolve(dataPath);
    return null;
  }
  return null;
}

const server = createServer((request, response) => {
  const file = route(request.url || '/');
  if (!file || !existsSync(file) || !statSync(file).isFile()) {
    response.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
    response.end('not found: ' + (request.url || '/'));
    return;
  }
  const body = readFileSync(file);
  response.writeHead(200, {
    'content-type': TYPES[extname(file)] || 'application/octet-stream',
    'content-length': body.length,
    // The shell uses ES modules and SharedArrayBuffer-free wasm; no COOP/COEP
    // needed, but caching must be off so a rebuild is picked up on reload.
    'cache-control': 'no-store',
  });
  response.end(body);
});

server.listen(port, '127.0.0.1', () => {
  console.log('serving http://127.0.0.1:' + port + '/');
  if (!dataPath) {
    console.log('note: no --data given, /th185.dat will 404');
  }
});
