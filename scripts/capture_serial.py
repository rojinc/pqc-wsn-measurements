#!/usr/bin/env python3
"""
capture_serial.py — record benchmark output from a Pico or ESP32 over USB serial,
with a live progress bar and ETA.

Saves TWO files:
  <out>.csv  — CSV data rows (algorithm,operation,security_level,time_us,run)
  <out>.log  — the full raw serial stream, including '#' summary/stack lines

Usage:
  python3 scripts/capture_serial.py --port /dev/cu.usbmodem1101 \
      --out results/raw/pico_falcon --plan verify=10000,keygen=1000,sign=10000

  # no --plan: still records, bar shows count + rate but no ETA
  python3 scripts/capture_serial.py --port /dev/cu.SLAB_USBtoUART --out results/raw/x

--plan lists the expected run count per operation, in the order they execute.
The ETA uses the measured rate of each operation so far; operations that haven't
started yet are estimated from the current operation's rate as a rough prior,
and re-estimated properly once they begin.

Stops automatically after --idle seconds of silence (default 20), or Ctrl+C.
"""
import argparse, sys, time, os, re, csv as csvmod

try:
    import serial
except ImportError:
    sys.exit("pyserial missing — run: pip3 install pyserial")


def fmt_dur(s):
    if s is None or s != s or s < 0:
        return "--:--"
    s = int(s)
    h, m, sec = s // 3600, (s % 3600) // 60, s % 60
    return f"{h}h{m:02d}m" if h else (f"{m}m{sec:02d}s" if m else f"{sec}s")


def check_board_matches(line, out_path, seen):
    """Warn if the firmware banner disagrees with the output filename.

    With two boards attached it is easy to capture the wrong one — especially
    with --port auto. The banner names the platform; the output name almost
    always does too. If they disagree, say so loudly and immediately rather than
    letting hours of the wrong board's data accumulate under the right filename.
    """
    if seen.get("checked"):
        return
    m = re.search(r"#\s*PQC\s+(RP2040|ESP32)\s+Benchmark Suite", line)
    if not m:
        return
    seen["checked"] = True
    board = m.group(1).lower()
    name = os.path.basename(out_path).lower()
    other = "esp32" if board == "rp2040" else "rp2040"
    hints = {"rp2040": ("pico", "rp2040"), "esp32": ("esp32", "esp")}
    if any(h in name for h in hints[other]) and not any(h in name for h in hints[board]):
        print("\n" + "!" * 68)
        print(f"!! BOARD MISMATCH: firmware says {m.group(1)}, "
              f"but --out is '{os.path.basename(out_path)}'")
        print("!! You are almost certainly capturing the wrong board.")
        print("!! Ctrl+C now, then pass --port explicitly.")
        print("!" * 68 + "\n", flush=True)


