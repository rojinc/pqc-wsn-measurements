# PQC Benchmarks on WSN-Class Hardware (RP2040 + ESP32)

On-device measurement campaign for the three finalised NIST post-quantum
signature standards, the draft FN-DSA standard, ML-KEM, and a classical ECDSA
baseline, on two constrained microcontrollers.

This repository holds the firmware, build files, capture and analysis scripts,
raw serial captures, and generated tables behind the measurement section of
*"Post-Quantum Cryptography for WSN–Blockchain Systems: A System-Level
Evaluation and Migration Strategy."*

Everything reported here is either measured on hardware, computed from measured
values and cited standards, or a documented datasheet constant.
`docs/CONSTANTS.md` lists every non-measured number in the project and where it
comes from; if a value is not on that list, it is not used in any result.

## Platforms

| Platform | Core | Clock | SRAM | FPU | Timer |
|---|---|---|---|---|---|
| RP2040 (Raspberry Pi Pico) | ARM Cortex-M0+ (dual) | 125 MHz | 264 KB | none | `time_us_32()` |
| ESP32-WROOM-32 | Xtensa LX6 (dual) | 240 MHz | 520 KB | single-precision | `esp_timer_get_time()` |

Both clocks are read from the hardware at boot and printed as `# CPU: <n> MHz`;
neither is assumed. The RP2040 runs at the pico-sdk default of 125 MHz, not the
133 MHz datasheet maximum. Both timers are 1 µs wall clock, so cycle counts in
the tables are derived (µs × MHz), not directly measured, on either platform.

Toolchains: Pico SDK 2.2.0 with `arm-none-eabi-gcc` 12.2.1; ESP-IDF v5.2.1 with
its bundled `xtensa-esp32-elf-gcc`. Both images are built at `-Os` throughout,
including the shared SHAKE/SHA-2 code that dominates every lattice and hash-based
scheme, so the optimisation level cannot flatter one platform over the other.

## Schemes

| Scheme | Standard | Role |
|---|---|---|
| FN-DSA-512 (FALCON) | FIPS 206 (draft) | compact signature, software-float sampler |
| ML-DSA-44 | FIPS 204 | lattice signature |
| SLH-DSA-128s (SPHINCS+) | FIPS 205 | hash-based signature |
| ML-KEM-512 | FIPS 203 | KEM, for gateway session-key establishment |
| ECDSA-P256 | classical | quantum-vulnerable baseline |

Post-quantum implementations are PQClean reference C (`clean` variant), from a
tree vendored at commit `3730b32a` under `vendor/pqclean/`. Only the `clean`
subtrees are retained, since they are the only ones built for either target. The
ECDSA baseline uses mbedTLS with matched configuration on both platforms; see
`docs/NOTES_build_asymmetry.md`. Reference C rather than hand-tuned assembly is
used on both sides so that a difference between the two columns reflects the
instruction set and toolchain rather than unequal optimisation effort.

P-256 stands in for secp256k1 as the classical cost baseline. The two curves have
the same 256-bit group order and the same scalar-multiplication cost class, so
the substitution affects the cost figure, not the security argument.

## Results

### Latency, mean in ms

| Scheme | Operation | n | RP2040 | ESP32 |
|---|---|--:|--:|--:|
| FN-DSA-512 | keygen | 1,000 | 5686.70 | 1386.99 |
| | sign | 10,000 | 1286.45 | 354.44 |
| | verify | 10,000 | 12.90 | 5.40 |
| ML-DSA-44 | keygen | 1,000 | 45.90 | 13.30 |
| | sign | 10,000 | 182.70 | 48.63 |
| | verify | 1,000 | 50.93 | 14.35 |
| SLH-DSA-128s | keygen | 1,000 | 26818.05 | 10383.92 |
| | sign | 100 | 208043.60 | 78892.73 |
| | verify | 10,000 | 227.45 | 75.13 |
| ML-KEM-512 | keygen | 1,000 | 11.31 | 4.41 |
| | encaps | 1,000 | 12.70 | 5.25 |
| | decaps | 1,000 | 14.84 | 6.48 |
| ECDSA-P256 | keygen | 1,000 | 106.15 | 41.53 |
| | sign | 1,000 | 116.21 | 46.84 |
| | verify | 1,000 | 409.66 | 161.74 |

