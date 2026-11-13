# Diala — first power-up and commissioning (firmware v1.2.0, main PCB RC4)

Morphedo Technologies · 2026-10-18

Do the steps in order. Do not skip a step that fails: write down what you saw and call the engineer.
क्रम से करें। कोई स्टेप फेल हो तो आगे न बढ़ें: जो दिखा वह लिखें और इंजीनियर को बुलाएँ।

Write every reading in the results sheet at the end. / हर रीडिंग आख़िर की रिज़ल्ट शीट में लिखें।

---

## 0. What you need / क्या चाहिए

- Bench power supply 24 V with current limit / करंट लिमिट वाली 24 V बेंच सप्लाई
- Multimeter, ST-LINK programmer, USB-C cable, laptop with a serial terminal (115200) / मल्टीमीटर, ST-LINK, USB-C केबल, सीरियल टर्मिनल वाला लैपटॉप
- 500 g reference weight / 500 g का जाँचा हुआ वज़न
- Caps for ports A, V, DI, DO; bypass connector; 70 ml calibration cell / A, V, DI, DO के कैप; बाईपास कनेक्टर; 70 ml कैलिब्रेशन सेल
- Contact thermometer or IR thermometer, or a DS18B20 probe on J4 (pin 9 data, 14 = 3.3 V, 15 = GND, 4.7 kΩ pin 9–14) glued to U10 / तापमान मापने का थर्मामीटर, या J4 पर DS18B20 प्रोब

---

## 1. Board check before power / पावर देने से पहले बोर्ड जाँच

1. Look for solder bridges on U6 (MCU), U10/U11 (valve drivers) and U1 (5 V regulator). U10/U11 thermal pad must be soldered.
   U6, U10/U11 और U1 पर सोल्डर ब्रिज देखें। U10/U11 का नीचे वाला पैड सोल्डर होना ज़रूरी है।
2. **Y1 crystal:** on this board Y1 is wired to the wrong pads and will not run. Either leave Y1, C34, C35 empty, or (engineer only) solder a thin wire from Y1 pad 3 to pad 2. The firmware works either way.
   **Y1 क्रिस्टल:** इस बोर्ड पर Y1 गलत पैड पर जुड़ा है। Y1, C34, C35 खाली छोड़ दें, या (सिर्फ़ इंजीनियर) पैड 3 से पैड 2 तक पतला तार लगाएँ। फ़र्मवेयर दोनों तरह चलेगा।
3. Check polarity: C1, C15 (+ mark), D1, D2, BZ1, D3. Fit the CR2032 cell in BT1.
   पोलैरिटी जाँचें: C1, C15, D1, D2, BZ1, D3। BT1 में CR2032 सेल लगाएँ।
4. Resistance J1 + to −: must not be a short (more than 1 kΩ after the capacitors charge).
   J1 के + और − के बीच शॉर्ट नहीं होना चाहिए।

---

## 2. First power, nothing connected / पहली बार पावर, कुछ भी जुड़ा नहीं

1. Set the supply to 24 V, current limit 0.3 A. Connect to J1.
   सप्लाई 24 V, करंट लिमिट 0.3 A। J1 पर जोड़ें।
2. Current should be below 0.1 A (blank MCU). Green LED D3 on.
   करंट 0.1 A से कम होना चाहिए। हरी LED D3 जलनी चाहिए।
3. Measure: 5 V rail 4.9–5.1 V; 3.3 V (U2 and U3 outputs) 3.25–3.35 V.
   नापें: 5 V रेल 4.9–5.1 V; 3.3 V (U2, U3) 3.25–3.35 V।
4. Valve rail (J10 pin 1) must be **0 V**. If it shows 24 V, switch off and call the engineer.
   वाल्व रेल (J10 पिन 1) **0 V** होनी चाहिए। 24 V दिखे तो बंद करें और इंजीनियर को बुलाएँ।

---

## 3. Flash the firmware / फ़र्मवेयर डालें

1. ST-LINK to J2, board on 24 V. / ST-LINK को J2 पर लगाएँ, बोर्ड 24 V पर।
2. STM32CubeProgrammer → open `build/diala_fw.hex` → Download → Reset. (Or `STM32_Programmer_CLI -c port=SWD mode=UR -w build/diala_fw.hex -v -rst`.)
3. Connect USB-C (J3), open the terminal at 115200, type `ver`. Expected: `DIALA firmware 1.2.0 for main PCB RC4`.
   USB-C (J3) लगाएँ, टर्मिनल 115200 पर खोलें, `ver` लिखें। जवाब: `DIALA firmware 1.2.0 for main PCB RC4`।
