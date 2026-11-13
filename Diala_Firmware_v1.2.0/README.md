# Diala main controller firmware v1.2.0

Morphedo Technologies · main controller PCB RC4 (STM32F407VET6) · 2026-10-18

Runs the full dialyser reprocessing cycle from PRD v0.1 on the board as fabricated (Gerber set of 17 Oct 2026, BOM issue 17 Oct 2026), against manifold DIALA-MAN-001 (valve truth table as in the Cycle Flow Walkthrough). Operator programs: low, middle and high flux, pre-clean, daily disinfection, rinse-out. Service programs: drain tank, fill tank, prime lines, valve seal test.

First power-up and commissioning: follow **BRINGUP.md** step by step. Automated board / kit test: **`kittest`** below and `tools/kittest.py`. What changed and why: **CHANGES.md**.

## Files

| Path | What |
| --- | --- |
| `build/diala_fw.hex`, `.bin` | Image to flash |
| `build/diala_fw.elf`, `.map` | Debug image and linker map |
| `build/test_report.txt` | Output of the release gate (`make all-checks`) |
| `build/SHA256SUMS.txt` | Checksums of the four build files |
| `src/app/` | Application: sequencer (`cycle.c`), power-on self-test (`post.c`), sensors, outputs and interlocks, alarms, storage, LCD UI, USB CLI |
| `src/bsp/stm32/` | Board support for RC4: pin map (`board.h`), drivers, clock, fault handlers, startup |
| `src/sim/` | Host simulator: plant model and 46 scenario tests |
| `tools/kittest.py` | Laptop script: runs the automated test, shows the technician only the actions, saves the report folder |
| `tools/` (checks) | `check_pinmap.py` (firmware pins vs. RC4 Gerbers and netlist), `check_cycle_table.py` (step table vs. PRD truth table), `check_lcd_text.py` (every LCD string fits 20 characters), `patch_crc.py` (writes the image CRC at build time) |
| `hw/diala_main_rc4_pads.csv` | Every copper pad of the fabricated RC4 board and its net, read from the Gerbers |
| `hw/diala_main.net` | Schematic netlist (agrees with the Gerbers pad for pad) |

## Flash the board

1. ST-LINK V2/V3 on J2 (10-pin 1.27 mm SWD), board powered from 24 V.
2. Any one of:
   - STM32CubeProgrammer: open `build/diala_fw.hex`, Download, Reset.
   - `STM32_Programmer_CLI -c port=SWD mode=UR -w build/diala_fw.hex -v -rst`
   - `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/diala_fw.elf verify reset exit"`
3. LCD: "DIALA REPROCESSOR / FW 1.2.0 / main PCB RC4 / Self test...", then the home screen or the self-test result page.
4. Service port: USB-C (J3), 115200 8N1. Type `help`.

Debugging note: the external watchdog U4 (TPS3823) resets the MCU about 1.1–1.6 s after the CPU stops kicking it. When the debugger halts the core, expect a reset; connect under reset (`mode=UR`) and avoid long breakpoints.

## Rebuild and check (Ubuntu 24.04)

```
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi cppcheck python3
git clone --depth 1 https://github.com/STMicroelectronics/cmsis_device_f4 vendor/cmsis_device_f4
git clone --depth 1 https://github.com/ARM-software/CMSIS_5 vendor/CMSIS_5
make all-checks
```

`make all-checks` is the release gate and must end without errors: target build with warnings as errors; 46 simulator scenarios; pin map against the RC4 Gerbers; step table against the PRD; LCD text lengths; cppcheck.

Release result: 34.9 KB flash (6.7 %), 15.1 KB RAM (11.5 %), 0 warnings; 46/46 scenarios; 41 firmware pins / 659 pads, 0 errors; 21/21 steps; 200 LCD strings; 0 cppcheck findings.

The simulator charges the time the real board spends in blocking I/O (FRAM over bit-banged I²C, LCD writes, ADC) to the simulated clock, so watchdog gaps and loop stalls in the tests are realistic. Worst loop stall measured: 12 ms; worst watchdog gap during a full 2,082-record log dump in a running cycle: 104 ms (the TPS3823 trips at 1,120 ms minimum).

## Operator use

