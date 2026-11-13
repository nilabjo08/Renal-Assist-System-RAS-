# Hardware and Pin Map - Diala Main PCB RC4

This is a readable summary of the **actual pin definitions and board drivers** in the v1.2.0 firmware. The authoritative definitions are [`src/bsp/stm32/board.h`](../src/bsp/stm32/board.h) and [`src/bsp/stm32/bsp_stm32.c`](../src/bsp/stm32/bsp_stm32.c). The matching source-package hardware data is in `hw/diala_main_rc4_pads.csv` and `hw/diala_main.net`.

> These mappings describe the RC4 design and firmware assumptions. They do not substitute for inspecting the physical board, verifying connector orientation, sensor voltage levels, or testing outputs on an isolated, current-limited bench.

## 1. Controller and communication

| Item | Used in the firmware |
| --- | --- |
| Microcontroller | STM32F407VET6, LQFP-100, Cortex-M4 |
| CPU clock | PLL running the core at 168 MHz from HSE 8 MHz if available, otherwise HSI 16 MHz |
| Flash / RAM | 512 KB flash / 128 KB SRAM defined in `linker/stm32f407ve.ld` |
| USB-C service connection | J3, via CH340C USB-to-UART chip; USART1, 115200 8N1 |
| Programming/debugging | J2, ST-LINK/SWD (not the service UART) |
| FRAM | MB85RC512T, 64 KB, bit-banged I2C on PB8/PB9; 7-bit address `0x50` |
| LCD | HD44780-style 20 x 4 character display, 4-bit bus |
| RTC | STM32 RTC, backed by coin cell, external LSE crystal if available or internal LSI fallback |
| Watchdogs | STM32 independent watchdog and external TPS3823 watchdog |

This is **not** an STM32Cube HAL application: the hardware layer uses STM32 CMSIS register definitions directly.

## 2. Ten valve outputs

The firmware uses a 10-bit mask: bit 0 means **V1**, bit 1 means **V2**, and so on up to bit 9 for **V10**. The bit positions are defined in [`src/app/config.h`](../src/app/config.h).

| Valve | Board connector | Role documented by the code |
| --- | --- | --- |
| V1 | J10 | RO water to venturi, used for vacuum/drain functions |
| V2 | J11 | RO water to common bus |
| V3 | J12 | Common bus to/from A (blood-side bottom) |
| V4 | J13 | Common bus to V (blood-side top) |
| V5 | J14 | Common bus to/from mixing tank bottom |
| V6 | J15 | A port to suction bus |
| V7 | J16 | V port to suction bus |
| V8 | J17 | DO port to suction bus |
| V9 | J18 | Common bus to/from DI port |
| V10 | J19 | Filtered air to V port |
| Spare | J20 | U11 output 3; not used by the normal program tables |
| Dosing pump | J21 | Controlled separately through pump PWM |

**Driver arrangement:** U10 DRV8860 controls V1-V8; U11 DRV8860 controls V9-V10 and the spare. They are connected as a daisy chain, and the firmware sends output bits in the appropriate order. The coil supply/valve rail is controlled separately.

The outputs are **not** direct GPIO-driven valve coils. STM32 pins PB12-PB15 transmit commands to the DRV8860 chain. The pump is driven by PWM on **PD12 (TIM4 CH1)**.

## 3. Sensor connections

| Signal | Board input | MCU pin | What the firmware reads |
| --- | --- | --- | --- |
| S2 bus pressure | J31 | PC0 / ADC channel 10 | Bus pressure, shown in mmHg |
| S3 supply pressure | J32 | PC1 / ADC channel 11 | RO supply pressure, shown in psig |
| 24 V voltage monitor | Board divider | PC2 / ADC channel 12 | Supply voltage |
| Optional S5 pH | Optional analog input | PC3 / ADC channel 13 | pH-related analog value (requires calibration/validation) |
| Valve-rail voltage monitor | Board divider | PC4 / ADC channel 14 | Actual energized valve supply |
| 5 V voltage monitor | Board divider | PA1 / ADC channel 1 | Logic supply |
| Pump current sense | Board shunt | PB0 / ADC channel 8 | Pump current, mA |
| S4 flow sensor | J33 | PA0 / TIM2 ETR | Pulses counted over a time interval |
| Load cell / HX711 | J30 | PE0 (SCK), PE1 (DOUT) | Raw HX711 counts converted into tank grams |
| S8 liquid detector | J34 | PE14 | Whether liquid is detected |
| S6 dialyser-present switch | J35 | PE15 | Whether dialyser/contact is present |
| S7 concentrate-low switch | J36 | PB10 | Concentrate-low indication |
| Optional DS18B20 | J4 | PA8 | Temperature of driver/selected location |

The pressure sensor conversions assume **0.5-4.5 V transducers behind an input divider**. S2 is configured for a `-100` to `+200 kPa` transducer range; S3 for `0` to `1200 kPa`. These ranges are marked for engineering verification in the code. **Never connect an unverified 5 V sensor output directly to a 3.3 V MCU input.**

Load-cell conversion uses a saved zero and counts-per-gram calibration. Flow uses a configurable frequency factor. The optional temperature probe uses CRC-checked 1-Wire readings and does not block ordinary firmware startup if missing.

### Switch input assumptions

The board header defines the following polarities:

| Input | Code assumption |
| --- | --- |
| S6 dialyser present | Active LOW |
| S7 concentrate low | Active LOW |
| S8 liquid detected | Active HIGH |

These values are marked **verify on bench**. If a switch reads backwards, first confirm actual wiring and connector signals rather than assuming that the software polarity is wrong.

## 4. STM32 firmware pin reference (all 42 mapped pins)

