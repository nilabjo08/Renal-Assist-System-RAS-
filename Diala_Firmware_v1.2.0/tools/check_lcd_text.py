#!/usr/bin/env python3
"""Every string the firmware puts on the 20x4 LCD must fit in 20 characters: step names, alarm
texts and hints, menu items, program names and literal line() calls."""
import re, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, '..', 'src', 'app')
src = {f: open(os.path.join(APP, f)).read() for f in os.listdir(APP) if f.endswith('.c')}
errors, checked = [], 0
def chk(where, t, limit=20):
    global checked
    checked += 1
    if len(t) > limit: errors.append(f'{where}: "{t}" is {len(t)} chars (max {limit})')
for m in re.finditer(r'\{\s*"([^"]*)",\s*\d+,', src['cycle.c']): chk('step name', m.group(1))
for m in re.finditer(r'\[ALM_\w+\]\s*=\s*\{\s*"([^"]*)",\s*"([^"]*)"', src['alarms.c']): chk('alarm', m.group(1)); chk('hint', m.group(2))
menu = re.search(r'menu_items\[\] = \{(.*?)\};', src['ui.c'], re.S).group(1)
for t in re.findall(r'"([^"]*)"', menu): chk('menu item (after "> ")', t, 18)
for t in re.findall(r'"([^"]*)"', re.search(r'const n\[PROG_COUNT\] = \{(.*?)\};', src['recipes.c'], re.S).group(1)): chk('program', t, 15)
for m in re.finditer(r'line\(\d,\s*"([^"]*)"\)', src['ui.c']): chk('ui line', m.group(1))
for m in re.finditer(r'\?\s*"([^"]*)"\s*:\s*"([^"]*)"', src['ui.c']): chk('ui text', m.group(1)); chk('ui text', m.group(2))
for m in re.finditer(r'prompt\("([^"]*)",\s*"([^"]*)"', src['kittest.c']): chk('kit-test prompt', m.group(1)); chk('kit-test prompt', m.group(2))
for m in re.finditer(r'lcd_fmt\(lcd[12],\s*"([^"]*)"\)', src['kittest.c']): chk('kit-test lcd', m.group(1))
for m in re.finditer(r'\[K_\w+\]\s*=\s*\{\s*\d,\s*"([^"]*)"', src['kittest.c']): chk('kit-test item (after "KIT n.n ")', m.group(1), 12)
print(f'lcd text: {checked} strings checked, {len(errors)} errors')
for e in errors: print('  ERROR', e)
sys.exit(1 if errors else 0)
