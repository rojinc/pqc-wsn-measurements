# Finding: build asymmetry between the RP2040 and ESP32 firmwares

**Status:** all causes resolved 2026-07-28. The captures in `results/raw/` were
taken on the fixed builds. This note is retained because the corrections it
records are material to how the published figures should be read.

## How it surfaced

Normalising latency by clock (cycles = µs × MHz) instead of comparing wall time
puts every operation on one axis. Doing that exposes ECDSA as the sole outlier:

| Operation | M0+ Mcyc | ESP32 Mcyc | M0+/ESP32 |
|---|--:|--:|--:|
| ML-KEM-512 decaps | 1.77 | 1.57 | 1.13× |
| ML-KEM-512 keygen | 1.24 | 1.07 | 1.16× |
| SLH-DSA-128s sign | 27,541.96 | 18,948.11 | 1.45× |
| ML-DSA-44 sign | 23.69 | 11.58 | 2.05× |
| FN-DSA-512 sign | 192.09 | 85.02 | 2.26× |
| **ECDSA-P256 sign** | **11.56** | **14.46** | **0.80×** |
| **ECDSA-P256 verify** | **40.08** | **47.76** | **0.84×** |

Every PQC operation lands in 1.13–2.26×. ECDSA is the only pair below 1.0 — the
M0+ apparently doing *less work per cycle*'s worth of time than a 240 MHz
superscalar-ish Xtensa, which is not physically plausible for the same algorithm
in the same library. That is a build artefact, not a microarchitectural result.

## Root cause 1 — optimisation level (FIXED)

The ESP32 build is uniformly `-Os`: every component sets it explicitly and the
project default is `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`.

The RP2040 build was not. CMake's `Release` default is `-O3`, and only the
*per-algorithm* PQClean libraries carried an explicit `-Os`. Everything else
inherited `-O3`:

| Target | Was | Contains |
|---|---|---|
| `mlkem512`, `mldsa44`, `falcon512`, `sphincs128s` | `-Os` ✅ | scheme code |
| `pqclean_common` | **`-O3`** | `fips202.c` (SHAKE/Keccak), `sha2.c` (SHA-256) |
| `bench_pico` | **`-O3`** | harness |
| `pico_mbedtls` | **`-O3`** | ECDSA / RSA |

So the shared hash layer was compiled at a *different* level from the schemes
calling into it — inconsistent within the RP2040 build, not merely against the
ESP32 one. Since SHAKE and SHA-256 dominate the runtime of every lattice and
hash-based scheme here, this flatters the RP2040 in proportion to how
hash-dominated each scheme is.

The measured ratios are consistent with that mechanism: the most hash-dominated
scheme (ML-KEM, 1.13×) sits at the bottom of the range and the most
arithmetic-dominated one (FN-DSA signing — emulated FP inside `falcon512`, which
was `-Os` on both sides — 2.26×) at the top.

**Fix applied:** `pico/CMakeLists.txt` now forces `CMAKE_C_FLAGS_RELEASE` to
`-Os -DNDEBUG` and sets `-Os` on `pqclean_common` explicitly. The whole RP2040
image now matches the ESP32.

## Root cause 2 — mbedTLS configuration (RESOLVED 2026-07-28)

**Decision: match the configurations.** The classical baseline is held to the
same standard as the PQC schemes — identical algorithm, identical options, no
hardware acceleration on either side.

| Setting | RP2040 | ESP32 | Action |
|---|---|---|---|
| `ECP_NIST_OPTIM` | ON | ON | already matched |
| `ECP_FIXED_POINT_OPTIM` | absent → **ON** | ON | enabled on RP2040 |
| `ECDSA_DETERMINISTIC` | absent | ON → **OFF** | disabled on ESP32 |
| `HARDWARE_MPI` | n/a | ON → **OFF** | disabled on ESP32 |

Both platforms now sign with a randomised `k` supplied by their own RNG
callback (`pico_rng` / `esp_rng`), both already present at every call site.
The previous configuration is recorded in the table above.

`CONFIG_MBEDTLS_HARDWARE_SHA` remains enabled on the ESP32 and is **not** in any
measured path: the message hash is computed once during setup, outside every
timed region; with `ECDSA_DETERMINISTIC` off the `md_alg` argument to
`mbedtls_ecdsa_write_signature` is unused, so no SHA runs inside the signing
loop; and PQClean compiles its own `sha2.c` rather than calling mbedTLS at all.

The prior state, for the record:



| Setting | RP2040 | ESP32 | Effect |
|---|---|---|---|
| `ECP_NIST_OPTIM` | ON | ON | matched ✅ |
| `ECP_FIXED_POINT_OPTIM` | absent | ON | speeds base-point mult (keygen/sign) on ESP32 only |
| `ECDSA_DETERMINISTIC` | absent | ON | **different algorithm**: RFC 6979 HMAC-DRBG k-derivation vs randomised k |
| `HARDWARE_MPI` | n/a | ON | ESP32 bignum accelerator; sized for RSA-scale operands, plausibly a net loss at 256 bits |

`ECDSA_DETERMINISTIC` is the serious one — it means the two platforms are not
running the same signing procedure at all.

Note that `ECP_FIXED_POINT_OPTIM` being ON only on the ESP32 works *against* the
observed anomaly: it should make the ESP32 faster on keygen and sign. With
configurations matched, the residual gap may therefore be larger, not smaller.

