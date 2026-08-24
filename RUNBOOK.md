# Runbook

## Where sample sizes live

Two files per platform. Both matter.

| What | RP2040 | ESP32 |
|---|---|---|
| `N_DET` (deterministic ops) and `N_SIGN` (rejection-sampling signers) | `pico/src/bench_harness.h` | `esp32/main/bench_harness_esp32.h` |
| Per-scheme overrides (`SPX_N_*`, `FAL_N_KEYGEN`) and the `RUN_*` build switches | `pico/src/bench_config.h` | `esp32/main/bench_config.h` |

Current defaults: `N_DET = 1000`, `N_SIGN = 10000`.

Rationale: the 10k signing sample is what makes p99/p99.9 credible (the earlier
n=100 put a single sample in the p99 — the reviewer objection). Deterministic ops
sit at CV < 1%, so 1000 is already far past convergence.

**Schemes that override this**, because sample size is scaled to variance and
per-run cost rather than held flat (see `docs/CONSTANTS.md`):

| Scheme / op | n | Why |
|---|--:|---|
| FN-DSA keygen | 1000 | ~5.5 s/run on RP2040; 10k would be ~15 h for a one-time provisioning cost |
| SLH-DSA keygen | 100 (Pico) / 1000 (ESP32) | CV measured at 0.00% |
| SLH-DSA sign | 100 | CV measured at 0.00%; see the cost warning below |
| SLH-DSA verify | 10000 | cheap (74.6 ms on ESP32) |

---

## ⚠ SLH-DSA-128s signing cost — read before configuring a run

**Measured, not estimated: 78.95 s per signature on ESP32**
(`results/raw/sphincs_esp32.csv`, n=100, CV 0.00%). An earlier version of this
runbook said "~1–2 s per signature, use `N_SIGN=1000`" — that estimate was
**withdrawn on 2026-07-27** after direct measurement. It was wrong by ~50×.

Consequences of the corrected figure:

- `N_SIGN = 1000` on ESP32 would run for **21.9 hours**, not the ~30 min the old
  number implied.
- `N_SIGN = 10000` would run for **9.1 days**.
- The correct setting is `SPX_N_SIGN = 100`, which takes ~2.2 h on ESP32 and is
  already far more than the variance requires — the full 100-run range spans
  0.5 ms on a 78,950 ms mean.

Never take a per-op cost for this scheme from an estimate. Probe it.

---

## Every session starts with

```bash
get_idf                      # or: . ~/esp/esp-idf/export.sh
cd ~/Desktop/research/pqc-wsn-bench/esp32
```

## Build → flash → capture (ESP32)

```bash
idf.py build
idf.py -p /dev/cu.SLAB_USBtoUART flash
python3 ../scripts/capture_serial.py --port /dev/cu.SLAB_USBtoUART \
        --out ../results/raw/<name> --plan verify=10000,keygen=1000,sign=100
```

## Build → flash → capture (RP2040)

Paste this as ONE block. The `&&` chaining is deliberate — see the warning below.

```bash
cd ~/Desktop/research/pqc-wsn-bench/pico && \
rm -rf build && mkdir build && cd build && \
PICO_SDK_PATH=~/pico-sdk cmake .. \
    -DCMAKE_C_COMPILER="$(which arm-none-eabi-gcc)" \
    -DCMAKE_CXX_COMPILER="$(which arm-none-eabi-g++)" && \
make -j"$(sysctl -n hw.ncpu)" bench_pico && \
python3 ../../scripts/elf2uf2.py bench_pico.elf bench_pico.uf2
```

Then hold BOOTSEL, plug the Pico in, and:

```bash
cp bench_pico.uf2 /Volumes/RPI-RP2/
python3 ../../scripts/capture_serial.py --port /dev/cu.usbmodem1101 \
        --out ../../results/raw/<name> --plan verify=100,keygen=3,sign=3
```

### Building the RP2040 on Windows

Proven path — the same recipe is in the published repo's README. Run from a
terminal that has `arm-none-eabi-gcc` and `cmake` on PATH (the Arm GNU Toolchain
installer, or the Raspberry Pi "Pico setup for Windows" bundle).