- **Home:** UP/DOWN choose the program, START selects it. ENTER clears an alarm; ENTER held 3 s opens the service PIN.
- **Patient programs:** enter the 8-digit dialyser ID (UP/DOWN change a digit, ENTER next). A new ID asks for the priming volume (25–250 ml); a known ID shows its baseline TCV and use count. Confirm with START.
- **During a cycle:** STOP aborts to the safe state at any time; MUTE silences the buzzer.
- **End of a patient cycle:** PASS/FAIL, TCV (ml and % of baseline) and leak loss stay on screen while the dialyser vents every 10 s. Removing the dialyser ends the cycle.
- **Rinse-out:** the confirm screen shows "HOLD LEFT xhyym" if the 6 h disinfection hold (SAN-01) is not over.

## Automated board / kit test (`kittest`)

The firmware tests the board itself, judges every result against a fixed limit, and reports. The technician only does the physical actions it asks for (place a weight, flip a switch, set a potentiometer) and answers the few questions only a person can (did only V3 click? are the LCD rows clear?).

Run it from the laptop: `python kittest.py --port COM5 --serial RC4-001 [--stage 1-5] [--mode quick|long]`. The script needs `pip install pyserial`. It writes a folder `<serial>_<date-time>/` with `session.log`, `report.csv`, `diag.txt`, `events.csv`, `summary.txt`, and exits 0 on PASS, 1 on FAIL. Without the script: `unlock <pin>`, `set serial <name>`, `kittest [1-5|all] [quick|long]`, answer with START / STOP on the keypad or `ok` / `no` / `skip` over USB, then `kittest report`.

| Stage | Items (limit) |
| --- | --- |
| 1 Controller | 1.1 image CRC (matches build) · 1.2 clock source (info) · 1.3 RTC advances 2–3 s in 2.1 s · 1.4 24 V and 5 V in limits |
| 2 Display, keypad | 2.1 all six keys within 30 s each · 2.2 LCD rows clear (technician) · 2.3 buzzer, green, red (technician) |
| 3 Memory, scale | 3.1 FRAM read-back · 3.2 FRAM keeps its record over a power-off (the test resumes by itself) · 3.3 load-cell samples · 3.4 zero noise ≤ 1.0 g (zero saved) · 3.5 span 100–5000 counts/g (span saved) · 3.6 0 g and 500 g within 1.0 g, three times · 3.7 drift ≤ 2.0 g over 15 min (`long` 60 min, `quick` skips) |
| 4 Valve driver | 4.1 rail ≥ 20 V within 1 s · 4.2 nFAULT high after the fault reset · 4.3 V2 unplugged is seen, then clears · 4.4 each of V1–V10 and the pump drives only its own load (technician) · 4.5 DS18B20 answers (SKIP if not fitted) · 4.6 driver ≤ 85.0 °C after 5 × 20 s with V1 V3 V4 V6 V7 V9 on |
| 5 Sensors, pump | 5.1 S2 at 0.50 / 1.83 / 4.50 V reads −750 / 0 / +1500 mmHg within 40 · 5.2 S2 at 0 V flagged · 5.3 S3 at 0.50 / 1.00 / 4.50 V reads 0 / 21.8 / 174 psig within 1 / 1 / 3 · 5.4–5.6 S6, S7, S8 closed then open · 5.7 75 Hz reads 10.0 ± 0.3 L/min · 5.8 pump ≥ 15 mA |

Safety: no valve is opened while S3 reads more than 5 psig; the test asks for RO water to be turned off and waits. Patient and service programs cannot start while a kit test runs. Stage 3 changes the scale calibration (zero and span are saved), which is what a new board needs anyway. A reset during any item other than 3.2 ends the test with that item failed.

Report line format: `K,<item>,<name>,<PASS|FAIL|SKIP|INFO>,<value>,<limit>,<note>`, then `SUMMARY,<PASS|FAIL>,…,serial <name>`. Progress and results are kept in FRAM (0x0140), so `kittest report` shows the last run even after a power-off.

## Event log

Every reset, self-test result, alarm, key press, S6/S7/S8 change, valve-rail check, valve and pump command, program start and end, step end, driver-temperature band change and kit-test result is recorded with a millisecond time stamp. `events [n]` prints the last n (up to 256, held in RAM since power-on); `events on` streams each one as it happens (always on during a kit test). Format: `E,<seconds since power-on>,<event>,<detail>`.

