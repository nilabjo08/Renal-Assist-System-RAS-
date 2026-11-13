# USB Service Console and Calibration — Diala v1.2.0

This is a simple reference for the **serial commands that actually exist in `src/app/cli.c`**. It is intended for engineers and authorized service staff, not as an operating procedure for processing medical devices.

Start with the [main README](../README.md), or use [Hardware and Pins](HARDWARE_AND_PINS.md) to understand what each sensor/valve is connected to.

> **Safety:** Some commands directly energize valves and a pump, change calibration, disable monitoring, or start a fluid-processing sequence. Only use them with an approved, isolated bench setup and appropriate hardware safeguards. A serial response of `OK` is not proof that the connected fluid path is safe.

## 1. How to connect

- **Connection:** board USB-C service connector **J3** through its CH340C USB-to-UART chip.
- **Serial settings:** **115200 baud, 8 data bits, no parity, 1 stop bit (8N1)**.
- **Tools:** a serial monitor such as Tera Term, PuTTY, or a compatible terminal.
- **Programming:** firmware flashing is a **different connection**: ST-LINK/SWD on **J2**.

At startup, the terminal prints a firmware-ready line. Then type:

```text
help
ver
status
diag
```

The terminal echoes your input. Use Enter after each command.

## 2. First commands to learn (no service unlock needed)

| Command | What it tells you or does |
| --- | --- |
| `help` | Lists the commands supported by this firmware. |
| `ver` | Firmware name, version and target board. |
| `status` | Current program/state, weight, pressures, flow, voltage, pump current, valve mask, S6/S7/S8 and active alarm. |
| `diag` | Startup self-test, reset reason, clock source, FRAM, driver faults, CRC and saved settings. |
| `time` | Reads the RTC date/time. |
| `log` | Prints saved cycle-history records. |
| `log 5` | Requests the most recent 5 records **by log position**. |
| `events 10` | Prints up to 10 recent in-memory events. |
| `events on` / `events off` | Starts/stops live event messages on the serial port. |
| `steps` | Exports measurements for the most recently recorded processing steps. |
| `dlz 12345678` | Looks up stored type, baseline TCV and use count for that dialyser ID. |
| `stop` | Requests a process stop/abort. |
| `enter` | Sends the ENTER action for a waiting cycle result/acknowledgement. |
| `kittest report` | Prints on-board kit-test results and summary. |

**RTC exception in this version:** the command `time YYYY-MM-DD HH:MM:SS` **also changes the clock without a service unlock**. Incorrect date/time can affect timestamps and the disinfection-hold display. Treat serial access as privileged physical access and review this behavior before deploying the machine.

The `log`, `events`, and `steps` commands can send several lines; the firmware streams these gradually rather than printing everything in one blocking operation.

### Understanding `status`

Example field names (your actual values will differ):

```text
state=... prog=... stage=... step=... alarm=...
weight_g=... bus_mmHg=... supply_psig=... flow_lpm=...
v24_mV=... v5_mV=... vrail_mV=... pump_mA=...
valves=0x... S8=... S6=... S7=... nFAULT=...
lines=... tmo_pct=... driver_temp_C=...
```

- `v24_mV`, `v5_mV`, and `vrail_mV` are **millivolts**.
- `pump_mA` is **milliamps**.
- `bus_mmHg` and `supply_psig` use different pressure units.
- `valves=0x...` is a bitmask for V1–V10, not a physical measurement of each valve's position.
- `nFAULT` is the DRV8860 fault input. An active fault indication requires diagnosis.
- `lines=germicide` is a saved machine state, not an independent chemical-sensor reading.

### Understanding `diag`

Focus on the following fields:

| Field | Meaning |
| --- | --- |
| `clock=HSE` / `clock=HSI` | External crystal running or internal oscillator fallback. |
| `reset=...` | Last power-on, pin/watchdog, internal watchdog, or software reset indication. |
| `self-test=...` | Startup checks; investigate failed items. |
| `fram=ok` / `FAULT` | Persistent memory health status. |
| `drv_check_failures` | Number of failed driver checks. |
| `drv_monitor=on/off` | Whether driver fault monitoring is configured to stop an active cycle. |
| `pump_check=on/off` | Whether the pump open-wire check is configured as active. |
| `image_crc=... ok` | Firmware image CRC matches the stored value. |
| `tx_dropped` | Number of serial lines/events that could not be queued. |

**Clock note:** the source documents an RC4 external 8 MHz crystal issue. The firmware can fall back to the internal oscillator. Always use `diag` and a bench measurement, not the schematic alone, to determine actual behavior.

## 3. How service unlock works

To use restricted service commands, an authorized technician enters the configured **four-digit service PIN**:

```text
unlock <your-pin>
```

If correct, the firmware replies `unlocked`. When finished, explicitly close access:

```text
lock
```

The default PIN is defined in [`src/app/config.h`](../src/app/config.h) and stored in FRAM settings. **Change the default PIN before deployment**. PIN protection on a physically accessible UART is not equivalent to a secure authenticated service interface; the code has no cryptographic session authentication.

The following sections describe commands for code understanding and controlled testing. They are **not** instructions to run a chemical reprocessing process.

## 4. Service commands — what they change