4. Type `diag`. Write down the clock line (`HSI 16MHz internal` is normal on this board) and the self-test line.
   `diag` लिखें। clock वाली लाइन (`HSI 16MHz internal` इस बोर्ड पर सामान्य है) और self-test वाली लाइन लिखें।
   With no sensors connected, `self-test: FAIL S2 BUS S3 SUPPLY` is expected at this step. FRAM, 24V/5V, VALVE RAIL, VALVE DRV must not appear (LOAD CELL may or may not appear without a load cell).
   अभी सेंसर नहीं जुड़े, इसलिए S2, S3 फेल दिखना सामान्य है। FRAM, 24V/5V, VALVE RAIL, VALVE DRV फेल नहीं दिखने चाहिए।
5. Set the clock: `time 2026-10-18 10:30:00` (use the real time). / असली समय डालें।

---

## 3a. Automated board test / ऑटोमैटिक बोर्ड टेस्ट

From here on, each stage can be checked by the firmware itself. On the laptop (once: `pip install pyserial`):
अब से हर स्टेज की जाँच फ़र्मवेयर ख़ुद करता है। लैपटॉप पर (एक बार: `pip install pyserial`):

`python kittest.py --port COM5 --serial RC4-001 --stage 1`

- Do only what the screen asks; answer with y / n on the laptop or START / STOP on the keypad.
  स्क्रीन जो कहे वही करें; लैपटॉप पर y / n या कीपैड पर START / STOP से जवाब दें।
- At the end it prints PASS or FAIL and saves a folder named after the board. Send that folder to the engineer.
  आख़िर में PASS या FAIL आता है और बोर्ड के नाम का फ़ोल्डर बनता है। वह फ़ोल्डर इंजीनियर को भेजें।
- Run `--stage 1` now, `--stage 2` after step 4, `--stage 4` after step 5 (RO water OFF), `--stage 3` and `--stage 5` after step 6. Once everything is connected, `--stage all` runs all 28 checks (about 25 min including the 15 min scale drift). Items 5.1–5.3 expect the kit's potentiometers; on an RC4 machine with real sensors answer `s` (skip) to them and check the sensors with step 6.
  स्टेप 4 के बाद `--stage 2`, स्टेप 5 के बाद `--stage 4` (RO पानी बंद), स्टेप 6 के बाद `--stage 3` और `--stage 5` चलाएँ।
- The manual steps below stay as the reference for what each check does and for anything the test reports as FAIL.
  नीचे के मैनुअल स्टेप हर जाँच का संदर्भ हैं, और FAIL आने पर काम आते हैं।

## 4. LCD and keypad / LCD और कीपैड

1. Power off. Connect the LCD (J40) and keypad (J41). Power on.
   पावर बंद। LCD (J40) और कीपैड (J41) जोड़ें। पावर चालू।
2. LCD shows "DIALA REPROCESSOR / FW 1.2.0". If the screen is blank or shows boxes, turn RV1 (contrast).
   LCD पर "DIALA REPROCESSOR / FW 1.2.0" दिखे। खाली या डिब्बे दिखें तो RV1 (कॉन्ट्रास्ट) घुमाएँ।
3. Press ENTER to pass the self-test page. On the home screen UP/DOWN must change the program name. MUTE must stop the buzzer.
   ENTER दबाकर आगे जाएँ। होम स्क्रीन पर UP/DOWN से प्रोग्राम का नाम बदलना चाहिए। MUTE से बज़र बंद होना चाहिए।
4. Hold ENTER for 3 s → "SERVICE PIN". Enter 2 5 8 0 (UP to change digit, ENTER next) → "SERVICE MENU".
   ENTER 3 सेकंड दबाए रखें → "SERVICE PIN"। 2 5 8 0 डालें → "SERVICE MENU"।
5. Engineer: change the PIN now over USB: `unlock 2580` then `set pin <new 4 digits>`. Write it in the machine file.
   इंजीनियर: अभी PIN बदलें: `unlock 2580` फिर `set pin <नए 4 अंक>`।

---

## 5. Valves one by one / वाल्व एक-एक करके (bench B2)

Connect only valves, no water yet. Supply current limit 1.5 A.
सिर्फ़ वाल्व जोड़ें, अभी पानी नहीं। करंट लिमिट 1.5 A।

| Board plug | Valve | SMC model | Board plug | Valve | SMC model |
| --- | --- | --- | --- | --- | --- |
| J10 | V1 | VX223JA | J15 | V6 | VX223JA |
| J11 | V2 | VX223JA | J16 | V7 | VX223JA |
| J12 | V3 | VX223JA | J17 | V8 | VX223JA |
| J13 | V4 | VX223JA | J18 | V9 | VX215JA (small) |
| J14 | V5 | VX223JA | J19 | V10 | VX215JA (small) |

