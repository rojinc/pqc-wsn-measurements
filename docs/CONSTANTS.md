# Constants and their provenance

Every non-measured number used anywhere in this project, and where it comes
from. If a value is not on this list, it is not used in any result.

Tags: **[M]** measured on hardware · **[D]** datasheet/standard ·
**[C]** computed from [M]/[D] · **[A]** assumption (flag in the paper)

---

## Platform

| Constant | Value | Tag | Source |
|---|--:|:--:|---|
| RP2040 clock | **125 MHz** | M | pico-sdk default `clk_sys`. Read on-device via `clock_get_hz(clk_sys)` and printed at boot as `# CPU: <n> MHz`. **Was previously recorded as 133 MHz** — the datasheet *maximum*, not the configured clock. Nothing in this project calls `set_sys_clock_khz()`, so the part runs at the SDK default. Any capture without a `# CPU:` line in its log predates this check and its cycle counts are unverified |
| RP2040 SRAM | 264 KB | D | RP2040 datasheet |
| ESP32 clock | 240 MHz | M | Read on-device via `esp_clk_cpu_freq()`, printed at boot as `# CPU: 240 MHz`. Matches `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240` |
| ESP32 SRAM | 520 KB | D | ESP32-WROOM-32 datasheet |

## Randomness source (differs by platform — disclose in Methodology)

| Platform | Source | Tag | Notes |
|---|---|:--:|---|
| RP2040 | ROSC `randombit`, 8 reads per byte (`pico/src/randombytes_pico.c`) | D | The RP2040 datasheet states the ring-oscillator random bit is **not** a certified entropy source and that consecutive samples may correlate |
| ESP32 | `esp_fill_random()` (ESP-IDF hardware RNG) | D | Espressif's RNG; entropy quality is adequate once RF or the SAR ADC is active |

> **Why this does not affect any timing result.** Every scheme measured here
> draws randomness only to seed a deterministic expansion. PQClean's ML-DSA
> derives its rejection-sampling nonce stream by hashing the seed through SHAKE,
> ML-KEM expands its matrix through SHAKE from the seed, and FN-DSA seeds its
> sampler the same way. A biased or correlated seed is whitened by the hash, so
> the distribution of rejection iterations — and hence the CV and tail
> statistics that carry our conclusions — is unaffected. The cost of the
> `randombytes` call itself is ≤ 32 bytes per operation (≈ 256 ROSC reads on
> RP2040), which is under 0.1 % of the fastest operation measured.
>
> **State this in Methodology anyway.** The two platforms do not use the same
> RNG, and a reviewer is entitled to ask. Neither source is claimed to be
> suitable for key generation in deployment; both are benchmarking harnesses.

## Power / energy

| Constant | Value | Tag | Source |
|---|--:|:--:|---|
| RP2040 active current | 24 mA @ 3.3 V → 79.2 mW | D | RP2040 datasheet. Same model used in arXiv:2603.19340 and Halak et al. (IEEE Access 2024) |
| ESP32 active current, radio off | **30–68 mA** @ 3.3 V → 99–224 mW | D | ESP32 Series Datasheet, "Current Consumption in Modem-sleep Mode", dual-core 240 MHz. **This is a range.** We use the 68 mA upper bound (224.4 mW) to stay conservative |
| Energy | E = t × P | C | Constant-power model |

> **Known limitation.** The constant-power model ignores workload-dependent
> draw. Saarinen's *pqps* measurements showed >50% variation in average power
> across cryptographic primitives on Cortex-M4. Absolute energy values here are
> **estimates**; relative comparisons between schemes on the same platform are
> robust (identical scaling constant). Publication-grade absolute energy claims
> require shunt-resistor measurement.
>
> A previous version of `analyze.py` used 264 mW for the ESP32 — an unsourced
> value above the datasheet maximum. Corrected to 224.4 mW.

## Radio (IEEE 802.15.4) — for Table IV / Tx time

| Constant | Value | Tag | Source |
|---|--:|:--:|---|
| Max PHY payload | 127 B | D | IEEE 802.15.4 aMaxPHYPacketSize |
| Data rate (2.4 GHz O-QPSK) | 250 kbit/s | D | IEEE 802.15.4-2020 |
| Airtime for a full 127 B frame | **4.06 ms** | C | 127 × 8 / 250,000 = 4.064 ms — pure airtime, preamble/SFD excluded |
| Frame count | ⌈(sig + payload) / 127⌉ | C | Frame-count method used in the paper |
| Inter-frame / CSMA-CA overhead | **not modelled** | A | Deployment-specific (backoff, ACKs, retries). Any per-frame overhead must be stated explicitly if added |

> An earlier on-spend calculation in this project used "≈5 ms/frame" including
> a guessed CSMA allowance. That guess is **withdrawn**. Use the computed
> 4.06 ms airtime and state CSMA separately, or measure it.

## Threat-model constants (not measured here; sourced from the literature)