## Driver temperature probe (optional)

A DS18B20 on the spare header J4: data to J4 pin 9 (PA8), 3.3 V to pin 14, GND to pin 15, 4.7 kΩ between pins 9 and 14 (in the harness). Glue or clamp the probe on U10. The firmware reads it every second; it appears on the SENSORS screen, in `status`, as the `driver_max_C` column of `steps`, as TEMP events, and is used by kit-test item 4.6. Without the probe everything else works and item 4.6 is skipped.

## Service menu (PIN, default 2580 — change it with `set pin`)

SENSORS · VALVE TEST (one output at a time, auto-off 10 s, shows rail voltage and nFAULT) · DRAIN TANK · FILL TANK 400 g · PRIME LINES · SEAL TEST (capped) · SCALE ZERO · SCALE SPAN · PRESSURE ZERO · LINE VOLUME WF · SET CLOCK · DRIVER MONITOR · SELF TEST RESULT · CLEAR LOGS · EXIT

Service programs run on the same sequencer as the patient cycle with every interlock in force, are logged, and record step telemetry.

| Program | Valves | Ends when | Shows |
| --- | --- | --- | --- |
| DRAIN TANK | V1 V3 V5 V6 | tank ≤ 5 g (120 s) | tank weight, drain rate g/s |
| FILL TANK 400 g | V2 V5 | tank ≥ 400 g (45 s) | tank weight, fill rate g/s |
| PRIME LINES | V2 V4 V6 · V2 V3 V7 · V2 V9 V8, 30 s each | time | — (dialyser or bypass must be fitted) |
| SEAL TEST | evacuate V1 V3 V4 V6 V7 V9 to −250 mmHg, then hold 20 s each with V3 V4 V9 · V3 V9 · V9 | time | loss per window, PASS if each ≤ 3.0 mmHg, and which valves to check |

Seal-test reading: window 1 high only → V7 / V10 or the V cap; window 2 high, 3 low → V6 or the A cap; window 3 high → V2 / V5 or the DI cap.

## USB commands

Open: `help ver status diag log [n] steps events [n] time [YYYY-MM-DD HH:MM:SS] dlz <id> stop enter unlock <pin> lock`, and `ok` / `no` / `skip` / `kittest report` during or after a kit test

After `unlock <pin>`:
`kittest [1-5|all] [quick|long]` · `kittest abort` · `events <on|off>` · `set serial <name>` · `valve <1-10|pump|spare> <on|off>` · `drv` · `trace <on|off>` · `diag clear` · `cal zero | cal span <g> | cal pzero | cal wf <ml_x10>` · `start <low|mid|high|preclean|disinfect|rinseout|drain|fill|prime|seal> [type] [prime_ml] [id]` · `strip <neg|pos>` · `set tmo <100-300>` · `set pumpcheck <on|off>` · `set monitor <on|off>` · `set pin <nnnn>` · `set hold <0-7>`

- `steps` prints the last run as CSV: step, valves, end reason (exit / time / TIMEOUT / ABORT), duration, tank weight at start and end, S2 min/max, mean supply pressure, mean inlet flow.
- `trace on` streams a `STEP,...` line at each step end and a `T,...` line every second while a program runs. Capture with any serial terminal into a file.
- `log` and `steps` never block: lines are sent only as fast as the UART drains, while the machine keeps running.

## Power-on self-test

Runs after every reset while the splash is shown (about 0.5 s): firmware image CRC (a mismatch blocks every program: reflash) · FRAM write/read-back · 24 V and 5 V · load cell samples · S2 and S3 in range · valve rail switched on with every output off, must reach 20 V · DRV8860 nFAULT must release after the fault reset · rail off again. Also reported: clock source (crystal or internal), RTC source (Y2 or internal), and whether the last restart came from a CPU fault or the watchdog. Failures raise the matching alarm; a FRAM failure blocks all programs because baseline TCV and the log could not be kept.

## Calibrate before first use

