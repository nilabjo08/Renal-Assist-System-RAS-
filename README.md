# Diala Firmware v1.2.0

**Firmware for the Diala dialyser-reprocessing machine**  
**Controller:** STM32F407VET6 | **Board:** Diala main PCB RC4 | **Language:** C11

This repository contains the software that reads the machine's sensors, operates its valves and dosing pump, guides the user through a 20 x 4 LCD, and records what happened during each processing cycle. It also includes a PC simulator and an automated hardware test utility.

> **Engineering / safety notice:** This is firmware for equipment used in a medical reprocessing workflow. The included simulation tests are not proof of physical performance, reprocessing efficacy, or regulatory/clinical approval. Do not use it for patient-related processing until the complete machine, process, sensor calibration, and safety systems have been independently validated and formally approved.

## 1. What is inside?

| Part | Simple explanation |
| --- | --- |
| **Main controller** | An STM32F407 runs the firmware without an operating system (bare metal). |
| **Processing sequence** | A step-by-step program decides which valves/pump should run and when to advance. |
| **Sensors** | Read tank weight, pressure, water flow, liquid detection, presence switches, supply voltage, and pump current. |
| **User interface** | A 20 x 4 character LCD and six buttons for programs, status, alarms, and service functions. |
| **Safety and diagnostics** | Startup checks, time limits, alarms, output checks, watchdogs, and recovery handling. |
| **Storage** | External FRAM saves calibration, settings, dialyser history, and cycle results across resets. |
| **Testing** | A PC simulator, three source/hardware cross-check scripts, and a 28-item on-board kit test. |

## 2. How it works (simple view)

```text
Power ON
   |
   v
Initialize board + load saved settings
   |
   v
Check firmware image, sensors, memory and valve driver
   |
   v
Show home screen / faults on LCD
   |
   v
Choose a program -> enter required details -> START
   |
   v
Read sensors -> check safety -> run the current step
   |                  |
   |                  +--> If unsafe: stop outputs, alarm, recover if safe
   v
Step finished? -> move to next step -> log the result
   |
   v
Finish program -> show result -> wait for required acknowledgement
```

The application checks the process at **10 ms intervals**, refreshes the LCD at **100 ms intervals**, and services the watchdog when the main work is progressing. See [Architecture and firmware flow](Diala_Firmware_v1.2.0/docs/ARCHITECTURE.md) for the detailed explanation.

## 3. Available programs

| Program | What it is for |
| --- | --- |
| **Low flux** | Full reprocessing sequence using the low-flux recipe. |
| **Middle flux** | Same main sequence, with a middle-flux recipe. |
| **High flux** | Same main sequence, with a high-flux recipe. |
| **Pre-clean** | Runs only the early cleaning/rinsing stages. |
| **Disinfect** | Machine disinfection using a bypass setup (not a fitted dialyser). |
| **Rinse-out** | Flushes the machine after disinfection and requires a test-strip result. |

Four additional **service programs** are implemented: drain tank, fill tank, prime lines, and valve seal test. Service actions must be performed only under an approved bench/commissioning procedure.

The detailed steps, measurement rules, program states, and recipe lookup are explained in [Architecture and firmware flow](Diala_Firmware_v1.2.0/docs/ARCHITECTURE.md).

## 4. Main hardware

- **MCU:** STM32F407VET6, Cortex-M4, 512 KB flash, 128 KB SRAM.
- **Outputs:** 10 solenoid valves (V1-V10), one dosing pump, a buzzer and two status LEDs.
- **Valve driver:** Two chained DRV8860 devices, plus a controlled valve-power rail.
- **Sensors:** HX711 load-cell interface; S2 bus pressure; S3 supply pressure; S4 flow pulses; S6/S7/S8 switches; monitored power rails; optional S5 pH and DS18B20 temperature input.
- **Display and buttons:** HD44780-compatible 20 x 4 LCD and START, STOP, MUTE, UP, DOWN, ENTER.
- **Memory:** MB85RC512T 64 KB I2C FRAM; RTC with battery backup.
- **Connections:** SWD programming through J2 and 115200-baud USB-C service serial through J3 (CH340C).

For **V1-V10 roles, connectors, and the STM32 pin map**, see [Hardware and pin map](Diala_Firmware_v1.2.0/docs/HARDWARE_AND_PINS.md).

## 5. Project structure

```text
Renal-Assist-System-RAS-/
|-- README.md                 # Main README (this file)
`-- Diala_Firmware_v1.2.0/
    |-- BRINGUP.md            # Hardware commissioning procedure
    |-- CHANGES.md            # Firmware revision history
    |-- Makefile              # Firmware build and checks
    |-- src/
    |   |-- app/              # Machine logic, UI, alarms, logging and tests
    |   |-- bsp/              # Board support and STM32 drivers
    |   `-- sim/              # PC simulation and scenario tests
    |-- tools/                # Source checks, image CRC and kit-test runner
    |-- linker/               # STM32F407VE memory layout
    |-- hw/                   # Board netlist and RC4 pad data
    |-- build/                # Supplied firmware binaries and test report
    `-- docs/
        |-- ARCHITECTURE.md
        |-- HARDWARE_AND_PINS.md
        |-- BUILD_AND_TESTING.md
        `-- SERVICE_CLI.md
```

**Where should I look in the code?**