Verify is the useful control: it involves no `k` derivation, so its 19% gap is
attributable to optimisation level and/or `HARDWARE_MPI`, while signing's larger
25% gap is consistent with deterministic-ECDSA overhead on top.

## Root cause 3 — the RP2040 clock was assumed, not measured (FIXED)

`bench_harness.h` hardcoded 133 MHz in both the boot banner and the cycle
formula (`_mean * 133.0 / 1000.0`), and `analyze.py` hardcoded the same value.
No code in this project ever called `set_sys_clock_khz()`, so the part ran at the
**pico-sdk default of 125 MHz**. 133 MHz is the RP2040 datasheet *maximum*.

`CONSTANTS.md` recorded the value as "confirmed on-device at boot". Nothing
confirmed it — the banner was a `printf` string literal.

- **Every RP2040 cycle count was 6.4% too high** (133/125).
- Every cycle-normalised cross-platform ratio inherited that error, in the
  direction that made the M0+ look *worse* per cycle than it is.
- **Wall-clock timings were never affected.** `time_us_32()` derives from the
  1 MHz timer tick, independent of `clk_sys`. Every millisecond figure in every
  table stands.

Fixed: the harness now reads `clock_get_hz(clk_sys)` and prints
`# CPU: <n> MHz (measured: clk_sys = <n> Hz)`; `analyze.py` parses that line out
of the capture's own `.log` and refuses to mix captures that disagree, warning
loudly when no measured value is available.

The ESP32 harness had always read `esp_clk_cpu_freq()` at runtime. This brings
the RP2040 to the same standard.

## Three smaller asymmetries (all fixed)

1. **Different test messages.** The two platforms signed different strings —
   124 bytes on RP2040, 118 on ESP32. Both fit inside one SHAKE256 block, so the
   timing effect was unmeasurable, but a controlled comparison should not have
   varied its input. Now one identical vector, with a comment in both files.
2. **README misdescribed the ESP32 timer** as `CCOUNT`, implying directly
   measured cycles. It is `esp_timer_get_time()`, a 1 µs wall clock — the same
   class as the RP2040's `time_us_32()`. Cycles are derived on both platforms.
3. **ECDSA keygen context placement.** `static` on RP2040, stack-allocated on
   ESP32, so the ~300-byte context lands in `.bss` on one platform and in the
   stack high-water mark on the other. ECDSA *keygen stack* figures (936 B vs
   1.8 KB) are therefore not directly comparable across platforms.

## Verified sound — no action needed

- **PQC code paths are identical.** Same PQClean `clean` variant, same function
  calls with the same arguments, setup outside the timed region on both
  platforms, for ML-KEM, ML-DSA, FN-DSA and SLH-DSA alike.
- **ECDSA API calls are identical too** (`mbedtls_ecdsa_genkey` /
  `write_signature` / `read_signature`, hash precomputed outside the loop). The
  ECDSA discrepancy is entirely configuration, not divergent code.
- **All nine capture CSVs pass integrity checks** — run numbers complete from 1
  to n, no gaps, no duplicates.
- **Stack measurements are insensitive to the optimisation change**: re-running
  ML-KEM at `-Os` moved the figures by 4 bytes (6228/8860/9620 vs
  6232/8864/9624) — one word of harness frame. Stack results stand.
- `MBEDTLS_ECP_NIST_OPTIM` is ON for both platforms.

## What was never at risk

- **PQClean source provenance.** A single vendored tree at commit `3730b32a`
  feeds both builds. No version skew is possible — this is the strongest part of
  the setup and it is worth stating in Methodology.
- **ESP32 hardware AES/SHA/MPI do not touch the PQC numbers.** PQClean compiles
  its own `sha2.c` and `fips202.c`; the accelerators are reachable only through
  mbedTLS, i.e. only by the classical baseline.
- **The FN-DSA finding.** The Cortex-M0+ signing time is a within-platform
  absolute measurement, and `falcon512` was `-Os` on both sides throughout, so
  the optimisation mismatch never touched it. See `NOTES_falcon_softfloat.md`.
- **RP2040-vs-published validation.** Same platform on both sides of that
  comparison.
- **All three ESP32 captures.** The ESP32 build was internally consistent.

## What is affected

Every RP2040 capture taken before 2026-07-28: `pico_main.*`, `pico_falcon.*`.
Any cross-platform ratio derived from them, including the "M0+/Xtensa gap is
2.0–3.7×, scheme-dependent" statement in `docs/VALIDATION_rp2040.md`, and the
SLH-DSA 2.62× ratio recorded in `pico/src/bench_config.h` — whose stated
explanation ("SHA-256 suits the M0+") is confounded by this and is withdrawn.

## Outcome

Every platform was rebuilt with the fixes above and recaptured from scratch. The
captures in `results/raw/` (`pico_full.*`, `esp32_full.*`) and the tables in
`results/tables/` come from those fixed builds; nothing in them predates
2026-07-28. The measured effect of the optimisation change on the most
hash-dominated scheme, ML-KEM-512 on the RP2040, was keygen +20.3%, encaps
+17.0%, decaps +12.1%. That is material, which is why the affected runs were
redone rather than documented as a caveat.

## Methodological note for the paper

Cross-platform comparison requires matched *build* configuration, not just
matched source. State explicitly: same PQClean commit, same optimisation level,
no hardware acceleration in the PQC path on either platform, and — for the
classical baseline — either matched mbedTLS settings or an explicit statement of
which platform-idiomatic configuration was used and why.