```bat
cd %USERPROFILE%\Desktop\research\pqc-wsn-bench\pico
rmdir /s /q build
mkdir build && cd build

set PICO_SDK_PATH=%USERPROFILE%\pico-sdk
cmake .. -G "MinGW Makefiles" -DPICO_NO_PICOTOOL=1 ^
    -DCMAKE_C_COMPILER=arm-none-eabi-gcc ^
    -DCMAKE_CXX_COMPILER=arm-none-eabi-g++
mingw32-make -j%NUMBER_OF_PROCESSORS% bench_pico

python ..\..\scripts\elf2uf2.py bench_pico.elf bench_pico.uf2
```

Differences from the macOS recipe, and why:

| | macOS | Windows |
|---|---|---|
| generator | default (Unix Makefiles) | **`-G "MinGW Makefiles"`** |
| build tool | `make -j$(sysctl -n hw.ncpu)` | `mingw32-make -j%NUMBER_OF_PROCESSORS%` |
| picotool | used for flashing | **`-DPICO_NO_PICOTOOL=1`** — it needs libusb; skip it |
| flashing | `picotool load -x` (drag-drop is unreliable on macOS) | drag-drop **works reliably**: hold BOOTSEL, plug in, copy the `.uf2` onto the `RPI-RP2` drive |
| keep-awake | `caffeinate -i` | Settings → System → Power → screen/sleep = Never |
| serial port | `/dev/cu.usbmodem*` | `COM3`, `COM7`, … — `capture_serial.py` auto-detects by USB VID on both |

Capture is identical apart from the port name:

```bat
cd %USERPROFILE%\Desktop\research\pqc-wsn-bench
python -m pip install pyserial
python scripts\capture_serial.py --out results\raw\pico_full
```

**Cross-host note.** Building the RP2040 on a different machine from the ESP32
is fine — the two never shared a toolchain anyway (`arm-none-eabi` vs
`xtensa-esp32-elf`). What matters is that the entire RP2040 dataset comes from
ONE build, which a single-session sweep guarantees. The banner now records
`gcc <version>` alongside the build timestamp, so the compiler is on the record
in every capture.

### Three rules for the Pico build, each earned the hard way

1. **`rm -rf build` before re-configuring.** Re-running `cmake` over an existing
   cache with different variables makes CMake regenerate the cache, and during
   regeneration a *bare* compiler name is resolved relative to the build
   directory — producing `.../pico/build/arm-none-eabi-gcc is not a full path to
   an existing compiler tool`. A clean build dir avoids the whole class.
2. **Absolute compiler paths** via `$(which ...)`, for the same reason.
3. **Chain with `&&`.** Without it a failed `make` does not stop the pipeline:
   `elf2uf2.py` converts the *previous* build's ELF and `cp` flashes it. That
   silently puts the wrong benchmark firmware on the board. `scripts/elf2uf2.py`
   now refuses to convert an ELF older than the sources (override: `--force`),
   but the `&&` is the real fix.

Also: **do not paste `#` comment lines into zsh.** Interactive zsh has
`interactive_comments` off by default, so a pasted comment runs as a command
(`zsh: command not found: #`) — harmless, but it means the paste isn't doing
what the transcript suggests.

### Confirming which firmware is actually on the board

The banner printed at boot names the schemes compiled in. Open the serial port
and power-cycle; if you see `# --- FN-DSA-512 ---` when you expected SLH-DSA,
the flash didn't take.

`--plan` gives the capture script an ETA. List the expected run counts in the
order the firmware executes them.

### Letting the capture finish

The script stops after `--idle` seconds of silence. **The default is 900 s (15
min)**, sized so a single slow operation cannot be mistaken for the end of the
run — SLH-DSA signing on RP2040 produces no output for ~4.6 minutes at a time.
Do not lower it below the slowest expected single operation, or the CSV will be
truncated mid-run.

It saves `<name>.csv` (data rows) and `<name>.log` (everything, incl. `#` lines).

## Analyse

```bash
python3 scripts/analyze.py results/raw/full_esp32.csv --platform ESP32
python3 scripts/analyze.py results/raw/*_esp32.csv --platform ESP32 --md \
        > results/tables/esp32.md
python3 scripts/analyze.py results/raw/pico_*.csv --platform RP2040 --md \
        > results/tables/rp2040.md
```