J20 = spare (leave empty), J21 = dosing pump. / J20 खाली छोड़ें, J21 = डोज़िंग पंप।

1. Plug all ten valves. Restart the board. `diag` must now show `drv_check_failures=0`. If not: one plug is loose, or bench item B1 (engineer, scope).
   दसों वाल्व लगाएँ। बोर्ड रीस्टार्ट करें। `diag` में `drv_check_failures=0` होना चाहिए। नहीं तो कोई प्लग ढीला है या B1 (इंजीनियर)।
2. Service menu → VALVE TEST. UP/DOWN choose V1…V10, ENTER switches it on for 10 s. Touch each valve: **only that valve** must click. Tick each one in the sheet.
   सर्विस मेन्यू → VALVE TEST। UP/DOWN से V1…V10 चुनें, ENTER से 10 सेकंड चालू। हर वाल्व छूकर देखें: **सिर्फ़ वही वाल्व** क्लिक करे। शीट में टिक करें।
3. Line 3 of the screen shows rail volts (about 23–24 V) and "nFLT ok".
   स्क्रीन की तीसरी लाइन पर रेल वोल्ट (लगभग 23–24 V) और "nFLT ok" दिखे।
4. If a different valve clicks: stop, write which one, call the engineer. Do not swap wires to "fix" it.
   कोई दूसरा वाल्व क्लिक करे तो रुकें, लिखें, इंजीनियर को बुलाएँ। तार बदलकर ठीक न करें।

---

## 6. Sensors / सेंसर (bench B3, B5, B6)

Power off, connect: load cell J30, S2 J31, S3 J32, S4 flow J33, S8 liquid J34, S6 dialyser switch J35, S7 concentrate switch J36. Power on, `diag` → self-test must be `PASS`.
पावर बंद करके सेंसर जोड़ें। पावर चालू, `diag` में self-test `PASS` होना चाहिए।

Service menu → SENSORS. / सर्विस मेन्यू → SENSORS।

1. **S6:** fit a dialyser in the holder → line 4 shows `DLZ`; remove → `-`. If reversed, engineer changes `DLZ_ACTIVE_LOW`.
   होल्डर में डायलाइज़र लगाएँ → `DLZ` दिखे; निकालें → `-`। उल्टा हो तो इंजीनियर बदलेगा।
2. **S7:** concentrate can empty → `LOW`; full → `ok`.
   कंसन्ट्रेट कैन खाली → `LOW`; भरा → `ok`।
3. **S8:** tube with water at the sensor → `LIQ`; dry tube → `-`.
   सेंसर पर पानी वाला ट्यूब → `LIQ`; सूखा → `-`।
4. **Scale:** empty tank → SCALE ZERO. Put the 500 g weight → SCALE SPAN → enter 0500. Remove and put back: SENSORS must read 500 ± 1 g.
   **वज़न:** खाली टैंक → SCALE ZERO। 500 g रखें → SCALE SPAN → 0500 डालें। हटाकर फिर रखें: 500 ± 1 g पढ़े।
5. **Pressure:** all ports open to air, RO water off → PRESSURE ZERO. Bus and supply must then read 0.0 ± 0.5.
   सभी पोर्ट हवा में खुले, RO पानी बंद → PRESSURE ZERO। फिर Bus और Supply 0.0 ± 0.5 पढ़ें।

---

## 7. Water connected / पानी जोड़कर

Connect RO water (regulator 20–25 psig), drain, tank, air filter. Supply current limit 3 A. In the terminal type `unlock <pin>` and `trace on`, and save the terminal output to a file from here on.
RO पानी (20–25 psig), ड्रेन, टैंक, एयर फ़िल्टर जोड़ें। करंट लिमिट 3 A। टर्मिनल में `unlock <pin>` और `trace on` लिखें, और अब से टर्मिनल का आउटपुट फ़ाइल में सेव करें।

1. Home screen line 2: supply ("Sup") must read 20–25 psig. / होम स्क्रीन पर "Sup" 20–25 psig हो।
2. **FILL TANK 400 g** (service menu). Check for leaks at every fitting. Write tank weight and g/s from the result screen.
   **FILL TANK 400 g** चलाएँ। हर फ़िटिंग पर लीक देखें। रिज़ल्ट स्क्रीन से वज़न और g/s लिखें।
3. **DRAIN TANK.** Write g/s. Tank must end at 5 g or less.
   **DRAIN TANK** चलाएँ। g/s लिखें। टैंक 5 g या कम पर रुके।
4. **PRIME LINES** with the bypass connector fitted (never with open ports — water will spray).
   **PRIME LINES** बाईपास कनेक्टर लगाकर चलाएँ (खुले पोर्ट के साथ कभी नहीं — पानी उछलेगा)।
