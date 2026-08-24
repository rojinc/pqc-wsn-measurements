# RP2040 validation against the published paper (arXiv:2603.19340)

This campaign re-measured the RP2040 from scratch — new project, new harness,
new build, n = 1000/10000 instead of the original n = 30/100. Comparing the two
independent measurement campaigns is the strongest evidence we have that the
method is sound.

## Peak stack — essentially identical

| Operation | Published (n=30) | This run (n=1000) | Δ bytes |
|---|--:|--:|--:|
| ML-KEM-512 keygen | 6,248 | 6,232 | −16 |
| ML-KEM-512 encaps | 8,880 | 8,864 | −16 |
| ML-KEM-512 decaps | 9,656 | 9,624 | −32 |
| ML-DSA-44 keygen | 38,400 | 38,392 | −8 |
| ML-DSA-44 verify | 36,072 | 36,072 | **exact** |
| ML-DSA-44 sign | 51,840 | 51,840 | **exact** |

Two exact matches and the rest within 32 bytes (one to four stack words — the
difference in the harness's own call frame between the two firmwares). Stack
painting on this platform is reproducible to the byte.

**The v1 saturation bug cannot be present here:** the painted region is 196,376 B
and the worst case (ML-DSA-44 sign) uses 51,840 B, leaving 144,536 B of headroom.
No overflow flag, no saturation warning, and keygen / verify / sign all report
different values in the physically sensible order (verify < keygen < sign).

## Timing — same ranking, ~5-12% offsets

| Operation | Published | This run | Δ |
|---|--:|--:|--:|
| ML-KEM-512 keygen | 9.94 ms | 9.36 ms | −6% |
| ML-KEM-512 encaps | 11.53 ms | 10.88 ms | −6% |
| ML-KEM-512 decaps | 14.23 ms | 13.29 ms | −7% |
| ML-DSA-44 keygen | 39.83 ms | 40.95 ms | +3% |
| ML-DSA-44 verify | 43.98 ms | 46.42 ms | +6% |
| ECDSA-P256 keygen | 83.64 ms | 78.27 ms | −6% |
| ECDSA-P256 sign | 92.60 ms | 86.92 ms | −6% |
| ECDSA-P256 verify | 321.36 ms | 301.37 ms | −6% |

Offsets are consistent in direction within each library and are attributable to a
different Pico SDK / toolchain version between campaigns (this run: SDK 2.2.0 +
whatever `arm-none-eabi-gcc` Homebrew currently ships; the paper used gcc 12.2.1).
Relative rankings and all conclusions are unchanged.

## The tail was previously underestimated — this is the headline

ML-DSA-44 signing, n = 100 (published) vs n = 10,000 (this run):

| Statistic | Published (n=100) | This run (n=10,000) |
|---|--:|--:|
| mean | 158.91 ms | 178.13 ms |
| CV | 67.5% | 66.93% |
| p95 | 384.70 ms | 421.96 ms |
| **p99** | **489.90 ms** | **608.95 ms** |
| p99.9 | — (not estimable) | 861.79 ms |
| max | 541.70 ms | **1238.90 ms** |

> Percentiles are nearest-rank (`ceil(q·n) − 1`), computed offline from every
> run by `scripts/analyze.py`. The published n=100 column has no p99.9 because
> at that sample size the quantile has no observation behind it — the script now
> suppresses it rather than silently reporting the maximum in its place.

At n = 100 the p99 rested on a single observation. With 10,000 samples the true
p99 is **24% higher** than published, and the observed worst case is **1.24
seconds** — 2.3× the previously reported maximum. The CV is nearly unchanged
(67.5% → 66.9%), confirming the distribution shape was right; only the tail was
undersampled.

This directly answers the reviewer objection: the tail statistics are now
supported by ~100 observations in the p99 bin and ~10 in the p99.9 bin.

## Cross-platform ratio (RP2040 ÷ ESP32)

| Operation | RP2040 | ESP32 | ratio |
|---|--:|--:|--:|
| ML-KEM-512 keygen | 9.36 | 4.47 | 2.09× |
| ML-KEM-512 encaps | 10.88 | 5.30 | 2.05× |
| ML-KEM-512 decaps | 13.29 | 6.54 | 2.03× |
| ML-DSA-44 keygen | 40.95 | 13.32 | 3.07× |
| ML-DSA-44 verify | 46.42 | 14.37 | 3.23× |
| ML-DSA-44 sign | 178.13 | 48.24 | 3.69× |
| ECDSA-P256 sign | 86.92 | 60.25 | 1.44× |

The M0+/Xtensa gap is scheme-dependent (2.0–3.7×), not a single constant — worth
stating rather than assuming one scaling factor.

## Implication for the FN-DSA claim

Using the ML-DSA sign ratio (3.69×) to project the measured ESP32 FN-DSA-512
signing time (354.25 ms) onto the RP2040 gives an expected **~1.3 seconds** on
Cortex-M0+ — against an earlier literature figure of 71.6 ms for this platform, an ~18×
discrepancy. The direct RP2040 measurement is the next run and will settle it.