| Command syntax | Purpose and important note |
| --- | --- |
| `drv` | Reports valve rail, `nFAULT`, shift-register readback and hold-control settings. |
| `trace on` / `trace off` | Enables/disables `STEP` output after steps and `T` telemetry each second while running. Setting persists in FRAM. |
| `diag clear` | Clears stored fault diagnostics; allows the external crystal to be attempted again after the next reset. Preserve fault evidence before clearing. |
| `cal zero` | Stores the current HX711 reading as the scale zero. |
| `cal span <g>` | Uses an entered reference mass in **grams** to calculate counts per gram; requires proper zero and certified load. |
| `cal pzero` | Saves current S2/S3 pressure values as zero offsets. Must be done only under verified zero-pressure conditions. |
| `cal wf <ml_x10>` | Stores line-volume correction, in **tenths of a millilitre**. |
| `set tmo <100-300>` | Adjusts the configured timeout multiplier, as a percentage of normal. |
| `set hold <0-7>` | Changes DRV8860 hold-PWM setting; **0 = full drive**, 1–7 corresponds to 12.5% increments after ~110 ms. Applies on next valve-rail power-up. |
| `set serial <text>` | Changes the saved board serial string; maximum 11 characters and no spaces. |
| `set pin <nnnn>` | Sets a new four-digit service PIN. |
| `set monitor on/off` | Enables/disables configured driver `nFAULT` monitoring. **Do not disable to hide a fault.** |
| `set pumpcheck on/off` | Enables/disables pump open-wire checking. **Do not disable to hide a fault.** |

Calibration and setting changes call `store_save_cal()` or `store_save_settings()` and are retained in FRAM if saving succeeds. A successful save **does not** confirm that the sensor calibration is physically accurate.

### Manual output command (high-risk)

The source accepts:

```text
valve <1-10|pump|spare> <on|off>
```

It is available only after service unlock and subject to the firmware's busy/POST/kit-test checks. A valve request can energize real hardware. One output is commanded at a time through this interface.

**Critical difference:** the source's serial-command response explicitly says the **10-second auto-off applies to the LCD valve-test menu only**. There is **no equivalent serial CLI 10-second timeout** in this command. Never assume the output will switch off by itself. Use independent electrical isolation and an approved test fixture.

The output layer does check valve-rail readiness and tank-valve permission, but it is not a complete universal protection against every unsafe manual combination or wiring error.

### Starting and acknowledging programs (service only)

The parser also accepts:

```text
start <low|mid|high|preclean|disinfect|rinseout|drain|fill|prime|seal> [type 1-3] [prime_ml] [id]
strip <neg|pos>
```

These commands start machine programs or record the rinse-out test-strip result and are **not recommended as a shortcut to the validated operator workflow**. The arguments can affect recipe selection and the identifier used for history. An incorrect strip answer can change whether the firmware considers the lines to contain germicide. See [Architecture](ARCHITECTURE.md) for how these states work, and require an approved operating protocol for any physical use.

## 5. On-board kit test from the console

The firmware has **28 test items in 5 stages**. The commands are:

```text
kittest [1-5|all] [quick|long]
ok
no
skip
kittest report
kittest abort
```

- `kittest ...` and `kittest abort` require the service unlock.
- `ok`, `no`, `skip` and `kittest report` are accepted without unlock when applicable.
- The `ok`/`no`/`skip` responses are for **the current prompted test item only**; `skip` does not establish compliance.
- `quick` omits the long drift test; `long` extends that check. Inspect the per-item result, not only the summary.

For the laptop test runner, its dependencies, output files, and staging, see [Build and Testing](BUILD_AND_TESTING.md#6-automated-board--kit-test).

## 6. Where common issues are diagnosed

| Symptom | First information to inspect | Relevant code |
| --- | --- | --- |
| Nothing appears on serial | Correct J3/COM port, 115200 8N1, cable/driver, UART pins PA9/PA10 | `src/app/cli.c`, `src/bsp/stm32/bsp_stm32.c` |
| Firmware resets repeatedly | `diag` reset flags; 24 V/5 V rail; external and internal watchdog | `bsp_stm32.c`, `app.c` |
| Startup reports failure | `diag` self-test, rail voltages, sensor validity and required connectors | `post.c`, `sensors.c` |
| Valves do not energize | `status` valve mask and rail voltage; `drv` rail/`nFAULT`/readback | `outputs.c`, `bsp_stm32.c` |
| Sensor readings are wrong | `status`, saved calibration, wiring, ADC/HX711 input | `sensors.c`, `store.c` |
| Program stops unexpectedly | `status`, `events`, `log`, `steps`, active alarm and timeout | `cycle.c`, `alarms.c`, `config.h` |
| Saved data seems missing | `diag` FRAM status; software/FRAM CRC checks | `store.c` |
| Crystal status shows HSI | `diag` clock field and RC4 Y1 hardware note | `bsp_stm32.c`, `board.h` |
| Rinse-out is still required | Saved germicide flag, disinfection time, test-strip state | `cycle.c`, `store.c` |

**Do not respond to an alarm by immediately changing an alarm threshold, clearing a fault record, or disabling monitoring.** Keep the diagnostic evidence, inspect the physical cause, and retest after a reviewed correction.

## 7. Where to make code changes

| Need to change... | Look here first |
| --- | --- |
| Command names, syntax, or permissions | [`src/app/cli.c`](../src/app/cli.c) |
| Startup PIN and configured limits | [`src/app/config.h`](../src/app/config.h) |
| FRAM record layout and saved settings | [`src/app/store.h`](../src/app/store.h), [`src/app/store.c`](../src/app/store.c) |
| Calibration conversion formulas | [`src/app/sensors.c`](../src/app/sensors.c) |
| Output rules and monitored faults | [`src/app/outputs.c`](../src/app/outputs.c) |
| Valve/pump physical signals | [`src/bsp/stm32/bsp_stm32.c`](../src/bsp/stm32/bsp_stm32.c) |
| Test commands / results | [`src/app/kittest.c`](../src/app/kittest.c), [`tools/kittest.py`](../tools/kittest.py) |

When you change service code, re-run the host simulator and source check scripts, and repeat the approved hardware tests for affected functions. See [Build and Testing](BUILD_AND_TESTING.md).
