# Build, Test and Commission - Diala Firmware v1.2.0

This guide separates **PC software tests**, **firmware compilation**, and **physical hardware testing**. A PASS in one category is not a substitute for the others.

> **Safety:** Hardware operations in this repository can energize valves and pumps in a medical reprocessing machine. Flashing, calibration, and commissioning must follow an approved procedure with qualified personnel, isolated test fixtures, and the correct electrical/fluid safeguards. Do not connect a patient-use setup for development testing.

## 1. Files supplied in the ZIP

| File | Purpose |
| --- | --- |
| `build/diala_fw.hex` | Flash image with memory addresses; suitable for STM32CubeProgrammer. |
| `build/diala_fw.bin` | Raw flash bytes (needs the correct base address when flashing). |
| `build/diala_fw.elf` | Firmware plus symbols for debugging. |
| `build/diala_fw.map` | Linker map for flash/RAM use and symbol placement. |
| `build/SHA256SUMS.txt` | Supplied hashes for these four build files. |
| `build/test_report.txt` | Stored report for the original release's build and test gate. |

The archive also contains source code, scripts, a linker file, board-data exports, and a `Makefile`. There is **no `vendor/` folder containing CMSIS headers**.

**Important:** A prebuilt `.hex` file and release report in the ZIP are not the same as independently rebuilding the source in a clean environment.

## 2. Fastest way to understand and test the code: PC simulator

You do **not** need the STM32 board for these tests.

### Requirements

- Linux or WSL (Windows Subsystem for Linux) is the simplest way to run the supplied `Makefile`.
- `gcc`, `make`, and `python3`.
- A compiler environment with AddressSanitizer and UndefinedBehaviorSanitizer support.

From the repository root:

```bash
make test
```

This command compiles `src/app/*.c` and `src/sim/*.c` into `build/sim`, with warnings treated as errors and runtime sanitizers enabled. It then runs **52 scenario tests**.

The simulated tests cover, among other things, recipe and sequence validation, success/failure cases, TCV and leak decisions, alarms, supply problems, power loss, FRAM records, logs, keypad behavior, startup checks, service programs, and on-board kit-test logic.

A successful run ends with:

```text
52/52 scenarios passed
```

### Run the three independent source-data checks

```bash
python3 tools/check_pinmap.py
python3 tools/check_cycle_table.py
python3 tools/check_lcd_text.py
```

| Script | What it actually verifies |
| --- | --- |
| `check_pinmap.py` | STM32 pin declarations versus the supplied RC4 pad/netlist data. |
| `check_cycle_table.py` | The **21-row main reprocessing table** against a reference truth table embedded in the script. |
| `check_lcd_text.py` | Static LCD step/menu/alarm/test strings fit the character-display limits. |

**Scope limits:** the pin checker cannot verify a soldered PCB; the table checker does not independently validate every service/disinfection path against external process documents; the LCD checker cannot prove correct physical display behavior.

## 3. Building firmware for the real STM32

### Toolchain

You need:

- ARM GNU bare-metal compiler and binary utilities (`arm-none-eabi-gcc`, `arm-none-eabi-nm`, `arm-none-eabi-objcopy`, `arm-none-eabi-size`).
- ARM embedded newlib libraries for the link flags used by the project.
- Python 3 (the CRC patch tool calls ARM binary utilities).
- GNU Make.
- CMSIS device and Cortex-M include headers at the paths specified in `Makefile`.
- `cppcheck` if you want `make check` / `make all-checks`.

For example, on a compatible Ubuntu/WSL environment:

```bash
sudo apt update
sudo apt install -y build-essential make python3 git cppcheck \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
```

The `Makefile` expects the following include directories:

```text
vendor/cmsis_device_f4/Include
vendor/CMSIS_5/CMSIS/Core/Include
```

One way to populate them is:

```bash
mkdir -p vendor
git clone --depth 1 https://github.com/STMicroelectronics/cmsis_device_f4.git vendor/cmsis_device_f4
git clone --depth 1 https://github.com/ARM-software/CMSIS_5.git vendor/CMSIS_5
```

For reproducible releases, pin these dependencies to **reviewed commit IDs** rather than relying on whatever their default branches contain later. The uploaded ZIP does not provide a dependency lockfile or commit IDs.

### Compile

```bash
make
```

The `Makefile` builds a temporary `.raw.elf`, uses `tools/patch_crc.py` to put the flash image's CRC in the final word, and produces:

```text
build/diala_fw.elf
build/diala_fw.hex
build/diala_fw.bin
build/diala_fw.map
```

The target is compiled with optimization and strict warnings for **STM32F407xx / Cortex-M4, hard-float ABI**. The `linker/stm32f407ve.ld` file sets the flash origin to `0x08000000` and the RAM origin to `0x20000000`.

### Complete local release gate

```bash
make all-checks
```

This executes the target build, simulator, three checks, and `cppcheck` for application/simulator sources. When a test fails, fix and rerun rather than using an old built image as proof that the latest source was tested.

### Check the supplied image and hashes

From the existing `build/` directory:

```bash
cd build
sha256sum -c SHA256SUMS.txt
```

This can confirm whether the four supplied files still match the original manifest. The final four bytes of `diala_fw.bin` hold the firmware's CRC-32; a corresponding check is also performed at device startup.

## 4. Physical programming interface

