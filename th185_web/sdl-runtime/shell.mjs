// TH185 web shell.
//
// This is the file sdl-runtime/th185.html loads. It owns the canvas, drives the
// frame loop, and forwards input; all game behaviour lives in the WASM module
// (th185-app.mjs, built by scripts/build-cpp.mjs --app).
//
// ## What it draws
//
// The module renders into a software framebuffer (RGBA8). The shell copies that
// buffer into an ImageData and blits it to the canvas. There is no WebGL yet:
// a 640x480 software blit is fast enough to be usable and keeps the browser
// path identical to the one the tests exercise.
//
// ## Frame pacing
//
// The game logic is a fixed 60 Hz step. `requestAnimationFrame` may fire faster
// or slower; the shell accumulates elapsed time and runs as many fixed steps as
// fit, capped so a long stall cannot trigger a spiral of death.

const canvas = document.querySelector('#canvas');
const status = document.querySelector('#status');
const progress = document.querySelector('#progress');
const errorBox = document.querySelector('#error');

const WIDTH = 640;
const HEIGHT = 480;
const STEP_MS = 1000 / 60;
const MAX_STEPS_PER_FRAME = 4;

const context = canvas.getContext('2d', { alpha: false });
context.imageSmoothingEnabled = false;

let core = null;
let running = false;
let paused = false;
let accumulator = 0;
let lastTime = 0;
let frames = 0;
let healthAt = 0;
let imageData = null;

const keys = new Set();

function fail(error) {
  running = false;
  errorBox.hidden = false;
  errorBox.textContent = '游戏运行失败：' + (error && error.message ? error.message : error);
  console.error(error);
}

function moduleError() {
  if (!core) return 'module not loaded';
  const pointer = core._th185_error();
  return core.UTF8ToString(pointer);
}

// The module exposes a 640x480 RGBA8 buffer. Copy it into the canvas.
function present() {
  const width = core._th185_frame_width();
  const height = core._th185_frame_height();
  const bytes = core._th185_frame_bytes();
  if (!width || !height || !bytes) return false;

  const pointer = core._th185_frame_pixels();
  if (!pointer) return false;

  if (!imageData || imageData.width !== width || imageData.height !== height) {
    canvas.width = width;
    canvas.height = height;
    imageData = context.createImageData(width, height);
  }

  // HEAPU8 is a view over the module's memory; it may be replaced when the
  // heap grows, so read it fresh every frame.
  const source = core.HEAPU8.subarray(pointer, pointer + bytes);
  imageData.data.set(source);
  context.putImageData(imageData, 0, 0);
  return true;
}

function frame(now) {
  if (!running) return;

  if (paused) {
    lastTime = now;
    accumulator = 0;
  } else {
    accumulator += now - lastTime;
    lastTime = now;

    let steps = 0;
    while (accumulator >= STEP_MS && steps < MAX_STEPS_PER_FRAME) {
      if (!core._th185_tick(0)) {
        fail(new Error(moduleError()));
        return;
      }
      accumulator -= STEP_MS;
      ++steps;
    }
    // Drop the backlog rather than accumulate it forever.
    if (accumulator > STEP_MS * MAX_STEPS_PER_FRAME) accumulator = 0;

    if (steps > 0 && !present()) {
      fail(new Error('帧缓冲为空'));
      return;
    }
  }

  if (now - healthAt >= 500) {
    const elapsed = now - healthAt;
    const fps = elapsed > 0 ? (frames * 1000) / elapsed : 0;
    const quads = core._th185_render_quads();
    const live = core._th185_live_animations();
    document.querySelector('#health').textContent =
      paused ? '已暂停'
             : fps.toFixed(0) + ' FPS · ' + quads + ' 精灵 · ' + live + ' 动画';
    frames = 0;
    healthAt = now;
  }
  ++frames;

  requestAnimationFrame(frame);
}

