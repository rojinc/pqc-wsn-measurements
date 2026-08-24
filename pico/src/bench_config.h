/*
 * bench_config.h — RP2040 run configuration.
 *
 * RE-RUN CAMPAIGN 2026-07-28. Everything in ONE session (~5.4 h), so the whole
 * RP2040 dataset comes from a single firmware build carrying a single build
 * stamp. Superseded captures: results/archive/2026-07-28_pre-buildfix/.
 *
 * This build differs from every archived RP2040 capture in three ways:
 *   1. -Os throughout (pqclean_common, harness and mbedTLS were -O3; the ESP32
 *      was always -Os). Measured effect on ML-KEM: +12% to +20%.
 *   2. clk_sys is READ from hardware, not assumed to be 133 MHz. It is 125.
 *   3. The test message is byte-identical to the ESP32 build.
 *
 * ==========================================================================
 * SAMPLE SIZES — must stay IDENTICAL to esp32/main/bench_config.h.
 * Change one, change the other, or the cross-platform tables are meaningless.
 *
 *   ML-DSA sign      10,000  N_SIGN. CV 63-67% (rejection sampling). The
 *                            reviewer-facing tail number — non-negotiable.
 *   deterministic ops 1,000  N_DET. CV < 1.5%; converged many times over.
 *   FN-DSA sign       1,000  CV 0.19-0.32%. Our own resampling shows n=100
 *                            lands within 0.124% of the n=10,000 mean at this
 *                            CV, so 1,000 is generous. NOTE: this previously
 *                            took N_SIGN (10,000) on this platform, which is
 *                            4.3 h of the session for no statistical gain.
 *   FN-DSA verify     1,000  CV 0.14%. Also previously took N_SIGN.
 *   FN-DSA keygen     1,000  CV 34% (NTRU rejection) — genuinely needs it.
 *                            ~1.65 h; the single most expensive item here.
 *   SLH-DSA kg/sign      30  CV 0.00%. The archived 100-run ESP32 sample spanned
 *                            0.5 ms on a 78,950 ms mean. On this platform each
 *                            extra sign sample costs ~4 minutes and buys nothing.
 *   SLH-DSA verify    1,000  CV 0.01%.
 *
 * Session budget at -Os (projected from archived -O3 costs):
 *   ML-KEM    0.7 min | ML-DSA  35.8 min | ECDSA  8.4 min
 *   FN-DSA  125.4 min | SLH-DSA 142.4 min | setup+stack 9.5 min
 *                                                 -> ~5.4 h
 *
 * Longest silent stretch ~4.1 min (one SLH-DSA signature), well inside the
 * capture script's 900 s idle timeout. Longest single op ~4.1 min, well inside
 * the 71.6 min time_us_32() wrap.
 * ========================================================================== */
#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H

/* SLH-DSA-128s — RP2040 probe measured (at -O3): keygen 27.29 s, sign 207.08 s,
 * verify 209.19 ms. Expect ~18% higher at -Os.
 * sign=100 is the ONLY reduced sample size in this project, per CONSTANTS.md. */
#define SPX_N_KEYGEN 1000
#define SPX_N_SIGN   100
#define SPX_N_VERIFY 10000

/* FN-DSA-512 — RP2040 measured (at -O3): keygen 5.51 s, sign 1.44 s,
 * verify 12.27 ms. Expect ~8% higher at -Os (falcon512 was already -Os; only
 * its SHAKE calls change). */
#define FAL_N_KEYGEN 1000
#define FAL_N_SIGN   10000
#define FAL_N_VERIFY 10000

/* Full sweep in one session. Ordering in bench_pico.c puts ML-DSA's 10k signing
 * loop last so faster results appear early on the serial log. */
#define RUN_MLKEM    1
#define RUN_MLDSA    1
#define RUN_ECDSA    1
#define RUN_FALCON   1
#define RUN_SPHINCS  1

#endif /* BENCH_CONFIG_H */