1. Scale: empty tank → SCALE ZERO; known weight (e.g. 500 g) on the tank → SCALE SPAN, enter the weight.
2. Pressure: all ports open to air → PRESSURE ZERO.
3. Line volume WF: fit the 70 ml calibration cell, run a cycle, then WF = (measured TCV + current WF) − 70 ml, entered in ml × 10 (70.0 ml → 700).
4. Clock: SET CLOCK, or `time 2026-10-18 10:30:00` over USB.

## Must be confirmed on the bench

| # | Item | How | Where to change |
| --- | --- | --- | --- |
| B1 | DRV8860 special-command waveform (fault reset; also used by the hold PWM) | Scope LATCH/CLK at rail-up; `diag` must show no driver-check failure with all ten valves plugged in | `DRV_SPECIAL_START_HIGH` in `bsp_stm32.c` |
| B2 | Valve map | Service VALVE TEST: V1–V10 each click their own valve (J10–J19) | `bsp_valve_write()` |
| B3 | Polarity of S6 dialyser-present, S7 concentrate-low, S8 liquid | SENSORS screen while operating each one | `LIQ_ACTIVE_HIGH`, `DLZ_ACTIVE_LOW`, `CONC_ACTIVE_LOW` in `board.h` |
| B4 | Real flow rates against step time limits | FILL / DRAIN rates, then a full cycle with `trace on`; `set tmo 200` while measuring | step `max_ms` in `cycle.c` |
| B5 | S2 range −100…+200 kPa (part pending approval) | Compare with a reference gauge | `S2_KPA_MIN/MAX` in `config.h` |
| B6 | Load-cell counts per gram | Replaced by SCALE SPAN | service menu |
| B7 | LCD timing through 74HCT541, buzzer, keypad wiring | Visual | `lcd_init()` |
| B8 | Valve driver load: V1–V8 are VX223JA (size 2, 7 W, ≈0.29 A) on U10; the connector check has five on at once | Kit-test item 4.6 with the DS18B20 on U10. If above 85 °C: `set hold 5` (62.5 % after 110 ms), confirm `drv` shows "read-back ok", repeat 4.6, and check every valve still holds at full supply pressure | `set hold`, `DRV_ENERGISE_CODE` |
| B9 | Seal-test pass limit 3.0 mmHg per 20 s | Run SEAL TEST on known-good valves several times | `SEAL_LIMIT_X10` in `config.h` |
| B10 | Pump open-wire threshold 15 mA | Read pump mA on the SENSORS screen with the chosen pump | `PUMP_OPEN_MA`, or `set pumpcheck off` |
| B11 | Crystal Y1 (see CHANGES.md) | `diag` shows the clock source | Hardware |

Step times the PRD left open (unchanged from v1.0.0): clean through membrane up to 60 s, ending at half the batch drawn; disinfection 150 g through each of A, V and dialysate paths plus 100 g cross path; rinse-out two tank fill-and-flush passes then 60 s per path; dilute steps pulse V2 (200 ms on / 400 ms off) in the last 30 g.

## Design notes

- **Step tables:** each program is a table of steps (valves, exit condition, maximum time, timeout alarm). The tables are validated at every boot; if validation fails the firmware drives no output.
- **Interlocks in one place (`outputs.c`):** no valve without the rail (PCB-03); V5 only in steps declared to use the tank and never in a rinse (SAF-02); V1 and V2 never together.
- **Valve driver check at every rail-up:** after the fault reset with all outputs off, nFAULT must be high. If not (a valve plug open, or B1), the program continues with nFAULT monitoring off for that session and a warning is shown.
- **Driver lines held low while the rail is off,** so the unpowered DRV8860s are never fed through their logic pins.
- **Safe state:** any fault turns every output off at once, then vents or drains where safe, and latches the alarm (FW-03). A CPU fault switches the valve rail and pump off in the fault handler itself, records where it happened, and restarts.
- **Watchdogs:** TPS3823 and IWDG kicked only when the sequencer and UI both ran (FW-04).
- **Storage (FRAM):** calibration and settings in two CRC-protected copies (settings also hold the board serial and boot count); 256-entry dialyser table (also held in RAM, so lookups never stall the loop); 2,082-record cycle log; power-loss record; self-test scratch area; kit-test record.
- **Image check:** the build writes the CRC-32 of the flash image into its last word (`tools/patch_crc.py`); the firmware recomputes it at every power-on.