| Constant | Value | Tag | Source |
|---|--:|:--:|---|
| T_crqc | 540 s (9 min) | A | Babbush et al. (Google Quantum AI) resource **estimate** — not a demonstrated result. Everything calibrated to it inherits that uncertainty |
| T_wake, 802.15.4 | 10–600 s | D | duty cycle 0.1–1% |
| T_wake, LoRaWAN Class A | up to 1800 s | D | ETSI 1% duty-cycle sub-band ceiling |
| T_block, Hyperledger Fabric | ~2–5 s | D | Raft ordering, default endorsement |
| T_block, Ethereum PoS | ~12 s | D | single-slot head-block latency |

## Sample sizes and why they differ

| Scheme / op | n | Rationale |
|---|--:|---|
| ML-DSA-44 sign | 10,000 | CV ≈ 67% (rejection sampling). At n=100 the mean is off by up to 26.6% and p99 rests on one sample. **[M]** — the published n=100 p99 (489.9 ms) proved 24% low against n=10,000 (609.1 ms) |
| FN-DSA-512 sign/verify | 10,000 | Cheap enough to run; confirms CV ≈ 0.3% |
| Deterministic ops (keygen, verify, encaps, decaps) | 1,000 | CV < 1.5%; converged |
| FN-DSA keygen | 1,000 | ~4 s/run on RP2040; 10k = 11 h for a one-time provisioning cost |
| SLH-DSA-128s sign | 100 | **[M]** 78.95 s/run on ESP32 → 10k = 9.1 days. No rejection sampling. **Confirmed after the fact: CV = 0.00%** (sd = 0.12 ms on a 78,950 ms mean — the full 100-run range spans 0.5 ms). n=100 is already vastly more than needed |
| SLH-DSA-128s keygen | 1,000 | **[M]** 10.40 s/run → ~2.9 h |
| SLH-DSA-128s verify | 10,000 | **[M]** 78.5 ms/run → ~13 min |

> **Methodological justification for unequal n:** sample size is scaled to
> variance and per-run cost, not held flat. Demonstrated empirically on our own
> data — resampling the FN-DSA sign distribution (CV = 0.32%), n = 100 estimates
> the n = 10,000 mean to within 0.124%; the same test on ML-DSA (CV = 67%) is off
> by up to 26.6%. State this in Methodology; it is defensible and pre-empts the
> sample-size objection.

---

## Corrections log

| Date | Item | Was | Now |
|---|---|---|---|
| 2026-07-27 | ESP32 power | 264 mW (unsourced) | 224.4 mW (datasheet upper bound) |
| 2026-07-27 | 802.15.4 frame time | ~5 ms (guessed) | 4.06 ms computed airtime; CSMA not modelled |
| 2026-07-27 | SLH-DSA sign cost | ~1–2 s (unverified estimate) | 78.95 s measured |
| 2026-07-28 | **RP2040 clock** | 133 MHz, hardcoded in the harness string and in the kc formula, documented as "confirmed on-device" when nothing confirmed it | **125 MHz**, read via `clock_get_hz(clk_sys)` and printed at boot. All prior RP2040 **cycle** counts were 6.4% high. Wall-clock µs unaffected — `time_us_32()` runs off the 1 MHz tick, not `clk_sys` |
| 2026-07-28 | **RP2040 optimisation** | `-O3` on `pqclean_common` (SHAKE/SHA-256), harness and mbedTLS, while scheme libs were `-Os`; ESP32 uniformly `-Os` | `-Os` throughout, matching ESP32. Measured effect on ML-KEM: keygen +20.3%, encaps +17.0%, decaps +12.1% |
| 2026-07-28 | Test message | different string on each platform (124 B vs 118 B) | one identical vector on both. Timing effect unmeasurable (both inside one SHAKE256 block) but the comparison was not controlled |
| 2026-07-28 | ESP32 timer, as documented | README claimed `CCOUNT` (direct cycle counter) | `esp_timer_get_time()` — 1 µs wall clock, same class as the RP2040's `time_us_32()`. Cycles are derived on both platforms, not measured on either |
| 2026-07-28 | Percentile indexing | `int(q·n)` | nearest-rank `ceil(q·n) − 1`. Shifts tail figures by one observation: RP2040 ML-DSA sign p99 609.13 → 608.95 ms, p99.9 867.83 → 861.79 ms; RP2040 FN-DSA keygen p99 12307.40 → 12111.27 ms; ESP32 ML-DSA sign p99 160.33 → 160.30 ms. No conclusion changes |
| 2026-07-28 | Small-n percentiles | p99.9 printed at any n | suppressed unless `n·(1−q) ≥ 1`. At n=100 the old p99.9 was just the maximum relabelled |
| 2026-07-28 | RP2040 SLH-DSA cost | projected 3.5× ESP32 (band 3.0–4.0×) | **measured 2.62×** (keygen/sign), 2.81× (verify). The projection was 33% high and its stated band did not contain the true value. Projected per-op costs 36.4 s / 276.3 s / 0.261 s → measured 27.293 s / 207.082 s / 0.209 s |

Sources:
- [ESP32 Series Datasheet (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32-WROOM-32 Datasheet](https://www.mouser.com/datasheet/2/891/esp-wroom-32_datasheet_en-1223836.pdf)