The following code paths are relative to `Diala_Firmware_v1.2.0/`.

| Want to understand or modify... | Begin with... |
| --- | --- |
| Startup and main loop | `src/bsp/stm32/main.c`, `src/app/app.c` |
| Process stages, valve combinations, exit rules | `src/app/cycle.c`, `src/app/cycle.h` |
| Recipe selection and quantities | `src/app/recipes.c` |
| Limits, time intervals, valve names | `src/app/config.h` |
| Sensor reading and calibration math | `src/app/sensors.c`, `src/app/store.h` |
| Valve/pump control and interlocks | `src/app/outputs.c`, `src/bsp/stm32/bsp_stm32.c` |
| LCD screens and keypad actions | `src/app/ui.c` |
| Alarms and startup diagnostics | `src/app/alarms.c`, `src/app/post.c` |
| USB serial commands | `src/app/cli.c` |
| Saved settings, historical results | `src/app/store.c` |
| Automated on-board checks | `src/app/kittest.c`, `tools/kittest.py` |
| Simulator tests | `src/sim/sim_main.c`, `src/sim/bsp_sim.c` |

For how the files call each other, use the [file-by-file architecture guide](Diala_Firmware_v1.2.0/docs/ARCHITECTURE.md).

## 6. Run software tests on a PC

On Linux or WSL with `gcc`, `make`, and `python3` installed, open a terminal at the **repository root**. Enter the firmware folder first:

```bash
cd Diala_Firmware_v1.2.0
make test
python3 tools/check_pinmap.py
python3 tools/check_cycle_table.py
python3 tools/check_lcd_text.py
```

`make test` compiles a simulated version of the firmware and runs the test scenarios **without requiring the Diala board**. The other commands verify the recorded pin map, the 21-step main sequence, and LCD text lengths.

To build a flashable image, install the ARM GNU toolchain and the required CMSIS header repositories first. **The uploaded firmware ZIP does not include its `vendor/` directory.** See [Build, testing, and commissioning](Diala_Firmware_v1.2.0/docs/BUILD_AND_TESTING.md) before running `make` or `make all-checks`.

## 7. Program or connect to an actual board

Inside `Diala_Firmware_v1.2.0/`, the supplied `build/diala_fw.hex` and `build/diala_fw.bin` are existing firmware images. For a controlled bench setup, programming is via **ST-LINK/SWD at J2**, not through the CH340C serial port. The USB-C service connection at **J3** is used for commands and diagnostics.

After appropriate electrical checks and programming, connect a serial terminal at **115200 baud, 8 data bits, no parity, 1 stop bit (8N1)**. Useful read-only commands:

```text
ver
status
diag
help
```

See [Service console and calibration](Diala_Firmware_v1.2.0/docs/SERVICE_CLI.md) for all supported commands. For physical first power-up and connection order, follow the existing [BRINGUP.md](Diala_Firmware_v1.2.0/BRINGUP.md) with qualified engineering supervision; this README is **not** a replacement for that procedure.

## 8. Verification status of this archive

The supplied release report (`Diala_Firmware_v1.2.0/build/test_report.txt`) states that the release gate passed. A fresh inspection of the uploaded source also produced:

| Check | Result |
| --- | --- |
| Host simulator | **52 / 52 scenarios passed** |
| MCU pin-map cross-check | **42 pins, 0 errors** |
| Main-sequence cross-check | **21 steps, 0 errors** |
| LCD-text cross-check | **267 strings, 0 errors** |
| Supplied `.bin` image CRC-32 | **Matches its final CRC word** |
| Supplied image SHA-256 list | **Matches the four supplied build files** |

These results do **not** establish a successful new ARM build or physical bench/clinical verification. The uploaded archive lacks CMSIS vendor headers; the current review environment also lacks `arm-none-eabi-gcc` and `cppcheck`.

## 9. Before publishing on GitHub

- Keep the repository's **real source code**, not only the `.hex` file.
- Review the default service PIN, firmware/manual valve commands, build artifacts, board details, and any test reports before making a repository public.
- The supplied ZIP contains **no `LICENSE` file**; obtain the owner's decision before selecting a license or making redistribution claims.
- Keep versions and release results accurate. The old README bundled in the ZIP contains earlier test-count figures; use the v1.2.0 results above.
- Treat values marked `[verify]` in `src/app/config.h` and unfinished bench sign-offs as **not yet validated**.

## More documentation

- [Architecture and firmware flow](Diala_Firmware_v1.2.0/docs/ARCHITECTURE.md) - how each module works, step sequencing, states, recipes, logging, safety checks, and where to edit code.
- [Hardware and pin map](Diala_Firmware_v1.2.0/docs/HARDWARE_AND_PINS.md) - valves, sensors, interfaces, pins, supply details, and board-specific concerns.
- [Build, testing, and commissioning](Diala_Firmware_v1.2.0/docs/BUILD_AND_TESTING.md) - PC tests, toolchain, flashing, kit test, and what remains to verify.
- [Service console and calibration](Diala_Firmware_v1.2.0/docs/SERVICE_CLI.md) - commands, examples, saved values, and diagnostic troubleshooting.
- [Existing revision history](Diala_Firmware_v1.2.0/CHANGES.md) and [existing board bring-up](Diala_Firmware_v1.2.0/BRINGUP.md) - detailed source-package reference documents.
