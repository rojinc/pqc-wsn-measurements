/*
 * bench_harness.h — timing/stats + hardened stack measurement for RP2040.
 *
 * CSV schema (identical to the ESP32 harness):
 *   algorithm,operation,security_level,time_us,run_number
 *
 * Every run is streamed as a CSV row; percentiles are computed OFFLINE from the
 * full CSV by scripts/analyze.py. On-device we keep only streaming (Welford)
 * mean/min/max/stddev, so n can be 10,000+ with no RAM cost.
 *
 * ---------------------------------------------------------------------------
 * STACK MEASUREMENT — hardened after the arXiv-v1 saturation bug.
 *
 * v1 reported ML-DSA-65/87 stacks that were WRONG: the painted region (64 KB)
 * was smaller than the actual requirement, so signing silently ran past the
 * paint and the measurement saturated. Two anomalies exposed it: (i) keygen,
 * sign and verify reported identical values within a level (impossible — they
 * have different code paths), and (ii) ML-DSA-87 reported LESS stack than
 * ML-DSA-65 despite strictly larger parameters.
 *
 * Guards now in place:
 *   1. Large painted region — PICO_STACK_SIZE is set to 192 KB by the build,
 *      and we paint from __StackLimit all the way up to the current SP.
 *   2. Overflow detection — if the BOTTOM painted word is overwritten, the
 *      stack may have grown past __StackLimit; the value is flagged UNRELIABLE
 *      instead of being reported as a number.
 *   3. Saturation guard — if the high-water mark comes within STACK_SAT_GUARD
 *      of the painted floor, we warn: too close to the ceiling to trust.
 *   4. Setup runs OUTSIDE the painted measurement, so producing a key or a
 *      signature for an op's inputs can never inflate that op's stack figure.
 *      (This caught a real bug on ESP32: verify was reporting sign's 52 KB.)
 *   5. The painter and the measurement pass are always_inline. Out-of-line
 *      codegen would place their own frames inside the region being painted,
 *      so this is a correctness requirement — see the note above them.
 *
 * Sanity check to apply to EVERY capture: keygen / sign / verify must report
 * three DIFFERENT numbers, and bigger parameters must give bigger stacks.
 * ---------------------------------------------------------------------------
 */
#ifndef BENCH_HARNESS_H
#define BENCH_HARNESS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"

/*
 * System clock in MHz, READ FROM THE HARDWARE at boot — never assumed.
 *
 * This was previously hardcoded to 133. Nothing in this project ever called
 * set_sys_clock_khz(), so the RP2040 was running at the pico-sdk default of
 * 125 MHz while every cycle count was computed as microseconds x 133 — an
 * inflation of 6.4% in every RP2040 cycle figure and in every cycle-normalised
 * cross-platform ratio derived from them. 133 MHz is the RP2040 datasheet
 * maximum, not the SDK default.
 *
 * Wall-clock microsecond timings were NEVER affected: time_us_32() derives from
 * the 1 MHz timer tick, independent of clk_sys. Only derived cycle counts were.
 *
 * The ESP32 harness has always read esp_clk_cpu_freq() at runtime; this brings
 * the RP2040 side to the same standard.
 */
static uint32_t g_cpu_mhz = 0;

/* Default sample sizes (match the ESP32 campaign). */
#define N_DET   1000     /* deterministic ops: keygen, verify, encaps, decaps */
#define N_SIGN  10000    /* signers: ML-DSA, FN-DSA                            */

#define STACK_PAINT_VALUE 0xDEADBEEF
#define STACK_SAT_GUARD   (16 * 1024)   /* bytes of headroom we insist on */

extern uint32_t __StackLimit;   /* stack floor, from the linker script */

static inline void bench_csv_header(void) {
    printf("algorithm,operation,security_level,time_us,run_number\n");
}

/*
 * Timing: run `code_block` num_runs times, stream each as a CSV row, accumulate
 * streaming stats. time_us_32() has 1 us resolution; unsigned subtraction gives
 * the correct delta even across the ~71-minute counter wrap, as long as a
 * SINGLE operation takes less than that (always true here).
 */
#define BENCH_RUN_N(algo, op, sec_level, num_runs, code_block)               \
    do {                                                                     \
        int _n = (num_runs);                                                 \
        double _mean = 0.0, _m2 = 0.0;                                       \
        uint32_t _min = UINT32_MAX, _max = 0;                               \
        for (int _i = 0; _i < _n; _i++) {                                    \
            uint32_t _s = time_us_32();                                      \
            { code_block; }                                                  \
            uint32_t _t = time_us_32() - _s;                                 \
            printf("%s,%s,%d,%lu,%d\n", algo, op, sec_level,                 \
                   (unsigned long)_t, _i + 1);                               \
            if (_t < _min) _min = _t;                                        \
            if (_t > _max) _max = _t;                                        \
            double _d = (double)_t - _mean;                                  \
            _mean += _d / (_i + 1);                                          \
            _m2 += _d * ((double)_t - _mean);                                \
        }                                                                    \
        double _sd = (_n > 1) ? sqrt(_m2 / _n) : 0.0;                        \
        double _cv = (_mean > 0) ? (_sd / _mean * 100.0) : 0.0;             \
        printf("# SUMMARY %s %s %d: mean=%.0fus (%.0fkc) min=%lu max=%lu "  \
               "sd=%.1f cv=%.2f%% n=%d\n",                                   \
               algo, op, sec_level, _mean,                                   \
               _mean * (double)g_cpu_mhz / 1000.0,                           \
               (unsigned long)_min, (unsigned long)_max, _sd, _cv, _n);      \
    } while (0)