def candidate_ports():
    """Serial devices that plausibly belong to a Pico or an ESP32 dev board.

    Cross-platform. macOS/Linux name these /dev/cu.* or /dev/ttyUSB*; Windows
    uses COM3, COM7, ... so globbing /dev alone finds nothing there.

    pyserial's list_ports works on all three and reports USB VID/PID, which is a
    far better filter than a name pattern:
        2e8a  Raspberry Pi (RP2040 CDC)
        10c4  Silicon Labs CP210x   (most ESP32 dev boards)
        1a86  WCH CH340
        0403  FTDI
    Falls back to name matching if VID is unavailable.
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        import glob
        pats = ("/dev/cu.usbmodem*", "/dev/cu.SLAB_USBtoUART*",
                "/dev/cu.usbserial*", "/dev/cu.wchusbserial*", "/dev/ttyUSB*",
                "/dev/ttyACM*")
        return sorted({p for pat in pats for p in glob.glob(pat)})

    KNOWN_VID = {0x2e8a, 0x10c4, 0x1a86, 0x0403, 0x303a}   # last = Espressif USB
    NAME_HINTS = ("usbmodem", "slab_usbtouart", "usbserial", "wchusbserial",
                  "ttyusb", "ttyacm", "com")
    out = []
    for p in list_ports.comports():
        dev = p.device
        low = dev.lower()
        if p.vid in KNOWN_VID:
            out.append(dev)
        elif any(h in low for h in NAME_HINTS):
            # On Windows every serial device is COMn, including Bluetooth ones,
            # so only accept a bare COM port when it also reports a USB VID.
            if low.startswith("com") and p.vid is None:
                continue
            if "bluetooth" in low or "debug-console" in low:
                continue
            out.append(dev)
    return sorted(set(out))


def port_help():
    found = candidate_ports()
    if found:
        return ("Serial devices currently present:\n" +
                "\n".join(f"    {p}" for p in found) +
                "\nPass one with --port, or use --port auto.")
    return ("No serial devices are present at all. Checklist:\n"
            "  - Did the board finish rebooting? After copying a UF2 the Pico\n"
            "    resets and takes a second or two to re-enumerate. Retry.\n"
            "  - Is /Volumes/RPI-RP2 still mounted? If so the UF2 copy did not\n"
            "    trigger a reboot and the board is still in BOOTSEL mode.\n"
            "  - Is the cable data-capable? Charge-only USB cables enumerate\n"
            "    nothing and are the classic cause of this exact error.")


def resolve_port(requested, wait_s=15.0):
    """Return a usable device path, waiting briefly for the board to enumerate."""
    if requested != "auto":
        if os.path.exists(requested):
            return requested
        print(f"{requested} not present — waiting up to {wait_s:.0f}s for it...")
        deadline = time.time() + wait_s
        while time.time() < deadline:
            if os.path.exists(requested):
                print(f"  appeared: {requested}")
                return requested
            time.sleep(0.5)
        sys.exit(f"ERROR: {requested} never appeared.\n{port_help()}")

    deadline = time.time() + wait_s
    announced = False
    while True:
        found = candidate_ports()
        if len(found) == 1:
            print(f"Auto-detected port: {found[0]}")
            return found[0]
        if len(found) > 1:
            sys.exit("ERROR: several candidate ports; pick one with --port.\n" +
                     "\n".join(f"    {p}" for p in found))
        if time.time() > deadline:
            sys.exit(f"ERROR: no board found after {wait_s:.0f}s.\n{port_help()}")
        if not announced:
            print(f"No board yet — waiting up to {wait_s:.0f}s for it to enumerate...")
            announced = True
        time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="auto",
                    help="serial device, or 'auto' (default) to find the board. "
                         "The Pico's CDC number changes with the USB port it is "
                         "plugged into, so hard-coding it is a recurring trap.")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True, help="output path WITHOUT extension")
    ap.add_argument("--idle", type=float, default=900.0,
                    help="stop after this many seconds of silence. Must exceed "
                         "the slowest single operation — SLH-DSA signing can "
                         "run for tens of seconds with no output.")
    ap.add_argument("--plan", default="",
                    help="expected counts, e.g. verify=10000,keygen=1000,sign=10000")
    a = ap.parse_args()

    plan = {}
    order = []
    for part in filter(None, a.plan.split(",")):
        k, _, v = part.partition("=")
        k = k.strip()
        plan[k] = int(v)
        order.append(k)
    total_planned = sum(plan.values()) if plan else None

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    csv_path, log_path = a.out + ".csv", a.out + ".log"

    port = resolve_port(a.port)

    try:
        ser = serial.Serial(port, a.baud, timeout=1.0)
    except serial.SerialException as e:
        sys.exit(f"ERROR opening {port}: {e}\n{port_help()}")
    a.port = port

    print(f"Recording {a.port} @ {a.baud} -> {csv_path} (+ .log)")
    if plan:
        print("Plan: " + ", ".join(f"{k}={v}" for k, v in plan.items())
              + f"  (total {total_planned} runs)")
    print("Ctrl+C to stop.\n")

    # CSV is written INCREMENTALLY (flushed every row) so a long run can be
    # stopped or interrupted at any moment without losing collected data.
    csvf = open(csv_path, "w", newline="")
    csvw = csvmod.writer(csvf)
    csvw.writerow(["algorithm", "operation", "security_level",
                   "time_us", "run_number"])
    csvf.flush()

    rows, header = [], None
    done_total = 0                 # rows seen across all ops
    per_op = {}                    # op -> count seen
    op_start = {}                  # op -> wall time of first row
    op_last = {}                   # op -> wall time of latest row
    cur_op = None
    started = time.time()
    last_rx = started
    last_draw = 0.0

    def draw(final=False):
        nonlocal last_draw
        now = time.time()
        if not final and now - last_draw < 0.25:
            return
        last_draw = now

        if cur_op and cur_op in per_op:
            n = per_op[cur_op]
            target = plan.get(cur_op)
            el = max(1e-9, op_last[cur_op] - op_start[cur_op])
            rate = n / el if el > 0 else 0        # runs/sec
            per_run = (el / n) if n else 0

            if target:
                frac = min(1.0, n / target)
                filled = int(frac * 24)
                bar = "#" * filled + "." * (24 - filled)
                op_eta = (target - n) * per_run

                # ETA for ops not yet started: assume same per-run cost as now
                remaining = op_eta
                seen_cur = False
                for o in order:
                    if o == cur_op:
                        seen_cur = True
                        continue
                    if seen_cur:
                        remaining += plan[o] * per_run
                total_eta = remaining
                msg = (f"[{bar}] {cur_op} {n}/{target} "
                       f"({frac*100:5.1f}%)  {per_run*1000:7.1f} ms/run  "
                       f"op ETA {fmt_dur(op_eta)}  total ETA {fmt_dur(total_eta)}")
            else:
                msg = (f"{cur_op}: {n} runs  {per_run*1000:7.1f} ms/run  "
                       f"elapsed {fmt_dur(now - started)}")
        else:
            msg = f"waiting for data...  elapsed {fmt_dur(now - started)}"

        sys.stderr.write("\r\033[K" + msg[:160])
        sys.stderr.flush()

    banner_seen = {}
    with open(log_path, "w") as logf:
        try:
            while True:
                raw = ser.readline()
                if raw:
                    last_rx = time.time()
                    line = raw.decode("utf-8", "replace").rstrip("\r\n")
                    logf.write(line + "\n")
                    logf.flush()

                    if not line:
                        continue
                    check_board_matches(line, a.out, banner_seen)
                    if line.startswith("#"):
                        # summary/stack/correctness lines: show them properly
                        sys.stderr.write("\r\033[K")
                        print(line, flush=True)
                        continue
                    if line.startswith("algorithm,"):
                        header = line.split(",")
                        continue

                    parts = line.split(",")
                    if len(parts) >= 5:
                        rows.append(parts)
                        csvw.writerow(parts[:5])
                        csvf.flush()          # survive an interrupt
                        done_total += 1
                        op = parts[1]
                        if op != cur_op:
                            cur_op = op
                        t = time.time()
                        if op not in per_op:
                            per_op[op] = 0
                            op_start[op] = t
                        per_op[op] += 1
                        op_last[op] = t
                        draw()
                elif time.time() - last_rx > a.idle:
                    sys.stderr.write("\r\033[K")
                    print(f"[idle {a.idle}s — stopping]")
                    break
                else:
                    draw()
        except KeyboardInterrupt:
            sys.stderr.write("\r\033[K")
            print("\n[stopped by user]")
        finally:
            ser.close()

    draw(final=True)
    sys.stderr.write("\r\033[K")

    csvf.close()
    if rows:
        print(f"Saved {len(rows)} rows -> {csv_path}  (written incrementally)")
        for op, n in per_op.items():
            el = op_last[op] - op_start[op]
            print(f"   {op}: {n} runs in {fmt_dur(el)} ({el/max(n,1)*1000:.1f} ms/run)")
    else:
        print("No CSV rows captured (check the .log).")
    print(f"Full log -> {log_path}")
    print(f"Total elapsed: {fmt_dur(time.time() - started)}")


if __name__ == "__main__":
    main()