This is the compact location guide for every `PIN_*` definition in `board.h`.

### System outputs and status

| Function | MCU pin |
| --- | --- |
| Buzzer | PE2 |
| Valve power-rail request | PE3 |
| External watchdog kick | PE4 |
| Green status LED | PE5 |
| Red status LED | PE6 |
| Pump PWM (TIM4_CH1) | PD12 |

### LCD (20 x 4)

| Function | MCU pin |
| --- | --- |
| Register select `RS` | PE7 |
| Enable `E` | PE8 |
| Data `D4`, `D5`, `D6`, `D7` | PE9, PE10, PE11, PE12 |
| Backlight control | PE13 |

### Keypad

| Key | MCU pin |
| --- | --- |
| START | PC6 |
| STOP | PC7 |
| MUTE | PC8 |
| UP | PC9 |
| DOWN | PC10 |
| ENTER | PC11 |

These keys are configured with pull-ups and are read as **pressed when LOW**.

### Valve-driver interface

| Function | MCU pin |
| --- | --- |
| DRV8860 `nFAULT` | PB11 |
| Shift-register latch | PB12 |
| Serial clock | PB13 |
| Data from valve chain (MISO) | PB14 |
| Data to valve chain (MOSI) | PB15 |

### Communications and nonvolatile memory

| Function | MCU pin |
| --- | --- |
| Service UART TX / RX | PA9 / PA10 (USART1) |
| FRAM I2C SCL / SDA | PB8 / PB9 |
| DS18B20 1-Wire | PA8 |

### Analog, counters and remaining inputs

| Function | MCU pin |
| --- | --- |
| Bus pressure | PC0 |
| Supply pressure | PC1 |
| 24 V monitor | PC2 |
| Optional pH input | PC3 |
| Valve rail monitor | PC4 |
| 5 V monitor | PA1 |
| Pump current | PB0 |
| Flow pulse counter | PA0 |
| HX711 SCK / DOUT | PE0 / PE1 |
| Liquid detector | PE14 |
| Dialyser-present signal | PE15 |
| Concentrate-low signal | PB10 |

## 5. Important configured limits

The following are **firmware settings** from `src/app/config.h`, not claims that the complete machine has been validated to them.

| Check | Configured value |
| --- | --- |
| Minimum RO supply pressure | 15.0 psig |
| Low-pressure time before alarm | 3 seconds when a water-dependent step uses it |
| Empty tank target | 5 g or less |
| Tank overfill condition | Above 1,200 g |
| Connector check vacuum | -100 mmHg |
| Leak-test evacuation target | -250 mmHg |
| Leak-test measurement | 1 s settle + 20 s hold |
| Leak loss threshold: low/middle flux | 17.0 mmHg |
| Leak loss threshold: high flux | 25.0 mmHg |
| TCV reuse threshold | At least 80% of first-use baseline |
| Accepted 24 V rail monitor range | 21.0-27.5 V |
| Accepted 5 V monitor range | 4.6-5.4 V |
| Valve rail expected when enabled | At least 20 V |
| Dosing pump open-wire threshold | Under 15 mA for about 1 s while commanded on |
| Service seal-test window | 20 s per phase; 3.0 mmHg maximum measured loss per phase |

Settings like transducer range, pulse calibration, dosing-stall threshold, valve driver hold-PWM, timeouts, and dilution pulse band require controlled physical verification.

## 6. Optional temperature probe

The DS18B20 can monitor valve-driver temperature. In the source's J4 harness description:

- **J4 pin 9:** 1-Wire data (STM32 PA8)
- **J4 pin 14:** 3.3 V supply
- **J4 pin 15:** GND
- **4.7 kOhm pull-up:** between data and 3.3 V in the probe harness

One probe is expected on the bus. The firmware requests a conversion approximately once per second and reads it after about 800 ms. Temperature appears in service diagnostics, step telemetry, and relevant kit tests. Its presence does **not** replace independent thermal protection or temperature validation.

## 7. RC4 board-specific matters

1. **HSE crystal footprint issue:** the source notes an RC4 Y1 pad/wiring mismatch that may keep the 8 MHz crystal from oscillating. The firmware supports fallback to internal HSI. Check `diag` to identify the actual clock source. Correct physical hardware only through an approved board-engineering change.
2. **Valve driver loading:** U10 can run several solenoid loads at once. The code supports an optional energize-then-hold setting, but its waveform, coil hold capability, and temperature must be verified physically. The default is full drive.
3. **Driver fault flag:** `nFAULT` can indicate an open load, short, or overtemperature. A failed check at rail-up is logged and warned about; it may also disable continued fault monitoring for that rail-on session. Do not treat the warning as a passed safety check.
4. **Power-up behavior:** the firmware initializes the requested valve rail off and keeps pump PWM off until controlled by the application. The PCB's external watchdog is also involved in valve-rail permission.
5. **No patient-use conclusion:** source verification and simulated sensors cannot establish real flow paths, sterilant performance, valve polarity, liquid detection reliability, or system-level fail-safe behavior.

## 8. How to verify a pin change

A pin assignment must agree with **all three** of these:

1. Actual PCB copper and connector wiring.
2. `src/bsp/stm32/board.h` and the corresponding driver initialization in `bsp_stm32.c`.
3. The cross-check data in `hw/`.

Run:

```bash
python3 tools/check_pinmap.py
```

The check reads the supplied RC4 Gerber-derived pad CSV and netlist and compares them with the firmware definitions. In the inspected archive it reported **42 pins, 0 errors**. That proves consistency of the provided files, **not** that every physical assembly or electrical behavior has passed a bench test.