5. **SEAL TEST:** remove the bypass, cap A, V, DI, DO. Run. Write the three numbers. PASS = each 3.0 or less. If FAIL, the screen names the valves to check (walkthrough open point 1); write it down.
   **SEAL TEST:** बाईपास हटाएँ, A, V, DI, DO पर कैप लगाएँ। चलाएँ। तीनों नंबर लिखें। हर नंबर 3.0 या कम = PASS। FAIL हो तो स्क्रीन पर लिखे वाल्व नोट करें।
6. Type `steps` after each program and save the output with the run name.
   हर प्रोग्राम के बाद `steps` लिखें और आउटपुट सेव करें।

---

## 8. Valve driver temperature / वाल्व ड्राइवर का तापमान (bench B8)

1. Cap the ports again. `start seal` three times back to back. Right after the third, measure U10 case temperature.
   पोर्ट पर कैप लगाएँ। `start seal` तीन बार लगातार चलाएँ। तीसरी के तुरंत बाद U10 का तापमान नापें।
2. Below 85 °C: OK, keep `hold` at 0. Above: engineer tries `set hold 5`, restarts, checks `drv` shows "read-back ok", and repeats the seal test — every valve must still hold (seal test must still pass).
   85 °C से कम: ठीक। ज़्यादा हो: इंजीनियर `set hold 5` करके दोबारा जाँचेगा।

---

## 9. First full cycle / पहला पूरा साइकल

Dosing pump connected (J21), concentrate in the can, 70 ml calibration cell fitted in the dialyser holder in place of a dialyser (SENSORS must show `DLZ`). With USB `valve pump on` (after unlock), SENSORS line 3 must show the pump current (more than 15 mA); then `valve pump off`.
डोज़िंग पंप (J21) जुड़ा, कैन में कंसन्ट्रेट, होल्डर में डायलाइज़र की जगह 70 ml कैलिब्रेशन सेल लगा (SENSORS में `DLZ` दिखे)।

1. Home → LOW FLUX → START. ID 00000001, priming 120 → START.
   होम → LOW FLUX → START। ID 00000001, प्राइमिंग 120 → START।
2. Watch the step name and timer on the LCD. Note any step that ends with an alarm.
   LCD पर स्टेप का नाम और समय देखें। कोई अलार्म आए तो स्टेप का नाम लिखें।
3. If a step times out only because flow is slow (`steps` shows TIMEOUT with weight still changing), engineer sets `set tmo 200` and repeats, then reports the measured times so the limits can be set (B4). Set `set tmo 100` again afterwards.
   अगर धीमे फ़्लो से स्टेप टाइमआउट हो, इंजीनियर `set tmo 200` करके दोहराएगा, बाद में `set tmo 100` वापस करेगा।
4. **WF calibration:** read TCV from the result screen. New WF = (TCV + current WF) − 70.0 ml. Service → LINE VOLUME WF, enter in ml × 10 (e.g. 68.5 ml → 0685). Run once more: TCV must read 70 ± 2 ml.
   **WF कैलिब्रेशन:** रिज़ल्ट स्क्रीन से TCV पढ़ें। नया WF = (TCV + अभी का WF) − 70.0 ml। LINE VOLUME WF में ml × 10 में डालें। दोबारा चलाएँ: TCV 70 ± 2 ml आए।
5. Remove the cell: the cycle ends. Type `log 5` and save.
   सेल निकालें: साइकल खत्म। `log 5` लिखकर सेव करें।

---

## 10. Results sheet / रिज़ल्ट शीट

| Item | Reading | OK? | Initials |
| --- | --- | --- | --- |
| Board serial / PCB no. | | | |
| 5 V / 3.3 V / 3.3 V analog | | | |
| Valve rail off at power-up (0 V) | | | |
| `ver` | | | |
| Clock source (`diag`) | | | |
| Self-test (all sensors connected) | | | |
| Service PIN changed | | | |
| V1–V10 each clicks its own valve | | | |
| `drv_check_failures` with all valves | | | |
| S6 / S7 / S8 polarity | | | |
| Scale 500 g after span | | | |
| Pressure zero | | | |
| Supply pressure psig | | | |
| Fill rate g/s, drain rate g/s | | | |
| Prime: no leaks | | | |
| Seal test windows 1 / 2 / 3 | | | |
| U10 temperature after 3 seal tests | | | |
| Full cycle: steps that timed out | | | |
| WF set to / TCV on 70 ml cell | | | |
| Files saved (`steps`, `log`, trace) | | | |
| Kit-test folders saved (stages 1–5 or all), summary | | | |
