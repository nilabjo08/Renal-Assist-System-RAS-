#!/usr/bin/env python3
"""Cross-check the full-reprocess step table in src/app/cycle.c against the PRD v0.1 section 4
valve truth table (transcribed below, in cycle order) and the section 3 time limits."""
import re, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, '..', 'src', 'app', 'cycle.c')).read()
# PRD section 4 truth table, in execution order: (PRD step, valves open, pump, max seconds)
PRD = [
 ('Self-check',              '',                     0, 10),
 ('Connector check',         'V1 V3 V4 V6 V7 V9',    0, 20),
 ('Empty tank',              'V1 V3 V5 V6',          0, 120),
 ('Dose concentrate',        '',                     1, 60),
 ('Dilute',                  'V2 V5',                0, 45),
 ('Clean through membrane',  'V1 V5 V9 V6 V7',       0, 60),   # PRD "XX s": 60 s chosen
 ('Clean, top connector',    'V1 V5 V9 V7',          0, 60),
 ('Flush blood top>bottom',  'V2 V4 V6',             0, 30),
 ('Flush blood bottom>top',  'V2 V3 V7',             0, 30),
 ('Flush dialysate DI>DO',   'V2 V9 V8',             0, 30),
 ('Fill tank for TCV',       'V2 V5',                0, 45),
 ('Empty blood side',        'V1 V10 V6',            0, 20),
 ('TCV draw from tank',      'V1 V5 V3 V7',          0, 60),
 ('TCV settle (firmware)',   '',                     0, 1.5),  # added: weigh W2 with valves closed
 ('Empty tank',              'V1 V3 V5 V6',          0, 120),
 ('Leak: evacuate',          'V1 V9 V8 V10',         0, 30),
 ('Leak: hold',              'V9 V10',               0, 21),   # 1 s settle + 20 s window
 ('Germicide: dose',         '',                     1, 60),
 ('Germicide: dilute',       'V2 V5',                0, 45),   # PRD 105 s for dose + dilute together
 ('Germicide > dialysate',   'V1 V5 V9 V8 V7',       0, 60),
 ('Germicide > blood',       'V1 V5 V3 V8 V7',       0, 60),
]
block = re.search(r'static const step_t full_steps\[\] = \{(.*?)\n\};', SRC, re.S).group(1)
rows = re.findall(r'\{\s*"([^"]+)",\s*\d+,\s*([^,]+),\s*(\d),[^}]*?S\(([^)]+)\)', block)
def mask(s):
    s = s.strip()
    return set() if s == '0' else set(x.strip() for x in s.split('|'))
consts = dict(re.findall(r'#define\s+(\w+)\s+(\d+)u', open(os.path.join(HERE, '..', 'src', 'app', 'config.h')).read()))
def ms(expr):
    for k, v in consts.items(): expr = expr.replace(k, v)
    return eval(expr) / 1000.0
errors = []
if len(rows) != len(PRD): errors.append(f'{len(rows)} firmware steps vs {len(PRD)} PRD steps')
for i, (prd, fw) in enumerate(zip(PRD, rows)):
    name, valves, pump, maxms = fw
    if mask(valves) != set(prd[1].split()): errors.append(f'step {i+1} {name}: valves {sorted(mask(valves))} != PRD {prd[0]} {prd[1].split()}')
    if int(pump) != prd[2]: errors.append(f'step {i+1} {name}: pump {pump} != PRD {prd[2]}')
    if abs(ms(maxms) - prd[3]) > 1e-6: errors.append(f'step {i+1} {name}: max {ms(maxms)} s != PRD {prd[3]} s')
vent = re.search(r'vent_step\s*=\s*\{\s*"[^"]+",\s*\d+,\s*([^,]+),', SRC).group(1)
if mask(vent) != {'V5', 'V9', 'V10'}: errors.append(f'vent pattern {mask(vent)} != PRD V5 V9 V10')
print(f'cycle table: {len(rows)} steps checked against PRD truth table, {len(errors)} errors')
for e in errors: print('  ERROR', e)
sys.exit(1 if errors else 0)
