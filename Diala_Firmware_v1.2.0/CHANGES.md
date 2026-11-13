# Diala firmware — changes from v1.1.0 to v1.2.0

Morphedo Technologies · 2026-10-18

v1.2.0 adds an automated board / kit test so results come from the log, not from someone reading the LCD and copying numbers by hand.

- **`kittest`**: 28 checks in five stages (controller, display and keypad, memory and scale, valve driver, sensors and pump), each judged against a fixed limit. The technician only does the physical actions asked for and answers the few questions only a person can (valve click, LCD readability, buzzer and LEDs). Answers on the keypad (START / STOP) or over USB (`ok` / `no` / `skip`).
- **FRAM persistence across a real power-off** is tested automatically: the test stores a token, asks for the power cycle, and resumes by itself when the board comes back.
- **Laptop script `tools/kittest.py`**: runs the test, shows only the actions, reconnects after the power cycle (the RC4 board's USB chip loses power), and saves one folder per run (`session.log`, `report.csv`, `diag.txt`, `events.csv`, `summary.txt`); exit code 0 = PASS.
- **Event log**: resets, self-test, alarms, keys, S6/S7/S8 changes, valve-rail checks, valve and pump commands, program and step boundaries, temperature bands, kit-test results, each with a millisecond time stamp (`events`, `events on`).
- **DS18B20 driver-temperature probe** (optional) on spare header J4 (PA8): shown on SENSORS and `status`, recorded per step (`steps` column `driver_max_C`), used by kit-test item 4.6 to close bench item B8 without a hand thermometer.
- **Firmware image check**: the build writes a CRC-32 of the image into its last word; the self-test recomputes it at every power-on. A mismatch raises FIRMWARE CHECK FAIL and blocks every program.
- **Board serial and boot count** in settings (`set serial`), printed on every report and in `diag`.
- **Safety**: the kit test never opens a valve while S3 reads more than 5 psig; programs cannot start during a kit test.
- **Compatibility**: the settings record keeps its v1.0/v1.1 layout (new fields use reserved bytes, checked at compile time), so calibration, logs and the dialyser table survive the upgrade.
- **Tests**: 52 simulator scenarios (6 new), including a full 28-item kit test run by a simulated technician, a run that must catch a swapped valve plug, a wrong divider resistor and a missing probe, the water interlock, an unexpected reset, a bad image and the event log.

# Diala firmware — changes from v1.0.0 to v1.1.0

Morphedo Technologies · 2026-10-18

v1.0.0 was written for main PCB RC2. v1.1.0 is the release for the RC4 board sent for fabrication on 17 Oct 2026, checked against that Gerber set, the 17 Oct BOM and manifold drawing DIALA-MAN-001.

## 1. Checked against the fabricated hardware

- **RC4 Gerbers vs. netlist.** The net of every copper pad (659) was read from the Gerber X2 attributes. RC4 is electrically identical to the netlist v1.0.0 was checked against, apart from the two extra Y1 pads below. The table is in `hw/diala_main_rc4_pads.csv` and `tools/check_pinmap.py` now checks the firmware pin map against it on every build.
- **Valve connectors.** J10–J19 = V1–V10, J20 = spare (U11 OUT3), J21 = dosing pump (Q3), confirmed from the Gerbers.
- **Cycle table vs. the Cycle Flow Walkthrough.** All 19 steps of the walkthrough match the firmware valve sets, exits and time limits.

## 2. Hardware findings

**Y1 (8 MHz crystal) will not oscillate as placed.** The footprint is the 4-pad 3225 type, but only pads 1 (HSE_IN) and 2 (HSE_OUT) are connected; pads 3 and 4 are open. On the ABM8-type crystal in the BOM, pads 1 and 3 are the crystal and pads 2 and 4 the lid, so the crystal sits between HSE_IN and an open pad. Rotating the part cannot fix it.
- Firmware: tries the crystal for about 100 ms, otherwise runs from the internal 16 MHz oscillator at the same 168 MHz. Nothing on this board needs the crystal (USB goes through the CH340C, the RTC uses Y2). `diag` and the self-test page show which clock is in use. If the crystal is used and later stops, the clock-security interrupt switches outputs off, disables the crystal and restarts on the internal clock.
- Prototypes: either leave Y1, C34, C35 unfitted, or fit Y1 and wire pad 3 to pad 2.
- Next board revision: use a crystal symbol with the ground pins (pins 1/3 crystal, 2/4 ground).

**Valve driver U10 load (bench item B8).** V1–V8 are VX223JA (SMC size 2, 7 W at 24 V, about 0.29 A each), all on U10. The connector check opens V1, V3, V4, V6, V7 together (about 1.46 A in one DRV8860), more than TI's continuous guidance for four or more channels on. Cleaning and germicide steps run four at once. v1.1.0 adds the DRV8860's built-in energise-then-hold PWM as an option (`set hold`, off by default) for use if U10 runs hot on the bench.

## 3. Bugs fixed

| v1.0.0 behaviour | Effect | v1.1.0 |
| --- | --- | --- |
| `log` sent every line with a blocking UART write | More than about 150 records took over 1.1 s: the external watchdog reset the controller, also in the middle of a cycle | Interrupt-driven 2 KB transmit buffer; long listings are produced only as fast as the UART drains. Tested: full 2,082-record dump during a running cycle, worst watchdog gap 104 ms, cycle unaffected |
| Short ENTER on the home screen selected the program | Holding ENTER for the service PIN never reached the PIN screen (except while an alarm was showing) | Only START selects a program; ENTER clears alarms and, held 3 s, opens the PIN |
| No way to change the service PIN | Default 2580 could not be changed before shipping | `set pin <nnnn>` |
| Dialyser-ID lookup read the FRAM table entry by entry | About 0.3 s main-loop stall at every ID entry and cycle end | Table held in RAM, written through to FRAM |
| Driver fault check only at the first rail-up after boot, then monitoring off permanently | A valve unplugged later was missed; one bad check disabled monitoring until reboot | Check at every rail-up; monitoring off only for that session, with a warning |
| Driver serial lines toggled while the rail (and the DRV8860 supply) was off | Unpowered chips fed through their logic pins | Lines held low while the rail is off |
| CPU fault handler looped with outputs as they were | Valves stayed as commanded until a watchdog fired (up to 1.6 s) | Rail and pump off in the handler, fault address recorded, restart, warning on the LCD |
| RTC prescaler assumed the 32.768 kHz crystal | About 2 % clock error when Y2 failed and LSI was used | Prescaler set for the source actually running |
| "VALVE TEST (1 at a time)" was 24 characters | Cut off on the 20-character LCD | Shortened; `tools/check_lcd_text.py` now checks every LCD string |

## 4. Added for start-up and commissioning

- **Power-on self-test:** FRAM, 24 V/5 V, load cell, S2, S3, valve rail, DRV8860 fault line; clock and RTC source; restart cause.
- **Service programs:** drain tank, fill tank 400 g (both report rates), prime lines, valve seal test with ports capped. The seal test covers open point 1 of the Cycle Flow Walkthrough (closed valves under reverse pressure).
- **Step telemetry** (`steps`, `trace on`) for bench item B4 (real flow rates against the PRD time limits).
- **Time-limit scale** (`set tmo 100–300`) for condition steps while flows are measured. Fixed-time steps, the leak-test window and the seal windows never change.
- **Dosing pump open-wire alarm** from the 1 Ω shunt (`set pumpcheck off` to disable).
- **Rinse-out hold warning:** time left of the 6 h disinfection hold (SAN-01).
- **`diag`** command: versions, clock/RTC source, reset cause, last fault, self-test result, settings.
- **`drv`** command: rail, nFAULT, raw DRV8860 read-back, hold-PWM read-back.
- **LCD resynchronised** every 30 s against ribbon-cable glitches.

## 5. Unchanged

Valve truth table, recipes, alarm limits, TCV and leak-test method, FRAM layout of v1.0.0 (calibration, settings, dialyser table and logs written by v1.0.0 are kept on upgrade; new settings use bytes that were reserved and zero).

## 6. Tests

46 simulator scenarios (30 from v1.0.0, 16 new). The simulator now charges the real board's blocking I/O time to the simulated clock, which is how the log-dump reset became visible.
