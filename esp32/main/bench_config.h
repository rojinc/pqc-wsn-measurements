/*
 * bench_config.h — ESP32 run configuration.
 *
 * RE-RUN CAMPAIGN 2026-07-28. Everything in ONE session (~1.6 h), so the whole
 * ESP32 dataset comes from a single firmware build carrying a single build
 * stamp. Superseded captures: results/archive/2026-07-28_pre-buildfix/.
 *
 * Estimated session length, from the archived per-op costs:
 *   ML-KEM   1000 each                     ~16 s
 *   ML-DSA   1000 / 1000 / 10000           ~8.5 min   (sign dominates)
 *   ECDSA    1000 each                     ~5.2 min
 *   FN-DSA   1000 / 1000 / 1000            ~29 min    (keygen 1.39 s/run)
 *   SLH-DSA  1000 verify, 30 kg, 30 sign   ~46 min    (sign 78.95 s/run)
 *   setup + stack passes                   ~4 min
 *                                         ----------
 *                                          ~1.6 h
 *
 * ==========================================================================
 * SAMPLE SIZES — matched to the RP2040 build so every cross-platform
 * comparison is like-for-like at identical n. Scaled to variance and per-run
 * cost as documented in docs/CONSTANTS.md, not held flat.
 *
 *   ML-DSA sign      10,000  CV 63-67% (rejection sampling). The
 *                            reviewer-facing tail number and the only one that
 *                            needs the full sample. Non-negotiable.
 *   deterministic ops 1,000  CV < 1.5%; converged many times over
 *   FN-DSA sign       1,000  CV 0.19-0.32%. Our own resampling shows n=100
 *                            lands within 0.124% of the n=10,000 mean at this
 *                            CV; 1,000 is generous
 *   FN-DSA keygen     1,000  CV 34-38% (NTRU rejection) — genuinely needs it
 *   SLH-DSA kg/sign      30  CV 0.00%. The archived 100-run sample spanned
 *                            0.5 ms on a 78,950 ms mean. Each extra sample
 *                            costs 79 seconds and buys nothing
 *   SLH-DSA verify    1,000  CV 0.01%
 * ========================================================================== */
#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H

/* SLH-DSA-128s — MEASURED on ESP32: keygen 10.39 s, sign 78.95 s, verify 74.6 ms.
 * sign=100 is the ONLY reduced sample size in this project: 10,000 would take
 * 9.1 days. CV measured 0.00%, so 100 is already vastly more than needed. */
#define SPX_N_KEYGEN 1000
#define SPX_N_SIGN   100
#define SPX_N_VERIFY 10000

/* FN-DSA-512 — MEASURED on ESP32: keygen 1.39 s, sign 354 ms, verify 5.45 ms */
#define FAL_N_KEYGEN 1000
#define FAL_N_SIGN   10000
#define FAL_N_VERIFY 10000

/* Full sweep in one session. The ordering in bench_esp32.c already puts the
 * long loops last, so fast results appear early on the serial log. */
#define RUN_MLKEM    1
#define RUN_MLDSA    1
#define RUN_ECDSA    1
#define RUN_FALCON   1
#define RUN_SPHINCS  1

#endif /* BENCH_CONFIG_H */
