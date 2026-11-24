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
// Bits contributed by the on-screen touch pad. Kept separate from `keys`
// because the keyboard map is code -> bit while the touch pad is id -> bit;
// merging them into one set would compare apples to oranges.
const touchButtons = new Set();

// Button bitmask, matching th185::PlayerButton in cpp/game/Player.hpp.
const BUTTON = {
  shoot: 1 << 0,
  bomb: 1 << 1,
  focus: 1 << 2,
  special: 1 << 3,
  up: 1 << 4,
  down: 1 << 5,
  left: 1 << 6,
  right: 1 << 7,
};

// Keyboard -> button. TH uses Z/X/Shift by convention; arrows move.
const KEY_TO_BUTTON = {
  KeyZ: BUTTON.shoot,
  KeyX: BUTTON.bomb,
  KeyC: BUTTON.special,
  ShiftLeft: BUTTON.focus,
  ShiftRight: BUTTON.focus,
  ArrowUp: BUTTON.up,
  ArrowDown: BUTTON.down,
  ArrowLeft: BUTTON.left,
  ArrowRight: BUTTON.right,
};

function heldMask() {
  let mask = 0;
  for (const code of keys) {
    const button = KEY_TO_BUTTON[code];
    if (button) mask |= button;
  }
  for (const button of touchButtons) mask |= button;
  return mask;
}

// Drops every held input. Pausing, blur and tab-hide all need this: a key that
// was down when focus left never delivers its keyup, so without a sweep the
// player would resume mid-move with the button stuck on. The touch bits need the
// same sweep for the same reason (a pointerup outside the window is lost).
function clearInput() {
  keys.clear();
  touchButtons.clear();
}

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

// --- audio ---------------------------------------------------------------
//
// The WASM mixer produces one tick of stereo float32 at 22050 Hz. Browsers
// will not let a page start audio without a gesture, so the context is created
// lazily on the first pointer/key event and the queue is drained from a
// ScriptProcessor callback sized to exactly one tick.
//
// Why ScriptProcessor rather than AudioWorklet: the worklet needs a separate
// module file and a message channel, and the mixer already runs inside the
// same thread as the frame loop. One tick is 367 frames at 22050 Hz, which is
// comfortably inside what ScriptProcessor can keep up with.
const AUDIO_RATE = 22050;

let audioContext = null;
let audioNode = null;
let audioEnabled = false;
let audioUnderruns = 0;
let audioTicks = 0;

function ensureAudio() {
  if (audioContext || !core) return;
  const Ctor = window.AudioContext || window.webkitAudioContext;
  if (!Ctor) return;
  // Audio is optional: a browser that rejects the context (or a size) must not
  // stop the game. This used to throw on the first key press and kill the
  // frame loop.
  try {

  const framesPerTick = core._th185_audio_frames_per_tick();
  const rate = core._th185_audio_rate();
  if (!framesPerTick || !rate) return;

  // ScriptProcessor only accepts 0 or a power of two between 256 and 16384.
  // The mixer produces 367 frames per tick (22050 Hz / 60 Hz), which is NOT a
  // power of two -- passing it threw
  //   "buffer size (367) must be 0 or a power of two between 256 and 16384"
  // and, because ensureAudio runs on the first input event, the exception
  // killed the frame loop the moment the player pressed a key.
  //
  // The two numbers are independent: the mixer keeps writing 367-frame ticks,
  // and this callback pulls whatever the buffer holds. 512 is the smallest
  // legal size above one tick, so a callback never starves on a fresh tick.
  const kAudioBufferFrames = 512;
  audioContext = new Ctor({ sampleRate: rate });
  audioNode = audioContext.createScriptProcessor(kAudioBufferFrames, 0, 2);
  audioNode.onaudioprocess = (event) => {
    const left = event.outputBuffer.getChannelData(0);
    const right = event.outputBuffer.getChannelData(1);
    const count = left.length;

    const pointer = core._th185_audio_frame();
    const samples = core._th185_audio_frame_samples();
    // The callback buffer (512) is larger than one mixer tick (367), so the
    // old `samples < count * 2` test would have been true forever and the
    // game would have gone permanently silent. Play what is there and pad the
    // rest with silence instead.
    const available = (pointer && samples >= 2) ? Math.floor(samples / 2) : 0;
    if (available === 0) {
      left.fill(0);
      right.fill(0);
      ++audioUnderruns;
      return;
    }

    // HEAPF32 can be detached by a memory growth, so re-read it each call.
    const heap = core.HEAPF32;
    const base = pointer >> 2;
    const playable = Math.min(count, available);
    for (let i = 0; i < playable; ++i) {
      left[i] = heap[base + i * 2];
      right[i] = heap[base + i * 2 + 1];
    }
    if (playable < count) {
      left.fill(0, playable);
      right.fill(0, playable);
      ++audioUnderruns;
    }
    ++audioTicks;
  };
  audioNode.connect(audioContext.destination);
  audioEnabled = true;
  } catch (err) {
    console.warn('audio disabled:', err && err.message ? err.message : err);
    audioContext = null;
    audioNode = null;
    audioEnabled = false;
  }
}

