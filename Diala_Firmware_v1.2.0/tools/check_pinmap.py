#!/usr/bin/env python3
"""Cross-check src/bsp/stm32/board.h against the board as fabricated.
Source 1: hw/diala_main_rc4_pads.csv, every copper pad and its net as written into the RC4 Gerbers
          (X2 attributes, fabrication set of 17 Oct 2026).
Source 2: hw/diala_main.net, the schematic netlist (must agree with the Gerbers pad for pad).
For every PIN_ line: port/pin -> LQFP-100 pin number -> net must match the comment."""
import re, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
NET = os.environ.get('DIALA_NETLIST', os.path.join(HERE, '..', 'hw', 'diala_main.net'))
BOARD = os.path.join(HERE, '..', 'src', 'bsp', 'stm32', 'board.h')
# STM32F407Vx LQFP-100 pinout (DS8626 table 7)
LQFP100 = {1:'PE2',2:'PE3',3:'PE4',4:'PE5',5:'PE6',7:'PC13',8:'PC14',9:'PC15',12:'PH0',13:'PH1',15:'PC0',16:'PC1',
 17:'PC2',18:'PC3',23:'PA0',24:'PA1',25:'PA2',26:'PA3',29:'PA4',30:'PA5',31:'PA6',32:'PA7',33:'PC4',34:'PC5',35:'PB0',
 36:'PB1',37:'PB2',38:'PE7',39:'PE8',40:'PE9',41:'PE10',42:'PE11',43:'PE12',44:'PE13',45:'PE14',46:'PE15',47:'PB10',
 48:'PB11',51:'PB12',52:'PB13',53:'PB14',54:'PB15',55:'PD8',56:'PD9',57:'PD10',58:'PD11',59:'PD12',60:'PD13',61:'PD14',
 62:'PD15',63:'PC6',64:'PC7',65:'PC8',66:'PC9',67:'PA8',68:'PA9',69:'PA10',70:'PA11',71:'PA12',72:'PA13',76:'PA14',
 77:'PA15',78:'PC10',79:'PC11',80:'PC12',81:'PD0',82:'PD1',83:'PD2',84:'PD3',85:'PD4',86:'PD5',87:'PD6',88:'PD7',
 89:'PB3',90:'PB4',91:'PB5',92:'PB6',93:'PB7',95:'PB8',96:'PB9',97:'PE0',98:'PE1'}
ADC = {'PA1':1,'PB0':8,'PC0':10,'PC1':11,'PC2':12,'PC3':13,'PC4':14}
import csv
PADS = os.path.join(HERE, '..', 'hw', 'diala_main_rc4_pads.csv')
text = open(NET).read()
netlist = {}
for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)(?=\(net \(code|\Z)', text, re.S):
    for n in re.finditer(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"', m.group(2)):
        netlist[(n.group(1), n.group(2))] = m.group(1)
gerber = {(r['ref'], r['pad']): r['net'] for r in csv.DictReader(open(PADS))}
errors, checked = [], 0
norm = lambda s: 'NC' if s.startswith('unconnected-') or s == 'N/C' else s
for k in sorted(set(netlist) | set(gerber)):
    a, b = norm(netlist.get(k, 'NC')), norm(gerber.get(k, 'NC'))
    if a != b: errors.append(f'{k[0]} pad {k[1]}: netlist {a!r} but RC4 Gerber {b!r}')
u6 = {int(p): n for (r, p), n in gerber.items() if r == 'U6'}
board = open(BOARD).read()
for m in re.finditer(r'#define\s+(PIN_\w+)\s+GPIO([A-E]),\s*(\d+)u\s*/\*\s*U6-(\d+)\s+(\S+)', board):
    name, port, num, upin, net = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), m.group(5)
    checked += 1
    if LQFP100.get(upin) != f'P{port}{num}': errors.append(f'{name}: P{port}{num} is not U6 pin {upin} ({LQFP100.get(upin)})')
    if u6.get(upin) != net: errors.append(f'{name}: U6-{upin} net is {u6.get(upin)!r}, board.h says {net!r}')
for m in re.finditer(r'#define\s+ADCCH_(\w+)\s+(\d+)u', board):
    pass
pinport = {mm.group(1): f'P{mm.group(2)}{mm.group(3)}' for mm in re.finditer(r'#define\s+PIN_(\w+)\s+GPIO([A-E]),\s*(\d+)u', board)}
for key, pin in [('P_BUS','P_BUS'),('P_SUPPLY','P_SUPPLY'),('V24_MON','V24_MON'),('PH','PH_IN'),('V5_MON','V5_MON'),('VRAIL_MON','VRAIL_MON'),('PUMP_I','PUMP_ISENSE')]:
    ch = int(re.search(rf'#define\s+ADCCH_{key}\s+(\d+)u', board).group(1))
    if ADC.get(pinport[pin]) != ch: errors.append(f'ADCCH_{key}={ch} but {pinport[pin]} is ADC channel {ADC.get(pinport[pin])}')
# every MCU signal net that is not a power/clock/debug/spare net must be in board.h
skip = re.compile(r'^(GND|\+3V3A?|NRST|/MCU/(VBAT|VCAP\d|HSE_\w+|LSE_\w+|BOOT\d|SW\w+|SP_\w+)|unconnected.*)$')
used = set(re.findall(r'U6-\d+\s+(\S+)', board))
missing = sorted(n for n in set(u6.values()) if not skip.match(n) and n not in used)
if missing: errors.append('MCU nets with no firmware pin: ' + ', '.join(missing))
print(f'pin map: {checked} firmware pins checked against the RC4 Gerbers ({len(gerber)} pads) and {os.path.basename(NET)}, {len(errors)} errors')
for e in errors: print('  ERROR', e)
sys.exit(1 if errors else 0)
