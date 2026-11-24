#!/usr/bin/env python3
"""Check that the web shell and the WASM module agree with each other.

There is no browser automation available here, so instead of pretending to test
the page we verify the CONTRACT: every export the shell calls must exist in the
module, every element the shell queries must exist in the HTML, and the paths
the HTML references must resolve. Those are the failures that actually happen,
and they are all checkable without a browser.

What this does NOT prove: that the page renders. That needs a browser.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
WEB = ROOT / 'sdl-runtime'


def read(path: Path) -> str:
    return path.read_text(encoding='utf-8', errors='replace')


def exported_functions() -> set[str]:
    """Names the build will export.

    The list is GENERATED from Exports.cpp, not written in build-cpp.mjs.
    It used to be hand-maintained and every function added later was missing
    at runtime (probes threw "not a function"), so the build now scans the
    EMSCRIPTEN_KEEPALIVE declarations. This check mirrors that scan.
    """
    build = read(ROOT / 'scripts' / 'build-cpp.mjs')
    if 'exportedFunctions(' in build:
        source = read(ROOT / 'cpp' / 'sdl' / 'Exports.cpp')
        return set('_' + name for name in re.findall(
            r'EMSCRIPTEN_KEEPALIVE[\s\S]{0,120}?\b(th185_\w+)\s*\(', source))
    # Legacy hand-written list.
    names = set()
    for match in re.finditer(r'\\"(_th185_[a-z_]+)\\"', build):
        names.add(match.group(1))
    return names


def selftest() -> int:
    print('=== web shell contract selftest ===')
    print()
    failures = 0

    html_path = WEB / 'th185.html'
    shell_path = WEB / 'shell.mjs'
    style_path = WEB / 'style.css'
    for path in (html_path, shell_path, style_path):
        ok = path.is_file()
        print('[%s] %s exists' % ('PASS' if ok else 'FAIL', path.name))
        if not ok:
            failures += 1
    if failures:
        print()
        print('selftest failed: the shell is incomplete')
        return 1

    html = read(html_path)
    shell = read(shell_path)

    # 1. Paths the HTML references must resolve to files we ship.
    referenced = re.findall(r'(?:href|src)="([^"]+)"', html)
    missing = []
    for ref in referenced:
        if ref.startswith('http') or ref.startswith('data:'):
            continue
        if ref == '/app/style.css':
            target = style_path
        elif ref == '/app/shell.mjs':
            target = shell_path
        else:
            continue
        if not target.is_file():
            missing.append(ref)
    ok = not missing
    print('[%s] every asset the HTML references exists (%d missing)'
          % ('PASS' if ok else 'FAIL', len(missing)))
    if not ok:
        for item in missing:
            print('       ' + item)
        failures += 1

    # 2. Elements the shell queries must exist in the HTML.
    queried = set(re.findall(r"querySelector\('#([a-zA-Z0-9_-]+)'\)", shell))
    declared = set(re.findall(r'id="([^"]+)"', html))
    absent = sorted(queried - declared)
    ok = not absent
    print('[%s] every element the shell queries exists in the HTML (%d missing)'
          % ('PASS' if ok else 'FAIL', len(absent)))
    if not ok:
        for item in absent:
            print('       #' + item)
        failures += 1

    # 3. Module functions the shell calls must be in the export list.
    called = set(re.findall(r'core\.(_th185_[a-z_]+)\(', shell))
    exported = exported_functions()
    ok = bool(exported)
    print('[%s] the build exports th185 functions (%d found)'
          % ('PASS' if ok else 'FAIL', len(exported)))
    if not ok:
        failures += 1

    if exported:
        unknown = sorted(called - exported)
        ok = not unknown
        print('[%s] every function the shell calls is exported (%d missing)'
              % ('PASS' if ok else 'FAIL', len(unknown)))
        if not ok:
            for item in unknown:
                print('       ' + item)
            failures += 1

    # 4. Every Emscripten RUNTIME method the shell touches must be exported.
    #
    # The old check only asked "is HEAPU8 in the build file", which is a string
    # search: it passed while UTF8ToString -- which the shell calls in three
    # places, including populatePickers() on the first start -- was absent from
    # EXPORTED_RUNTIME_METHODS. On the real artifact that threw
    #   TypeError: core.UTF8ToString is not a function
    # inside prepare(), so the game never started in a browser at all.
    #
    # The property that actually has to hold is "the shell's core.X accesses
    # are a subset of what the app link exports", so derive both sides.
    build = read(ROOT / 'scripts' / 'build-cpp.mjs')
    # The app link is the one with MODULARIZE + ENVIRONMENT=web,node; take the
    # LAST EXPORTED_RUNTIME_METHODS that is not the node-harness one, which is
    # identified by carrying NODEFS.
    runtime_lists = re.findall(r'-sEXPORTED_RUNTIME_METHODS=([^"\']+)', build)
    app_methods: set[str] = set()
    for entry in runtime_lists:
        names = {n.strip() for n in entry.split(',') if n.strip()}
        if 'NODEFS' not in names:
            app_methods = names
    used_runtime = set(re.findall(r'core\.([A-Z][A-Za-z0-9_]*)\b', shell))
    # _th185_* are KEEPALIVE C exports, checked separately; HEAP* are views the
    # runtime always exposes but that still belong in the list for clarity.
    used_runtime = {n for n in used_runtime if not n.startswith('_')}
    missing_runtime = sorted(used_runtime - app_methods)
    ok = not missing_runtime
    print('[%s] every runtime method the shell uses is exported (%d used, %d missing)'
          % ('PASS' if ok else 'FAIL', len(used_runtime), len(missing_runtime)))
    if not ok:
        for item in missing_runtime:
            print('       ' + item)
        failures += 1

    ok = 'HEAPU8' in app_methods
    print('[%s] HEAPU8 is in EXPORTED_RUNTIME_METHODS'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 5. The shell must not use APIs that need a secure context it will not get.
    forbidden = []
    for token in ('SharedArrayBuffer', 'navigator.gpu', 'WebGL2RenderingContext'):
        if token in shell:
            forbidden.append(token)
    ok = not forbidden
    print('[%s] the shell avoids context-dependent APIs (%d found)'
          % ('PASS' if ok else 'FAIL', len(forbidden)))
    if not ok:
        for item in forbidden:
            print('       ' + item)
        failures += 1

    # 5.1 Audio must survive a browser that rejects the ScriptProcessor size.
    #
    # ScriptProcessorNode only accepts 0 or a power of two between 256 and 16384.
    # The shell used to pass the mixer's per-tick frame count (367 at 22050 Hz),
    # which threw on the first key press -- and because ensureAudio() runs from
    # the input handler, that exception killed the frame loop. Two invariants
    # keep it fixed: a literal power of two, and a try/catch around the init.
    audio_sizes = re.findall(r'createScriptProcessor\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,', shell)
    audio_ok = True
    for name in audio_sizes:
        match = re.search(r'(?:const|let|var)\s+' + re.escape(name) + r'\s*=\s*(\d+)', shell)
        if not match:
            continue
        size = int(match.group(1))
        if size < 256 or size > 16384 or (size & (size - 1)) != 0:
            audio_ok = False
    print('[%s] ScriptProcessor buffer sizes are legal powers of two (%s)'
          % ('PASS' if audio_ok else 'FAIL', ','.join(audio_sizes) or 'none'))
    if not audio_ok:
        failures += 1

    audio_guarded = 'catch (err)' in shell and 'audio disabled' in shell
    print('[%s] audio initialisation is wrapped in try/catch'
          % ('PASS' if audio_guarded else 'FAIL'))
    if not audio_guarded:
        failures += 1

# 6. Frame pacing must be bounded, or a stall becomes an infinite loop.
    ok = 'MAX_STEPS_PER_FRAME' in shell
    print('[%s] the frame loop bounds its catch-up steps'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 7. Every button on the on-screen pad must actually contribute a bit.
    #
    # The pad used to be drawn and pressable while doing nothing but
    # preventDefault(), which makes the game unplayable on a touch device and is
    # invisible to any check that only asks 'does the element exist'. Checking
    # that each id maps to a BUTTON bit AND reaches heldMask() is the difference
    # between 'the button is there' and 'the button works'.
    touch_ids = re.findall(r'<button id="([a-z]+)"', html)
    # Buttons that are NOT part of the movement/shoot bitmask: menu actions that
    # have their own handlers (setPaused, fullscreen, start/...). #pause is one
    # of these -- it calls setPaused() directly, which sends the module a
    # _th185_pause() call rather than a held bit, so it is correctly absent from
    # TOUCH_TO_BUTTON. Listing it as "missing" was a bug in this check, not in
    # the shell.
    NON_BITMASK = {'start', 'resume', 'fullscreen', 'touch-toggle',
                   'return-title', 'pause'}
    pad_ids = [i for i in touch_ids if i not in NON_BITMASK]
    mapping = re.search(r'const TOUCH_TO_BUTTON = \{([^}]*)\}', shell)
    mapped = {}
    if mapping:
        for key, value in re.findall(r'(\w+)\s*:\s*(BUTTON\.\w+)', mapping.group(1)):
            mapped[key] = value
    missing = [i for i in pad_ids if i not in mapped]
    ok = bool(pad_ids) and not missing
    print('[%s] every touch-pad button maps to a BUTTON bit (%d pad, %d missing)'
          % ('PASS' if ok else 'FAIL', len(pad_ids), len(missing)))
    if not ok:
        for item in missing:
            print('       #' + item)
        failures += 1

    # The bits must be OR-ed into the mask the module receives, not just stored.
    ok = re.search(r'for \(const button of touchButtons\)\s*mask \|= button', shell) is not None
    print('[%s] touch bits are folded into heldMask()'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 8. addEventListener must not be called from inside the frame loop.
    #
    # addEventListener deduplicates by listener IDENTITY. A closure created
    # inside frame() is a new function object on every call, so it is never
    # deduplicated and the listeners accumulate: the audio-unlock handler used
    # to do exactly this, adding two listeners per frame (pointerdown + keydown)
    # until the first gesture fired -- thousands of pending listeners after a
    # minute of play, all invoked on the next keypress.
    #
    # Checking "the listener is registered at module scope" is the property that
    # actually prevents the leak, and it is checkable without a browser.
    frame_body = re.search(r'function frame\(now\) \{([\s\S]*?)\n\}', shell)
    leak = False
    if frame_body and 'addEventListener' in frame_body.group(1):
        leak = True
    print('[%s] the frame loop registers no event listeners (leak guard)'
          % ('PASS' if not leak else 'FAIL'))
    if leak:
        failures += 1

    # 9. The pause menu must be driven while paused.
    #
    # While paused, frame() does not tick, so the module cannot receive a button
    # bitmask -- the menu had to be driven from discrete key presses. Without
    # that, the pause panel appeared and its selection could never move, even
    # though three exports (pause_item / pause_move / pause_confirm) existed and
    # the probe drove them directly. The gap was purely in the shell.
    for name in ('_th185_pause_move', '_th185_pause_confirm', '_th185_pause_item'):
        ok = name in shell
        print('[%s] the shell drives the pause menu (%s)'
              % ('PASS' if ok else 'FAIL', name))
        if not ok:
            failures += 1

    # The selection label the shell writes must exist in the HTML, or the user
    # sees no feedback at all.
    ok = 'pause-selection' in html
    print('[%s] the pause panel has a selection label'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 9.1 The scene / difficulty pickers must exist AND be wired.
    #
    # The shell used to always load scene 0 at difficulty 0. Eight of the nine
    # scenes and seven of the eight difficulties were unreachable from the
    # browser, even though every export existed and the probe drove them
    # directly -- the gap was purely in the shell, and nothing noticed because
    # "the game runs" was the only thing being checked.
    for element in ('id="scene"', 'id="difficulty"'):
        ok = element in html
        print('[%s] the welcome panel has a %s picker'
              % ('PASS' if ok else 'FAIL', element.split('"')[1]))
        if not ok:
            failures += 1

    for name in ('_th185_scene_count', '_th185_scene_name',
                 '_th185_load_stage', '_th185_set_difficulty'):
        ok = name in shell
        print('[%s] the shell uses %s' % ('PASS' if ok else 'FAIL', name))
        if not ok:
            failures += 1

    # 9.2 A stage load must also reset the run.
    #
    # load_stage() swaps the scene data only. Lives, the starting loadout and
    # the magic circle are all created by reset_scenario(), and the shell did
    # not call it -- so the browser ran with player_lives=0, loadout_size=0 and
    # magic_ready=0 while every probe reported healthy numbers, because the
    # probes call reset_scenario() themselves (28 sites in probe-input.mjs) and
    # were therefore asserting on their own setup.
    #
    # The contract that has to hold is "after load_stage the run is initialised",
    # so check that the shell calls reset_scenario() and that it does so in
    # applySelection(), i.e. on the path every start and restart takes.
    apply_body = re.search(r'function applySelection\(\)\s*\{([\s\S]*?)\n\}', shell)
    ok = apply_body is not None and '_th185_reset_scenario' in apply_body.group(1)
    print('[%s] applySelection() re-initialises the run (reset_scenario)'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    ok = '_th185_reset_scenario' in exported
    print('[%s] the build exports reset_scenario' % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 10. pause_confirm must read the module's phase rather than assume which
    #     item was picked: Resume/Restart keep Playing, Quit goes to Idle.
    ok = re.search(r'function pauseActivate\(\)[\s\S]{0,600}?_th185_phase\(\)', shell) is not None
    print('[%s] pause_confirm result is taken from the module phase'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # Pausing must drop the touch bits too, or a held direction stays stuck on
    # after resume (the pointerup is lost while the game is paused).
    ok = ('function clearInput' in shell
          and 'touchButtons.clear()' in shell
          and re.search(r'function clearInput\(\)[\s\S]{0,200}?touchButtons\.clear\(\)', shell) is not None)
    print('[%s] clearInput() releases touch bits as well as keys'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: the shell and the module agree')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())