Full distributions with sd, CV, min, p50, p95, p99, p99.9 and max are in
`results/tables/rp2040.md` and `results/tables/esp32.md`, computed offline from
every run by `scripts/analyze.py`.

Sample size is scaled to each operation's variance and per-run cost rather than
held constant, and the choice for each is justified in `docs/CONSTANTS.md`. The
short version: rejection-sampling and cheap operations get 10,000 runs so their
tails are well estimated; deterministic operations and slow key generations get
1,000; SLH-DSA signing gets 100, because at 78.9–208 s per signature 10,000 runs
would take 9 to 24 days and its measured CV is 0.00%. Sample sizes are identical
across the two platforms, so the columns compare like with like. Percentiles are
nearest-rank and are suppressed where `n·(1−q) < 1` rather than silently
reporting the maximum in their place. No warm-up iterations are discarded.

Two results are worth flagging. ML-DSA signing has a long tail: CV is 64% on both
platforms, and on the RP2040 the p99 is 584 ms against a 183 ms mean, with a worst
observed case of 1157 ms. FN-DSA signing is the opposite, near-deterministic at
CV 0.22%, but its *key generation* has a CV above 37% because it too rejects and
resamples until it finds a valid trapdoor. That cost is paid once at provisioning,
not per transaction.

### Peak stack, bytes

Worst-case high-water mark per operation.

Each cell is RP2040 / ESP32.

| Scheme | keygen | sign | verify |
|---|--:|--:|--:|
| FN-DSA-512 | 18,028 / 18,256 | 43,164 / 43,568 | 4,744 / 5,152 |
| ML-DSA-44 | 38,384 / 38,800 | 51,824 / 52,284 | 36,056 / 36,512 |
| SLH-DSA-128s | 3,072 / 3,632 | 2,376 / 2,912 | 1,888 / 2,336 |
| ECDSA-P256 | 848 / 1,832 | 1,032 / 2,200 | 1,064 / 1,912 |
| ML-KEM-512 | 6,240 / 6,704 | 8,864 / 9,328 (encaps) | 9,624 / 10,080 (decaps) |

**The two platforms use different measurement mechanisms, so small cross-platform
deltas are method artefacts, not results.** The RP2040 uses stack painting: the
stack region is filled with a sentinel pattern before the operation and scanned
afterwards for the furthest extent overwritten. The ESP32 uses per-operation
isolated FreeRTOS tasks and reads the task high-water mark. The task frame
accounts for the consistent 400–500 byte offset between the columns. Treat
cross-platform differences below roughly 0.5 KB as noise; within a platform the
figures are reproducible to the byte (see `docs/VALIDATION_rp2040.md`).

One further caveat: the ECDSA keygen context is `static` on the RP2040 and
stack-allocated on the ESP32, so it lands in `.bss` on one platform and in the
high-water mark on the other. Those two keygen figures are not comparable across
platforms.

### Static footprint

Per-scheme flash and static RAM deltas against a baseline firmware with every
scheme disabled are in `results/tables/flash_pico.md` and
`results/tables/flash_esp32.md`, generated by `scripts/measure_flash.sh`. Peak
stack is measured separately by the firmware and is not included in those
figures.

### Signature sizes and radio airtime

Sizes are specification constants; frame counts and airtime are computed from
them. IEEE 802.15.4 carries a 127-byte MAC payload, and a full frame occupies
4.064 ms of channel at the 2.4 GHz O-QPSK rate of 250 kbit/s.

| Scheme | Public key (B) | Signature (B) | 802.15.4 frames | Airtime floor (ms) |
|---|--:|--:|--:|--:|
| FN-DSA-512 | 897 | 666 | 6 | 24.38 |
| ML-DSA-44 | 1312 | 2420 | 20 | 81.28 |
| SLH-DSA-128s | 32 | 7856 | 62 | 251.97 |
| ML-KEM-512 | 800 | 768 (ciphertext) | 7 | 28.45 |
| ECDSA-P256 | 64 | 72 | 1 | 4.06 |