Percentiles are computed offline from every run. A `—` in a percentile column
means the sample is too small for that quantile to rest on a real observation
(rule: `n*(1-q) >= 1`, so p99.9 needs n ≥ 1000). That is deliberate — it stops a
small-n maximum from being reported as an estimated tail.

## Flash footprint

```bash
bash scripts/measure_flash.sh pico     # RP2040, per-scheme delta builds
bash scripts/measure_flash.sh esp32    # ESP32
```

Writes `results/tables/flash_<platform>.md`. Method: build a baseline firmware
with every `RUN_*` off, then one build per scheme with only that scheme on, and
report the delta in text/data/bss. The delta is the honest per-scheme cost —
absolute ELF size includes the SDK, USB stack and harness, which are not
attributable to any scheme.

---

## Adding a new algorithm — checklist

1. Vendor the PQClean sources into `vendor/pqclean/`.
2. Add a component dir `esp32/components/<algo>/CMakeLists.txt`
   (copy the `mldsa44` one — each algorithm gets its OWN component so their
   identically-named `api.h` / `params.h` never collide). On the Pico side add
   an `add_pqc_lib(<algo> ...)` line to `pico/CMakeLists.txt` for the same reason.
3. Copy its `api.h` to `esp32/main/api_<algo>.h` / `pico/src/api_<algo>.h`.
4. Add `<algo>` to `REQUIRES` in `esp32/main/CMakeLists.txt`.
5. Add setup + per-op tasks in `bench_esp32.c` / `bench_pico.c` following the
   existing pattern.
6. **Probe the per-op cost with tiny n before committing to a real run.** Set
   `n = 3` for the expensive ops, flash, read the measured seconds-per-op off
   the serial log, then compute the real sample sizes from that. This step is
   not optional — it is the step that would have caught the SLH-DSA error above.
7. ESP32 only: size the task stack ~2× the expected peak, and **under ~145 KB**
   (the largest contiguous free block — bigger fails to allocate).
8. Put any long loop **last** so fast results appear early.

## Stack-measurement rules (these exist because of the arXiv-v1 bug)

The v1 paper reported wrong ML-DSA-65/87 stacks because the painted region was
smaller than the actual requirement, so the measurement silently saturated. Two
anomalies gave it away: identical numbers across different ops, and ML-DSA-87
reporting *less* than ML-DSA-65. Guards now in place:

- **Isolated task per op** — each measured op runs alone, so its high-water mark
  is only its own frames.
- **Setup is separate** — golden inputs (keys/signatures) are produced in a
  throwaway task, so setup work can't inflate a measured op. (This caught a real
  bug: verify was reporting sign's 52 KB.)
- **Saturation guard** — warns if headroom < 16 KB; the number is then suspect.
- **FreeRTOS canary** (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY`) aborts on a
  true overflow instead of returning a saturated value.
- **RP2040 painter is `always_inline`** — out-of-line codegen would put the
  painter's own frame inside the region it paints. This is a correctness
  requirement, enforced in `pico/src/bench_harness.h`.

### Sanity checks to run on EVERY new capture

- [ ] keygen / sign / verify report **three different** stack numbers
- [ ] no `*** WARNING ... SATURATED ***` and no `UNRELIABLE` in the log
- [ ] no `ERROR: could not create task` (means the stack request was too big)
- [ ] `CORRECTNESS: ... OK` / `MATCH`
- [ ] row counts match the configured n
- [ ] stack ordering is physically sensible (bigger parameters ⇒ bigger stack)

## Gotchas hit so far

| Symptom | Cause | Fix |
|---|---|---|
| `could not create task` (ESP32) | requested stack > largest contiguous free block (~145 KB) | use ≤100 KB per-op stacks |
| same failure right after a big task | FreeRTOS defers freeing a deleted task's stack to the idle task | `vTaskDelay(200ms)` between tasks (already in `bench_run_task`) |
| verify stack == sign stack | setup signing ran inside the verify task | do setup in its own task (done) |
| CSV missing after capture | script killed before the save step | let it idle-stop, or Ctrl+C once and wait |
| CSV truncated mid-run | `--idle` shorter than one slow operation | keep `--idle` above the slowest single op (default 900 s) |
| a run meant to take minutes is still going after a day | sample size set from an *estimated* per-op cost | probe with n=3 first (step 6 above) |
