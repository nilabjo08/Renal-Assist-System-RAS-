#!/usr/bin/env python3
"""Diala kit / board test runner (firmware v1.2).

Runs `kittest` on the board over the USB service port, shows the technician only the actions to
do, and saves everything the board reports into one folder per run:

    <out>/<serial>_<YYYYMMDD-HHMM>/
        session.log   every line from the board, with laptop time stamps
        report.csv    one row per test item: item, name, status, value, limit, note
        diag.txt      `diag` output after the test
        events.csv    the last 256 events (`events`)
        summary.txt   PASS / FAIL line

Answer prompts on the keypad (START = yes / done, STOP = no) or here: y = yes, n = no, s = skip.
Exit code 0 = PASS, 1 = FAIL, 2 = could not run.

Install once:   pip install pyserial
Run:            python kittest.py --port COM5 --serial RC4-001            (Windows)
                python3 kittest.py --port /dev/ttyUSB0 --serial KIT-01 --stage 4
"""
import argparse, datetime, os, queue, sys, threading, time

try:
    import serial                     # pyserial
except ImportError:                   # pragma: no cover
    print("pyserial missing: run  pip install pyserial")
    sys.exit(2)

HINT_EN = "Answer: y = yes / done, n = no, s = skip  (or START / STOP on the keypad)"
HINT_HI = "जवाब: y = हाँ / हो गया, n = नहीं, s = छोड़ें  (या कीपैड पर START / STOP)"


class Link:
    """Serial link that survives the board being switched off (the RC4 board's CH340 loses power)."""
    def __init__(self, port, log):
        self.port, self.log, self.ser = port, log, None
        self.buf = b""

    def open(self, wait_s=180):
        t_end = time.time() + wait_s
        while True:
            try:
                self.ser = serial.Serial(self.port, 115200, timeout=0.1)
                return True
            except (serial.SerialException, OSError):
                if time.time() > t_end:
                    return False
                time.sleep(1.0)

    def send(self, cmd):
        for _ in range(3):
            try:
                self.ser.write((cmd + "\r").encode())
                self.log.write(f"{stamp()} >> {cmd}\n")
                return
            except (serial.SerialException, OSError, AttributeError):
                self.reopen()

    def reopen(self):
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        print("\n... board connection lost (power off?) - waiting for it to come back ...")
        if not self.open():
            raise SystemExit("board did not come back within 3 minutes")
        print("... connected again")

    def lines(self):
        while True:
            try:
                chunk = self.ser.read(256)
            except (serial.SerialException, OSError):
                self.reopen()
                continue
            if chunk:
                self.buf += chunk
                while b"\n" in self.buf:
                    raw, self.buf = self.buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip("\r")
                    self.log.write(f"{stamp()} {line}\n")
                    self.log.flush()
                    yield line
            else:
                yield None


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def console_answers(q):
    for raw in sys.stdin:
        a = raw.strip().lower()
        if a in ("y", "yes", "ok", "h", "haan"):
            q.put("ok")
        elif a in ("n", "no", "nahin"):
            q.put("no")
        elif a in ("s", "skip"):
            q.put("skip")
        elif a in ("abort", "q"):
            q.put("kittest abort")
        elif a:
            print("  " + HINT_EN)