This is an **airtime floor**: it counts frame occupancy only and excludes
inter-frame spacing, CSMA/CA backoff, and retransmissions. It is a lower bound,
but a sufficient one for ranking the schemes, since all are penalised by the same
omissions. An earlier version of this project used a guessed 5 ms per frame that
folded in a CSMA allowance; that guess is withdrawn and recorded in the
corrections log in `docs/CONSTANTS.md`.

### Energy

`results/tables/*.md` report modelled computation energy as `E = P_active × t_op`
using datasheet active-power figures of 79.2 mW for the RP2040 and 224.4 mW for
the ESP32.

**These are estimates, not measurements, and there are two hard limits on how
they may be read.** Because the model is a linear rescaling of time, comparisons
between schemes on the *same* platform share the scaling constant and are sound.
Cross-platform energy comparisons are not: the two power constants are datasheet
values whose ratio (2.83) exceeds the platforms' speed ratio for some operations,
so a scheme can show higher modelled energy on the faster ESP32 than on the
RP2040. The ESP32 figure is itself the upper bound of a 30–68 mA datasheet range.
A publication-grade absolute energy claim would need shunt-resistor
instrumentation, which this campaign did not use.

**Transmission energy is deliberately not reported.** Deriving it needs a
per-frame radio power figure for a named transceiver that this campaign did not
measure, and multiplying the airtime floor by an assumed radio power would
introduce a value with no provenance. The airtime floor above is the defensible
transmission result.

## Reproducing

1. `SETUP_MACOS.md` — install and verify both toolchains, then run
   `bash scripts/check_env.sh`.
2. `RUNBOOK.md` — build, flash, and capture, per platform. It also carries the
   stack-measurement rules and the per-capture sanity checks.
3. `python3 scripts/analyze.py results/raw/pico_full.csv --md` regenerates a
   results table from a capture. The script reads the measured clock out of the
   capture's own `.log` and refuses to mix captures that disagree.

Captures are long. SLH-DSA signing alone is 208 s per run on the RP2040, and
FN-DSA key generation averages 5.7 s with a 21 s worst case.

## Layout

```
├── common/              cross-platform harness header
├── pico/                RP2040 firmware (CMake / Pico SDK)
├── esp32/               ESP32 firmware (ESP-IDF)
├── vendor/pqclean/      PQClean at commit 3730b32a, clean variants only
├── scripts/             environment check, serial capture, analysis, flash sizing
├── results/raw/         captured CSVs and full serial logs
├── results/tables/      generated tables
└── docs/
    ├── CONSTANTS.md              every non-measured number, with provenance
    ├── VALIDATION_rp2040.md      independent re-measurement against prior work
    ├── NOTES_build_asymmetry.md  build-parity corrections, and why they mattered
    └── NOTES_falcon_softfloat.md why FN-DSA signing is slow on Cortex-M0+
```

## Provenance and known limitations

`docs/CONSTANTS.md` is the place to start. It carries a dated corrections log of
every value this project got wrong and fixed, including an RP2040 clock that was
assumed rather than measured (making every prior cycle count 6.4% high), an
unsourced ESP32 power constant, a guessed radio frame time, and a percentile
indexing error. Wall-clock timings were unaffected by the clock correction, since
`time_us_32()` runs off the 1 MHz tick rather than `clk_sys`.

`docs/NOTES_build_asymmetry.md` records how a mismatched optimisation level and
a mismatched mbedTLS configuration between the two builds were found and fixed,
and confirms what was never at risk: a single vendored PQClean tree feeds both
builds, so no version skew is possible, and the ESP32 hardware AES/SHA/MPI
accelerators are reachable only through mbedTLS and therefore never touch the
post-quantum numbers.

The randomness source differs by platform: the RP2040 uses the ROSC random bit,
which its datasheet does not certify as an entropy source, and the ESP32 uses
`esp_fill_random()`. Neither is claimed to be suitable for deployment key
generation. This does not affect any timing result, because every scheme here
draws randomness only to seed a deterministic SHAKE expansion, so a biased seed
is whitened by the hash and the distribution of rejection iterations is
unchanged.

## Citing

See `CITATION.cff`.

## Licence

MIT for the harness, build files, scripts, documentation, and measurement data.
The vendored PQClean sources under `vendor/pqclean/` keep their own upstream
licences, retained in place. See `LICENSE`.