function resumeAudio() {
  ensureAudio();
  if (audioContext && audioContext.state === 'suspended') {
    audioContext.resume().catch(() => {});
  }
}

// Audio needs a user gesture before a browser will start it. Bind ONCE, at module
// scope, then unhook so later gestures do not re-create the context.
//
// This used to live INSIDE frame(). addEventListener deduplicates by listener
// identity, and a closure created inside frame() is a brand new function object
// every call -- so each frame added two more listeners (pointerdown + keydown)
// and none were removed until a gesture fired. After one minute at 60 fps that
// is ~7200 accumulated listeners, all invoked on the next keypress. It was a
// leak that also made the first input event slow, on a path nobody looks at
// because the game still "worked".
const wakeAudio = () => {
  resumeAudio();
  window.removeEventListener('pointerdown', wakeAudio);
  window.removeEventListener('keydown', wakeAudio);
};
window.addEventListener('pointerdown', wakeAudio);
window.addEventListener('keydown', wakeAudio);

function audioHealth() {
  if (!audioEnabled) return '音频未启用';
  if (!audioContext) return '音频初始化中';
  if (audioContext.state !== 'running') return '音频已挂起';
  return '音频 ' + audioTicks + ' 块' + (audioUnderruns ? ' · 欠载 ' + audioUnderruns : '');
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
      if (!core._th185_tick(heldMask())) {
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
             : fps.toFixed(0) + ' FPS · ' + quads + ' 精灵 · ' + live + ' 动画'
               + ' · ' + audioHealth();
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
  clearInput();
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
  clearInput();
  setPaused(true);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearInput();
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
  clearInput();
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

// On-screen pad -> button bits. These used to be placeholders that only called
// preventDefault(), so on a touch device the game was unplayable: the pad was
// drawn, pressable, and did nothing. The module's tick() already accepts a
// bitmask, so the whole fix is to fold these into heldMask().
//
// The mapping uses the same BUTTON table as the keyboard. #card is mapped to
// `special` because that is the button the market/pause confirm and the active
// card share in this build; there is no separate card key yet.
const TOUCH_TO_BUTTON = {
  shoot: BUTTON.shoot,
  focus: BUTTON.focus,
  bomb: BUTTON.bomb,
  card: BUTTON.special,
};

for (const [id, bit] of Object.entries(TOUCH_TO_BUTTON)) {
  const button = document.querySelector('#' + id);
  if (!button) continue;
  // pointerdown/up rather than click: click fires on release, which would make
  // a held touch behave like a tap and break continuous fire and focus.
  button.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    touchButtons.add(bit);
    // Capture so a finger that slides off the button still delivers pointerup;
    // without this the bit would stay stuck on and the player would keep firing.
    if (button.setPointerCapture) {
      try { button.setPointerCapture(event.pointerId); } catch (_) {}
    }
    resumeAudio();
  });
  const release = (event) => {
    if (event) event.preventDefault();
    touchButtons.delete(bit);
  };
  button.addEventListener('pointerup', release);
  button.addEventListener('pointercancel', release);
  button.addEventListener('pointerleave', (event) => {
    // Only release when the pointer that left is not captured (mouse hover-off
    // should not cancel a drag; a genuinely left touch should).
    if (!button.hasPointerCapture || !button.hasPointerCapture(event.pointerId)) release(event);
  });
  button.addEventListener('contextmenu', (event) => event.preventDefault());
}

document.querySelector('#pause').addEventListener('click', () => setPaused(true));

window.addEventListener('error', (event) => fail(event.error || event.message));
window.addEventListener('unhandledrejection', (event) => fail(event.reason));
