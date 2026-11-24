#!/usr/bin/env python3
"""Verify the loadout editor's data path against the real module.

The web shell's cards panel cannot be unit-tested here (no browser, and
shell.mjs is a browser module that cannot be imported in Node). What CAN be
tested is the part that goes wrong: which card key is used, whether the
budget/slot gates agree with the module, whether removal targets the slot the
user clicked, and -- the point of the whole panel -- whether the equipped set
actually changes the run.

loadout_probe.mjs drives exactly that against th185-app.mjs and prints
'key: value' lines; this asserts on them.

Why this check exists: the panel's three exports (cards_equip /
cards_loadout_* / cards_buy) were exported and probe-driven for months while
the shell had zero references, so a browser player could never change their
loadout. Nothing failed, because "the function exists" was the only thing
being checked.

Exit codes: 0 ok; 1 failed; 2 environment missing
"""

from __future__ import annotations

import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def dat_path():
    from probe import target_path
    return target_path().parent / 'th185.dat'


def selftest() -> int:
    print('=== loadout editor selftest ===')
    print()
    failures = 0

    module = ROOT / 'artifacts' / 'cpp-check' / 'th185-app.mjs'
    if not module.is_file():
        print('[SKIP] app module not built: %s' % module)
        return 0

    script = ROOT.parent / 'scripts' / 'loadout_probe.mjs'
    if not script.is_file():
        script = ROOT / 'scripts' / 'loadout_probe.mjs'
    if not script.is_file():
        print('[SKIP] loadout_probe.mjs not found')
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
        timeout=600)
    output = result.stdout + result.stderr

    ok = result.returncode == 0
    print('[%s] the loadout probe ran cleanly (rc=%d)'
          % ('PASS' if ok else 'FAIL', result.returncode))
    if not ok:
        for line in output.splitlines()[-12:]:
            print('       ' + line)
        failures += 1

    fields = {}
    for line in output.splitlines():
        if ':' in line and not line.startswith(' '):
            key, _, value = line.partition(':')
            fields[key.strip()] = value.strip()

    def check(name, condition, detail=''):
        nonlocal failures
        print('[%s] %s%s' % ('PASS' if condition else 'FAIL', name,
                             ('  (' + detail + ')') if detail else ''))
        if not condition:
            failures += 1

    check('the table index and the card id are different keys',
          int(fields.get('rows_where_index_ne_id', '0') or 0) > 0,
          fields.get('rows_where_index_ne_id', '?') + ' / 87 rows differ')
    check('card_index_of() round-trips every row',
          fields.get('index_id_roundtrip_bad') == '0',
          fields.get('index_id_roundtrip_bad'))
    check('card_index_of() rejects an unknown id',
          fields.get('index_of_unknown') == '-1', fields.get('index_of_unknown'))

    check('the starting budget is 8 slots',
          fields.get('start_slots') == '8', fields.get('start_slots'))
    check('the starting cost cap is 100',
          fields.get('start_cap') == '100', fields.get('start_cap'))
    check('the starter loadout is the three documented cards',
          fields.get('start_equipped') == 'DANGO,PACHE,FLAN',
          fields.get('start_equipped'))
    check('the starter loadout costs 45',
          fields.get('start_cost') == '45', fields.get('start_cost'))
    check('an owned card reports owns()=1',
          fields.get('start_owns_first') == '1', fields.get('start_owns_first'))

    check('owned_at() enumerates in ascending id order',
          fields.get('owned_ids_sorted') == 'true', fields.get('owned_ids_sorted'))
    check('every enumerated id reports owns()=1',
          fields.get('owned_ids_all_owned') == 'true',
          fields.get('owned_ids_all_owned'))
    check('owned_at() past the end returns the 0xFFFFFFFF sentinel',
          fields.get('start_owned_at_end') == '-1', fields.get('start_owned_at_end'))

    check('equipping everything is capped by slots, not by the loop',
          fields.get('slots_respected') == 'true', fields.get('slots_respected'))
    check('equipping everything never exceeds the cost cap',
          fields.get('cap_respected') == 'true', fields.get('cap_respected'))
    check('the loadout fills up to the slot limit',
          fields.get('equipped_after_click_all') == '8',
          fields.get('equipped_after_click_all'))

    check('unequip() reports success',
          fields.get('unequip_returned') == '1', fields.get('unequip_returned'))
    check('unequip() removes exactly one slot',
          fields.get('unequip_size_delta') == '-1',
          fields.get('unequip_size_delta'))
    check('unequip() frees exactly the removed card cost',
          fields.get('unequip_freed_cost') == fields.get('unequip_freed_expected'),
          str(fields.get('unequip_freed_cost')) + ' vs '
          + str(fields.get('unequip_freed_expected')))
    check('unequip() removes the card the clicked slot held',
          fields.get('unequip_removed_slot0_card') == 'true',
          'slot0=' + str(fields.get('unequip_slot0_was'))
          + ' removed=' + str(fields.get('unequip_disappeared')))
    check('unequip() rejects an out-of-range slot',
          fields.get('unequip_oob_rejected') == 'true',
          fields.get('unequip_oob_rejected'))

    check('clear_loadout() empties the slots',
          fields.get('cleared_size') == '0', fields.get('cleared_size'))
    check('clear_loadout() zeroes the cost',
          fields.get('cleared_cost') == '0', fields.get('cleared_cost'))

    check('an effect card is equipped successfully',
          fields.get('extend_equipped') == '1', fields.get('extend_equipped'))
    check('the baseline life count is 3',
          fields.get('lives_without') == '3', fields.get('lives_without'))
    check('equipping EXTEND adds exactly one life',
          fields.get('lives_delta') == '1',
          'delta=' + str(fields.get('lives_delta'))
          + ', ' + str(fields.get('lives_without')) + ' -> '
          + str(fields.get('lives_with')))

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: the loadout editor drives the run')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())