#define BENCH_RUN(algo, op, sec_level, code_block) \
    BENCH_RUN_N(algo, op, sec_level, N_DET, code_block)

/*
 * MUST be inlined — this is a correctness requirement, not an optimisation.
 *
 * The painter fills [__StackLimit, SP), i.e. exactly the region below the
 * caller's stack pointer. If the compiler ever emits it out of line, the call
 * pushes a return address and saved registers INTO that region, and the paint
 * loop then overwrites its own frame — corrupting the return address and
 * crashing, or silently producing a bogus high-water mark. `static inline` is
 * only a hint; at -O0, or under -fno-inline, or if a future compiler decides
 * the loop is too large, GCC is free to ignore it. always_inline makes the
 * guarantee explicit and turns a violation into a build error.
 *
 * The same applies to the measurement pass, which walks the painted region
 * while its own frame would sit inside it.
 */
#define BENCH_ALWAYS_INLINE static inline __attribute__((always_inline))

BENCH_ALWAYS_INLINE void bench_paint_stack(uint32_t *bottom, size_t words) {
    for (size_t i = 0; i < words; i++) bottom[i] = STACK_PAINT_VALUE;
}

BENCH_ALWAYS_INLINE uint32_t bench_measure_stack(uint32_t *bottom, size_t words) {
    size_t unused = 0;
    while (unused < words && bottom[unused] == STACK_PAINT_VALUE) unused++;
    return (uint32_t)((words - unused) * sizeof(uint32_t));
}

/*
 * Measure peak stack for ONE operation.
 *
 * IMPORTANT: `code_block` must contain ONLY the operation being measured. Any
 * key/signature setup it needs must already have been done before this macro is
 * invoked, or the setup's frames will be attributed to this operation.
 */
#define BENCH_STACK(algo, op, sec_level, code_block)                         \
    do {                                                                      \
        uint32_t _sp;                                                         \
        __asm__ volatile("mov %0, sp" : "=r"(_sp));                          \
        uint32_t *_bottom = &__StackLimit;                                   \
        size_t _words = (_sp - (uint32_t)_bottom) / sizeof(uint32_t);        \
        uint32_t _avail = (uint32_t)(_words * sizeof(uint32_t));             \
        bench_paint_stack(_bottom, _words);                                   \
        { code_block; }                                                       \
        uint32_t _used = bench_measure_stack(_bottom, _words);               \
        int _overflow = (_bottom[0] != STACK_PAINT_VALUE);                   \
        int _sat = (!_overflow && (_avail - _used) < STACK_SAT_GUARD);       \
        if (_overflow) {                                                      \
            printf("# STACK %s %s %d: UNRELIABLE — paint floor overwritten "\
                   "(avail=%lu). Increase PICO_STACK_SIZE and re-run.\n",     \
                   algo, op, sec_level, (unsigned long)_avail);               \
        } else {                                                              \
            printf("# STACK %s %s %d: %lu bytes used (avail=%lu "            \
                   "headroom=%lu)%s\n",                                       \
                   algo, op, sec_level, (unsigned long)_used,                 \
                   (unsigned long)_avail, (unsigned long)(_avail - _used),    \
                   _sat ? "  *** WARNING: near ceiling — may be SATURATED,"  \
                          " increase PICO_STACK_SIZE ***" : "");              \
        }                                                                     \
    } while (0)

/* msg_bytes is passed in because the message is declared in the .c file, after
 * this header is included. */
static inline void bench_init(size_t msg_bytes) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(2000);   /* let the host CDC settle */

    /* Read the real clock — do not assume it. See the note on g_cpu_mhz. */
    uint32_t sys_hz = clock_get_hz(clk_sys);
    g_cpu_mhz = sys_hz / 1000000u;

    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    printf("#\n# PQC RP2040 Benchmark Suite\n");
    printf("# Platform: Raspberry Pi Pico (RP2040, Cortex-M0+)\n");
    printf("# CPU: %lu MHz (measured: clk_sys = %lu Hz)\n",
           (unsigned long)g_cpu_mhz, (unsigned long)sys_hz);
    printf("# SRAM: 264 KB, Flash: 2 MB\n");
    printf("# Paintable stack: %lu bytes (SP=%08lx, StackLimit=%08lx)\n",
           (unsigned long)(sp - (uint32_t)&__StackLimit),
           (unsigned long)sp, (unsigned long)&__StackLimit);
    printf("# Sample sizes: deterministic=%d, signing=%d\n", N_DET, N_SIGN);
    /* BUILD PROVENANCE — ties this capture to the firmware that produced it. */
    printf("# Build: %s %s | gcc %s | timer=time_us_32 (1us wall clock)\n",
           __DATE__, __TIME__, __VERSION__);
    printf("# Msg: %u bytes (must match the ESP32 build byte-for-byte)\n",
           (unsigned)msg_bytes);
    printf("#\n");
}

#endif /* BENCH_HARNESS_H */