async function prepare() {
  status.textContent = '正在加载引擎…';
  const moduleUrl = new URL('/th185-app.mjs', location.href).href;
  const factory = (await import(moduleUrl)).default;

  core = await factory({
    print: (line) => console.log('[wasm] ' + line),
    printErr: (line) => console.warn('[wasm] ' + line),
    locateFile: (name) => new URL('/' + name, location.href).href,
  });

  status.textContent = '正在加载游戏资源…';
  progress.hidden = false;
  const response = await fetch('/th185.dat');
  if (!response.ok) {
    throw new Error('th185.dat 下载失败 (' + response.status + ')');
  }
  const archive = new Uint8Array(await response.arrayBuffer());
  progress.max = archive.length;
  progress.value = archive.length;
  status.textContent =
    '资源 ' + (archive.length / 1048576).toFixed(1) + ' MB，正在初始化…';

  core.FS.writeFile('/th185.dat', archive, { canOwn: true });

  if (!core._th185_initialize()) {
    throw new Error(moduleError());
  }
  if (!core._th185_start()) {
    throw new Error(moduleError());
  }

  progress.hidden = true;
  status.textContent = '就绪';
}

document.querySelector('#start').addEventListener('click', async () => {
  const button = document.querySelector('#start');
  button.disabled = true;
  errorBox.hidden = true;
  try {
    if (!core) await prepare();
    else if (!core._th185_restart()) throw new Error(moduleError());

    document.querySelector('#welcome').hidden = true;
    running = true;
    paused = false;
    document.querySelector('#pause-panel').hidden = true;

    const now = performance.now();
    lastTime = now;
    healthAt = now;
    accumulator = 0;
    frames = 0;
    canvas.focus({ preventScroll: true });
    requestAnimationFrame(frame);
  } catch (error) {
    button.disabled = false;
    fail(error);
  }
});

function setPaused(value) {
  if (!core || !running) return;
  if (value) {
    if (!core._th185_pause()) return;
  } else {
    if (!core._th185_resume()) return;
  }
  paused = value;
  keys.clear();
  document.querySelector('#pause-panel').hidden = !paused;
  lastTime = performance.now();
  accumulator = 0;
}

const RECOGNISED = new Set([
  'KeyZ', 'KeyX', 'KeyC', 'ShiftLeft', 'ShiftRight',
  'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', 'Escape',
]);

window.addEventListener('keydown', (event) => {
  if (!running) return;
  if (event.code === 'Escape') {
    event.preventDefault();
    if (!event.repeat) setPaused(!paused);
    return;
  }
  if (!RECOGNISED.has(event.code)) return;
  event.preventDefault();
  if (!paused) keys.add(event.code);
});

window.addEventListener('keyup', (event) => keys.delete(event.code));

window.addEventListener('blur', () => {
  keys.clear();
  setPaused(true);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    keys.clear();
    setPaused(true);
  }
});

document.querySelector('#fullscreen').addEventListener('click', () => {
  if (document.fullscreenElement) {
    document.exitFullscreen();
  } else {
    document.documentElement.requestFullscreen().catch(() => {});
  }
});

document.querySelector('#resume').addEventListener('click', () => {
  setPaused(false);
  canvas.focus({ preventScroll: true });
});

document.querySelector('#return-title').addEventListener('click', () => {
  running = false;
  paused = false;
  keys.clear();
  document.querySelector('#pause-panel').hidden = true;
  document.querySelector('#welcome').hidden = false;
  document.querySelector('#start').disabled = false;
});

const touch = matchMedia('(any-pointer: coarse)').matches;
document.querySelector('#touch').hidden = !touch;
document.querySelector('#touch-toggle').addEventListener('click', () => {
  const panel = document.querySelector('#touch');
  panel.hidden = !panel.hidden;
});

for (const id of ['shoot', 'focus', 'bomb', 'card']) {
  const button = document.querySelector('#' + id);
  if (!button) continue;
  // Touch buttons are placeholders until the input layer maps them to the
  // module's key bitmask; pressing them currently does nothing.
  button.addEventListener('pointerdown', (event) => event.preventDefault());
}

document.querySelector('#pause').addEventListener('click', () => setPaused(true));

window.addEventListener('error', (event) => fail(event.error || event.message));
window.addEventListener('unhandledrejection', (event) => fail(event.reason));