def collect(link, cmd, until="end", timeout=30.0):
    """Send a command and return its output lines up to the line `until` (or a quiet period)."""
    link.send(cmd)
    out, t_end, quiet = [], time.time() + timeout, 0
    for line in link.lines():
        if line is None:
            quiet += 1
            if (until is None and quiet > 15) or time.time() > t_end:
                break
            continue
        quiet = 0
        if until and line == until:
            break
        if line.startswith(("E,", "T,")) and not cmd.startswith("events"):
            continue
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description="Run the Diala kit / board test and save the report")
    ap.add_argument("--port", required=True, help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--serial", required=True, help="board or kit serial, max 11 characters, e.g. RC4-001")
    ap.add_argument("--stage", default="all", help="1-5 or all (default all)")
    ap.add_argument("--mode", default="normal", choices=["normal", "quick", "long"], help="quick skips the scale drift test, long runs it 60 min")
    ap.add_argument("--pin", default="2580", help="service PIN")
    ap.add_argument("--out", default="kittest_results", help="folder for the results")
    a = ap.parse_args()
    if len(a.serial) > 11 or " " in a.serial:
        sys.exit("serial: max 11 characters, no spaces")

    run_dir = os.path.join(a.out, f"{a.serial}_{datetime.datetime.now():%Y%m%d-%H%M}")
    os.makedirs(run_dir, exist_ok=True)
    log = open(os.path.join(run_dir, "session.log"), "w", encoding="utf-8")
    link = Link(a.port, log)
    if not link.open(wait_s=10):
        sys.exit(f"cannot open {a.port}")

    print(f"Diala kit test - serial {a.serial}, stage {a.stage}, mode {a.mode}")
    print(f"Results folder: {run_dir}")
    print(HINT_EN)
    print(HINT_HI)
    print()

    ver = collect(link, "ver", until=None, timeout=3)
    print("Firmware:", next((v for v in ver if "firmware" in v), "no answer - check the cable and the board power"))
    unlock = collect(link, f"unlock {a.pin}", until=None, timeout=3)
    if not any("unlocked" in u for u in unlock):
        sys.exit("unlock failed: wrong PIN?")
    collect(link, f"set serial {a.serial}", until=None, timeout=3)

    answers = queue.Queue()
    threading.Thread(target=console_answers, args=(answers,), daemon=True).start()

    cmd = "kittest" + ("" if a.stage == "all" else f" {a.stage}") + ("" if a.mode == "normal" else f" {a.mode}")
    link.send(cmd)
    summary, items = None, []
    started = False
    for line in link.lines():
        while not answers.empty():
            link.send(answers.get())
        if line is None:
            continue
        if line.startswith("ERR"):
            print(line)
            if not started:
                sys.exit(2)
        elif line.startswith("KITTEST START"):
            started = True
            print(line)
        elif line.startswith("ITEM,"):
            _, code, name = line.split(",", 2)
            print(f"\n[{code}] {name}")
        elif line.startswith("PROMPT,"):
            text = line.split(",", 2)[2]
            print(f"  >>> {text}")
        elif line.startswith("K,"):
            f = line.split(",", 6)
            items.append(f)
            mark = {"PASS": "ok  ", "FAIL": "FAIL", "SKIP": "skip", "INFO": "info"}.get(f[3], f[3])
            print(f"  {mark} {f[1]} {f[2]}: {f[4]}   ({f[6] if len(f) > 6 else ''})")
        elif line.startswith("KITTEST RESUME"):
            print("  board is back on - continuing")
        elif line.startswith("SUMMARY,"):
            summary = line
        elif line.startswith("KITTEST END"):
            if "," in line:
                print("  " + line)
            break

    # after the test: report, diagnostics and the event log
    report = collect(link, "kittest report", timeout=20)
    diag = collect(link, "diag", until=None, timeout=5)
    events = collect(link, "events 256", timeout=40)
    with open(os.path.join(run_dir, "report.csv"), "w", encoding="utf-8") as f:
        f.write("record,item,name,status,value,limit,note\n")
        seen = set()
        for r in report:
            if r.startswith(("K,", "SUMMARY,")) and not r.startswith("K,item,") and r not in seen:
                seen.add(r)
                f.write(r + "\n")
    open(os.path.join(run_dir, "diag.txt"), "w", encoding="utf-8").write("\n".join(diag) + "\n")
    open(os.path.join(run_dir, "events.csv"), "w", encoding="utf-8").write("\n".join(events) + "\n")
    summary = next((r for r in report if r.startswith("SUMMARY,")), summary) or "SUMMARY,FAIL,no summary received"
    open(os.path.join(run_dir, "summary.txt"), "w", encoding="utf-8").write(summary + "\n")
    log.close()

    ok = summary.startswith("SUMMARY,PASS")
    print("\n" + "=" * 60)
    print(("RESULT: PASS" if ok else "RESULT: FAIL") + "   " + summary.split(",", 2)[2])
    print("नतीजा: " + ("पास" if ok else "फेल") + f"  — फ़ोल्डर {run_dir} इंजीनियर को भेजें")
    print("=" * 60)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
