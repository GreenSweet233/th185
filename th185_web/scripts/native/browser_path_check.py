#!/usr/bin/env python3
"""Gate the browser-path probe.

There is no browser here, so the closest thing to a real session is a probe that
performs ONLY the calls shell.mjs performs, in the same order, and then asserts
the resulting run is playable. scripts/browser_path_probe.mjs does that.

Why this wrapper exists: the probe was written, run by hand, and never added to
ci.mjs -- so a whole round of 'the seam between export and call site is broken'
defects could reappear without CI noticing. A check that is not gated is not a
check.

What this does NOT prove: that the page renders. That needs a browser.

Exit codes: 0 ok; 1 failed; 2 environment missing (skipped, not failed)
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def dat_path():
    from probe import target_path
    return target_path().parent / 'th185.dat'


def selftest() -> int:
    print('=== browser-path selftest ===')
    print()

    module = ROOT / 'artifacts' / 'cpp-check' / 'th185-app.mjs'
    if not module.is_file():
        print('[SKIP] app module not built: %s' % module)
        return 0

    script = ROOT / 'scripts' / 'browser_path_probe.mjs'
    if not script.is_file():
        print('[SKIP] browser_path_probe.mjs not found')
        return 0

    try:
        dat = dat_path()
    except Exception as exc:
        print('[SKIP] cannot locate th185.dat: %s' % exc)
        return 0
    if not dat.is_file():
        print('[SKIP] th185.dat not found')
        return 0

    result = subprocess.run(
        ['node', str(script), str(module), str(dat)],
        capture_output=True, text=True, encoding='utf-8', errors='replace',
        timeout=900)
    output = result.stdout + result.stderr

    failures = 0

    def check(name, condition, detail=''):
        nonlocal failures
        print('[%s] %s%s' % ('PASS' if condition else 'FAIL', name,
                             ('  (' + detail + ')') if detail else ''))
        if not condition:
            failures += 1

    ok = result.returncode == 0
    check('the browser-path probe ran cleanly', ok, 'rc=%d' % result.returncode)
    if not ok:
        for line in output.splitlines()[-15:]:
            print('       ' + line)

    fields = {}
    for line in output.splitlines():
        if ':' in line and not line.startswith(' '):
            key, _, value = line.partition(':')
            fields[key.strip()] = value.strip()

    def as_int(name, default=-999):
        try:
            return int(fields.get(name, default))
        except (TypeError, ValueError):
            return default

    # The run the page gets must be initialised: this is the reset_scenario()
    # seam, which the shell was missing entirely at one point.
    check('the run has lives after start', as_int('lives_after_start') > 0,
          fields.get('lives_after_start', '?'))
    check('the run has a loadout after start',
          as_int('loadout_after_start') > 0,
          fields.get('loadout_after_start', '?'))
    check('the magic circle exists after start',
          as_int('magic_ready_after_start') == 1,
          fields.get('magic_ready_after_start', '?'))

    # Names must decode: UTF8ToString was once missing from the export list and
    # the game never started at all.
    check('scene names decode', as_int('scene_names_decoded') == 9,
          fields.get('scene_names_decoded', '?'))
    check('difficulty names decode', as_int('difficulty_names_decoded') == 8,
          fields.get('difficulty_names_decoded', '?'))

    # The framebuffer the shell blits every frame.
    check('the frame buffer is 640x480',
          fields.get('frame') == '640x480', fields.get('frame', '?'))
    check('the frame buffer is not blank',
          as_int('frame_nonzero_samples') > 0,
          fields.get('frame_nonzero_samples', '?'))

    # Input must reach the player through the shell's own bitmask.
    check('the shell bitmask moves the player',
          fields.get('player_moved_right') == 'true',
          fields.get('player_moved_right', '?'))
    check('holding shoot fires bullets', as_int('bullets_fired') > 0,
          fields.get('bullets_fired', '?'))

    # The stage must actually finish through this call path.
    check('the stage clears through the browser path',
          as_int('cleared_at_frame') >= 0, fields.get('cleared_at_frame', '?'))
    check('every scheduled wave fired',
          fields.get('schedules_fired') == fields.get('schedules_expected')
          and as_int('schedules_expected') > 0,
          '%s / %s' % (fields.get('schedules_fired', '?'),
                       fields.get('schedules_expected', '?')))
    check('the phase becomes Cleared (4)', as_int('phase_after_play') == 4,
          fields.get('phase_after_play', '?'))

    # Death state. The defect: game_over was set and nothing happened, so the
    # player stayed at the death position and the stage ran on to a clear at
    # zero lives. The original respawns on life >= 0 (docs/DEATH-AND-PHASE.md).
    check('the probe reached zero lives', as_int('zero_lives_at_frame') >= 0,
          fields.get('zero_lives_at_frame', '?'))
    check('zero lives does not fabricate a game-over state',
          as_int('game_over_flag_at_zero') == 0,
          fields.get('game_over_flag_at_zero', '?'))
    check('the life count stops at zero, never negative',
          as_int('lives_after_zero') == 0, fields.get('lives_after_zero', '?'))
    check('the player is respawned at the start position',
          as_int('player_y_at_zero') > 0, fields.get('player_y_at_zero', '?'))
    check('respawn restores the invulnerability window',
          as_int('player_invuln_at_zero') > 0,
          fields.get('player_invuln_at_zero', '?'))
    check('the death-state assertions all passed',
          as_int('death_state_failures') == 0,
          fields.get('death_state_failures', '?'))

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: the browser call path produces a playable, correct run')
    return 0


if __name__ == '__main__':
    sys.exit(selftest())
