#!/usr/bin/env python3
"""
analyze.py — turn captured benchmark CSVs into paper-ready statistics.

Percentiles are computed HERE (offline, from every run) rather than on-device,
so p95/p99/p99.9 rest on the full sample, not a device-side approximation.

Usage:
    python3 scripts/analyze.py results/raw/full_esp32.csv
    python3 scripts/analyze.py results/raw/*.csv --platform ESP32 --mhz 240
    python3 scripts/analyze.py results/raw/full_esp32.csv --md > results/tables/esp32.md

=============================================================================
CONSTANT PROVENANCE — read before quoting any energy number
=============================================================================
Every constant below is either MEASURED, DATASHEET-sourced, or DERIVED.
Nothing here is a guess. Anything not on this list is not used.

  [DATASHEET] RP2040 active current: 24 mA @ 3.3 V = 79.2 mW
      Source: RP2040 datasheet; same figure used in arXiv:2603.19340 and by
      Halak et al. (IEEE Access 2024). Constant-power upper bound.

  [DATASHEET] ESP32 active current @240 MHz, radio off (modem-sleep):
      30-68 mA @ 3.3 V = 99-224 mW. Source: ESP32 Series Datasheet
      (Espressif), "Current Consumption in Modem-sleep Mode", dual-core
      240 MHz row. We use the 68 mA UPPER BOUND (224.4 mW) so the estimate is
      conservative, matching the RP2040 methodology.
      NOTE: this is a RANGE, not a point value. Absolute ESP32 energy figures
      carry that uncertainty; RELATIVE comparisons between schemes on the same
      platform are unaffected (all scale by the same constant).

  [MEASURED] All timings, stack figures, signature lengths: from the CSVs in
      results/raw/, captured on hardware.

  [DERIVED]  Cycles = mean_us * clock_MHz / 1000. The clock is NOT assumed: it
      is read from the '# CPU: <n> MHz' line that each firmware prints at boot
      after querying the hardware (clock_get_hz(clk_sys) on RP2040,
      esp_clk_cpu_freq() on ESP32). If no capture log carries that line, the
      script says so and marks the cycle counts UNVERIFIED.
      RP2040 = 125 MHz (pico-sdk default), ESP32 = 240 MHz. 133 MHz is the
      RP2040 datasheet maximum and was wrongly used here until 2026-07-28.

Energy is E(uJ) = t(us) * P(mW)/1000, i.e. a CONSTANT-POWER model. Real draw
varies with workload (Saarinen's pqps measurements showed >50% variation across
primitives on Cortex-M4), so these are ESTIMATES. Label them as such in the
paper. Direct measurement with a shunt resistor would be required for
publication-grade absolute energy claims.
=============================================================================
"""
import argparse, csv, glob, math, statistics as st, sys, os

# [DATASHEET] see provenance block above
POWER_MW = {"RP2040": 79.2, "ESP32": 224.4}
POWER_NOTE = {
    "RP2040": "3.3V x 24mA (datasheet typical active)",
    "ESP32":  "3.3V x 68mA (datasheet modem-sleep 240MHz upper bound; range 30-68mA)",
}
# Clock is NOT a constant you may assume. Both firmwares now print the measured
# value at boot as "# CPU: <n> MHz"; analyze.py reads it from the matching .log
# and only falls back to these defaults if no log is present — with a warning.
#
# History: the RP2040 was assumed to be 133 MHz (its datasheet maximum) while
# actually running at the pico-sdk default of 125 MHz, inflating every RP2040
# cycle count by 6.4%. Wall-clock microseconds were unaffected.
CLOCK_MHZ_FALLBACK = {"RP2040": 125, "ESP32": 240}


def clock_from_logs(paths, platform):
    """Read '# CPU: <n> MHz' from the .log beside each .csv. Returns (mhz, source)."""
    import re
    found = {}
    for p in paths:
        log = os.path.splitext(p)[0] + ".log"
        if not os.path.exists(log):
            continue
        for ln in open(log, errors="replace"):
            m = re.match(r"#\s*CPU:\s*(\d+)\s*MHz", ln)
            if m:
                found.setdefault(int(m.group(1)), []).append(os.path.basename(log))
                break
    if len(found) == 1:
        return next(iter(found)), "measured on-device"
    if len(found) > 1:
        detail = "; ".join(f"{k} MHz in {', '.join(v)}" for k, v in found.items())
        sys.exit(f"ERROR: captures disagree on CPU clock ({detail}). "
                 f"Do not mix them in one table.")
    return CLOCK_MHZ_FALLBACK[platform], "FALLBACK — no '# CPU:' line in any log"


def load(paths):
    rows = {}
    for p in paths:
        with open(p) as f:
            for r in csv.DictReader(f):
                try:
                    t = int(r["time_us"])
                except (ValueError, KeyError):
                    continue
                if t <= 0:
                    continue
                key = (r["algorithm"], r["operation"], r["security_level"])
                rows.setdefault(key, []).append(t)
    return rows