- **SWD debugger/programmer:** ST-LINK on the board's **J2**.
- **Power:** board-specific 24 V supply according to the existing commissioning procedure.
- **Firmware image:** normally use `build/diala_fw.hex` since it includes target addresses.
- **USB-C J3:** CH340C service serial. It is **not** the STM32 programming connection in this project.

With ST-LINK and a known safe bench setup, the project's documented CLI flashing command is:

```bash
STM32_Programmer_CLI -c port=SWD mode=UR -w build/diala_fw.hex -v -rst
```

You may alternatively use the STM32CubeProgrammer graphical application: connect using ST-LINK, select the `.hex`, program/verify, and reset. Follow [`BRINGUP.md`](../BRINGUP.md) for electrical checks and connector sequence first.

**Debugger note:** an external hardware watchdog may reset the controller if execution is halted for a long breakpoint. The code also configures an internal watchdog.

## 5. Read diagnostic information after programming

Use a serial terminal on the CH340C COM/tty device at **115200 8N1**:

```text
ver
status
diag
```

- `ver` shows the firmware version and board target.
- `status` shows current state, tank weight, pressures, flow, voltages, active valves, and switches.
- `diag` shows the controller's clock source, reset reason, power-on self-test, driver-monitor options, memory status, image CRC, and serial number.

If the board lacks some connected sensors, the startup self-test can report corresponding failures. That should be recorded, diagnosed, and resolved for the intended bench stage, not silently treated as permission for operational use.

## 6. Automated board / kit test

There are **28 on-board checks** grouped into **five stages**, implemented in `src/app/kittest.c`. The laptop interface is [`tools/kittest.py`](../tools/kittest.py).

Install its Python dependency:

```bash
python3 -m pip install pyserial
```

Example for Windows COM port:

```powershell
python tools/kittest.py --port COM5 --serial RC4-001 --stage 1 --pin YOUR_PIN
```

Example on Linux:

```bash
python3 tools/kittest.py --port /dev/ttyUSB0 --serial RC4-001 --stage 1 --pin YOUR_PIN
```

Replace the port, board serial, and PIN with the correct values. The script reads technician responses from the keyboard, while some steps also accept START/STOP on the machine keypad. `--stage all` is the default; `--mode quick` skips the long scale-drift measurement, and `--mode long` extends it.

| Stage | Tests included |
| --- | --- |
| **1 - Controller** | Firmware CRC, clock information, RTC time progress, monitored supply rails. |
| **2 - Display/keys** | Six keys, LCD readability, buzzer, and LEDs. |
| **3 - Storage/scale** | FRAM read/write and persistence, HX711, zero/span, repeatability, drift. |
| **4 - Valve driver** | Valve rail, `nFAULT`, unplug detection, individual valve/pump mapping, optional probe and heat test. |
| **5 - Sensors/pump** | Pressure ranges and open-wire detection, S6/S7/S8, flow pulses, pump current. |

Certain valve-driver test steps require RO water to be off and supply pressure below the firmware's test threshold before outputs are allowed. **Do not bypass the real machine's approved test setup and safeguards.** Some checks need physical actions or specialized test equipment; `SKIP` is not equivalent to confirming correct hardware behavior.

The kit test can store its progress in FRAM across the one intentional power-cycle check. It writes results to a folder such as:

```text
kittest_results/
`-- RC4-001_YYYYMMDD-HHMM/
    |-- session.log
    |-- report.csv
    |-- diag.txt
    |-- events.csv
    `-- summary.txt
```

The script can report `PASS` even when individual checks are **skipped**, provided no check failed. Inspect `report.csv` and apply the project's acceptance rules before signing off any hardware.

**Manual alternative:** an authorized service console can start `kittest [1-5|all] [quick|long]`, accept `ok` / `no` / `skip`, and retrieve `kittest report`. See [Service CLI](SERVICE_CLI.md).

## 7. What was actually verified for this documentation?

The following checks were executed on the supplied ZIP's source in a local Linux environment:

| Verification | Result |
| --- | --- |
| `make test` | **52 / 52 passed** |
| `python3 tools/check_pinmap.py` | **42 pins checked; no errors** |
| `python3 tools/check_cycle_table.py` | **21 rows checked; no errors** |
| `python3 tools/check_lcd_text.py` | **267 strings checked; no errors** |
| SHA-256 comparison with supplied manifest | **All 4 build files matched** |
| Binary CRC-32 comparison | **Image CRC matched (0xE7FD15C3)** |

**Not independently executed here:** a clean ARM cross-compile; `cppcheck`; programming an RC4 board; power-on self-test on the physical board; all 28 real kit-test checks; fluid-path validation; chemical/process verification; and any medical/regulatory approval. The supplied `build/test_report.txt` records an earlier successful release gate, but it is not a substitute for those missing physical checks.

## 8. Checklist before a GitHub release

- [ ] Source and binaries match a known commit and version (`FW_VERSION_STR`).
- [ ] Third-party CMSIS revisions are pinned and license obligations reviewed.
- [ ] `make all-checks` passes in a recorded clean build environment.
- [ ] The board revision, bootloader/programming method, and image hash are documented.
- [ ] Physical commissioning/bench evidence has been reviewed by a qualified engineer.
- [ ] All `[verify]` limits and process assumptions have an owner and validation record.
- [ ] No production IDs, service credentials, patient data, or sensitive logs are being published unintentionally.
- [ ] An appropriate `LICENSE` is provided if the rights owner approves redistribution.

For runtime logic go to [Architecture](ARCHITECTURE.md). For specific signals go to [Hardware](HARDWARE_AND_PINS.md).