def pct(sorted_v, q):
    """Nearest-rank percentile, or None when the sample cannot support it.

    Suppression rule: require n * (1 - q) >= 1, i.e. at least one observation is
    expected to fall beyond the quantile. Without this, a small sample silently
    returns its own maximum dressed up as a tail statistic -- at n = 100 the
    naive p99.9 IS the max, which is not an estimate of anything. The rule admits
    p95/p99 at n = 100 and requires n >= 1000 before p99.9 is reported at all.

    Indexing is true nearest-rank: ceil(q * n) - 1.
    """
    n = len(sorted_v)
    if n * (1.0 - q) < 1.0:
        return None
    return sorted_v[min(n - 1, max(0, math.ceil(q * n) - 1))]


def _ms(x):
    return None if x is None else x / 1000


def stats(v, mhz, power_mw):
    v = sorted(v)
    n = len(v)
    mean = st.mean(v)
    sd = st.pstdev(v) if n > 1 else 0.0
    return {
        "n": n,
        "mean_ms": mean / 1000,
        "sd_ms": sd / 1000,
        "cv": (sd / mean * 100) if mean else 0,
        "min_ms": v[0] / 1000,
        "p50_ms": _ms(pct(v, .50)),
        "p95_ms": _ms(pct(v, .95)),
        "p99_ms": _ms(pct(v, .99)),
        "p999_ms": _ms(pct(v, .999)),
        "max_ms": v[-1] / 1000,
        "kcycles": mean * mhz / 1000,
        "energy_mj": mean * (power_mw / 1000) / 1000,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csvs", nargs="+")
    ap.add_argument("--platform", default="ESP32", choices=list(POWER_MW))
    ap.add_argument("--mhz", type=int, default=None,
                    help="override the clock. Normally read from the capture's "
                         "own .log ('# CPU: <n> MHz'), which is measured on-device.")
    ap.add_argument("--md", action="store_true", help="emit a markdown table")
    a = ap.parse_args()

    paths = [p for pat in a.csvs for p in glob.glob(pat)]
    if not paths:
        sys.exit("no CSVs matched")
    if a.mhz:
        mhz, clk_src = a.mhz, "--mhz override"
    else:
        mhz, clk_src = clock_from_logs(paths, a.platform)
    pw = POWER_MW[a.platform]

    data = load(paths)
    if not data:
        sys.exit("no usable rows found")

    print(f"# Platform: {a.platform} @ {mhz} MHz  [{clk_src}]")
    if clk_src.startswith("FALLBACK"):
        print("# WARNING: clock not confirmed from any capture log. Cycle counts "
              "below are UNVERIFIED.")
    print(f"# Energy model: {pw} mW — {POWER_NOTE[a.platform]}")
    print(f"# ESTIMATE, constant-power. Not measured. See provenance block in this script.")
    print(f"# Files: {', '.join(os.path.basename(p) for p in paths)}\n")

    hdr = ["operation", "n", "mean", "sd", "CV%", "min", "p50", "p95", "p99", "p99.9", "max", "kcyc", "mJ"]

    def num(x, nd=2):
        """Format a stat, or an em dash when the sample cannot support it."""
        return "—" if x is None else f"{x:.{nd}f}"

    rows, suppressed = [], False
    for k in sorted(data):
        s = stats(data[k], mhz, pw)
        for key in ("p50_ms", "p95_ms", "p99_ms", "p999_ms"):
            if s[key] is None:
                suppressed = True
        rows.append([
            f"{k[0]}-{k[2]} {k[1]}", f"{s['n']}", num(s["mean_ms"]), num(s["sd_ms"]),
            num(s["cv"]), num(s["min_ms"]), num(s["p50_ms"]), num(s["p95_ms"]),
            num(s["p99_ms"]), num(s["p999_ms"]), num(s["max_ms"]),
            num(s["kcycles"], 0), num(s["energy_mj"]),
        ])

    if a.md:
        print("| " + " | ".join(hdr) + " |")
        print("|" + "|".join("---" for _ in hdr) + "|")
        for r in rows:
            print("| " + " | ".join(r) + " |")
    else:
        # Width each column to its widest cell (+2 gutter) so large values —
        # e.g. SLH-DSA signing at ~19 Gcycles — can never run into the next
        # column. Fixed widths used to collide here and corrupt the table.
        w = [max(len(h), *(len(r[i]) for r in rows)) + 2
             for i, h in enumerate(hdr)] if rows else [len(h) + 2 for h in hdr]
        w[0] = max(w[0], 26)
        print(f"{hdr[0]:<{w[0]}}" + "".join(f"{h:>{w[i]}}" for i, h in enumerate(hdr) if i))
        for r in rows:
            print(f"{r[0]:<{w[0]}}" + "".join(f"{c:>{w[i]}}" for i, c in enumerate(r) if i))

    print("\n# Times in ms. kcyc = mean_us * MHz / 1000. mJ = estimated, constant-power model.")
    if suppressed:
        print("# '—' = sample too small for that quantile to rest on a real observation")
        print("#       (rule: n*(1-q) >= 1; so p99.9 needs n >= 1000, p99 needs n >= 100).")


if __name__ == "__main__":
    main()
