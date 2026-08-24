/*
 * bench_harness.h — Timing measurement framework for PQC benchmarks
 *
 * Uses time_us_32() from the Pico SDK to measure operations in microseconds.
 * Runs each operation NUM_RUNS times, computes mean/min/max/stddev,
 * and prints CSV output over USB serial.
 *
 * CSV format: algorithm,operation,security_level,time_us,run_number
 */

#ifndef BENCH_HARNESS_H
#define BENCH_HARNESS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

#define NUM_RUNS       30
#define NUM_RUNS_DSA_SIGN 100  /* ML-DSA signing needs more runs for variance analysis */

/* Stack painting for peak stack usage measurement */
#define STACK_PAINT_VALUE 0xDEADBEEF

/*
 * Print CSV header — call once at program start
 */
static inline void bench_csv_header(void) {
    printf("algorithm,operation,security_level,time_us,run_number\n");
}

/*
 * Print one CSV row
 */
static inline void bench_csv_row(const char *algo, const char *op,
                                  int sec_level, uint32_t time_us,
                                  int run) {
    printf("%s,%s,%d,%lu,%d\n", algo, op, sec_level,
           (unsigned long)time_us, run);
    /* Yield to USB stack so CDC doesn't timeout during long benchmarks */
    sleep_ms(10);
}

/*
 * Run a benchmark: execute `func` NUM_RUNS times, printing CSV each run.
 * Also prints a summary line prefixed with '#' for quick human reading.
 *
 * Usage:
 *   BENCH_RUN("ML-KEM", "keygen", 512, {
 *       OQS_KEM_keypair(kem, pk, sk);
 *   });
 */
#define BENCH_RUN(algo, op, sec_level, code_block)                          \
    do {                                                                     \
        uint32_t _times[NUM_RUNS];                                          \
        for (int _i = 0; _i < NUM_RUNS; _i++) {                            \
            uint32_t _start = time_us_32();                                 \
            { code_block; }                                                 \
            uint32_t _end = time_us_32();                                   \
            _times[_i] = _end - _start;                                     \
            bench_csv_row(algo, op, sec_level, _times[_i], _i + 1);         \
        }                                                                    \
        /* Compute summary statistics */                                     \
        uint64_t _sum = 0;                                                  \
        uint32_t _min = UINT32_MAX, _max = 0;                              \
        for (int _i = 0; _i < NUM_RUNS; _i++) {                            \
            _sum += _times[_i];                                              \
            if (_times[_i] < _min) _min = _times[_i];                       \
            if (_times[_i] > _max) _max = _times[_i];                       \
        }                                                                    \
        double _mean = (double)_sum / NUM_RUNS;                             \
        double _var = 0;                                                     \
        for (int _i = 0; _i < NUM_RUNS; _i++) {                            \
            double _d = (double)_times[_i] - _mean;                         \
            _var += _d * _d;                                                 \
        }                                                                    \
        double _stddev = sqrt(_var / NUM_RUNS);                             \
        double _cv = (_mean > 0) ? (_stddev / _mean * 100.0) : 0;          \
        printf("# SUMMARY %s %s %d: mean=%.0f min=%lu max=%lu "            \
               "stddev=%.1f cv=%.2f%%\n",                                    \
               algo, op, sec_level, _mean,                                   \
               (unsigned long)_min, (unsigned long)_max, _stddev, _cv);      \
    } while (0)

/*
 * Extended run macro for ML-DSA signing (100 iterations for variance analysis)
 */
#define BENCH_RUN_EXTENDED(algo, op, sec_level, num_runs, code_block)       \
    do {                                                                     \
        uint32_t _times[num_runs];                                          \
        for (int _i = 0; _i < num_runs; _i++) {                            \
            uint32_t _start = time_us_32();                                 \
            { code_block; }                                                 \
            uint32_t _end = time_us_32();                                   \
            _times[_i] = _end - _start;                                     \
            bench_csv_row(algo, op, sec_level, _times[_i], _i + 1);         \
        }                                                                    \
        uint64_t _sum = 0;                                                  \
        uint32_t _min = UINT32_MAX, _max = 0;                              \
        for (int _i = 0; _i < num_runs; _i++) {                            \
            _sum += _times[_i];                                              \
            if (_times[_i] < _min) _min = _times[_i];                       \
            if (_times[_i] > _max) _max = _times[_i];                       \
        }                                                                    \
        double _mean = (double)_sum / num_runs;                             \
        double _var = 0;                                                     \
        for (int _i = 0; _i < num_runs; _i++) {                            \
            double _d = (double)_times[_i] - _mean;                         \
            _var += _d * _d;                                                 \
        }                                                                    \
        double _stddev = sqrt(_var / num_runs);                             \
        double _cv = (_mean > 0) ? (_stddev / _mean * 100.0) : 0;          \
        printf("# SUMMARY %s %s %d: mean=%.0f min=%lu max=%lu "            \
               "stddev=%.1f cv=%.2f%% (n=%d)\n",                            \
               algo, op, sec_level, _mean,                                   \
               (unsigned long)_min, (unsigned long)_max, _stddev, _cv,       \
               num_runs);                                                    \
    } while (0)

/*
 * Stack painting: call before the operation to paint the stack
 * with a known pattern, then measure how much was overwritten.
 */
static inline void bench_paint_stack(uint32_t *bottom, size_t size_words) {
    for (size_t i = 0; i < size_words; i++) {
        bottom[i] = STACK_PAINT_VALUE;
    }
}

static inline uint32_t bench_measure_stack(uint32_t *bottom, size_t size_words) {
    size_t unused = 0;
    for (size_t i = 0; i < size_words; i++) {
        if (bottom[i] == STACK_PAINT_VALUE)
            unused++;
        else
            break;
    }
    return (uint32_t)((size_words - unused) * sizeof(uint32_t));
}

/*
 * Measure peak stack usage for a single operation using stack painting.
 * Paints from __StackLimit up to current SP, runs the operation once,
 * then scans for the high-water mark.
 *
 * Usage:
 *   BENCH_STACK("ML-KEM", "keygen", 512, {
 *       PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);
 *   });
 */
extern uint32_t __StackLimit;   /* provided by linker script (stack bottom) */

#define BENCH_STACK(algo, op, sec_level, code_block)                         \
    do {                                                                      \
        uint32_t _sp;                                                         \
        __asm__ volatile("mov %0, sp" : "=r"(_sp));                          \
        uint32_t *_bottom = &__StackLimit;                                   \
        size_t _words = (_sp - (uint32_t)_bottom) / sizeof(uint32_t);        \
        size_t _avail = _words * sizeof(uint32_t);                           \
        bench_paint_stack(_bottom, _words);                                   \
        { code_block; }                                                       \
        uint32_t _used = bench_measure_stack(_bottom, _words);               \
        /* Overflow guard: if bottom word was overwritten, stack may have     \
         * grown past __StackLimit and the measurement is unreliable. */      \
        int _overflow = (_bottom[0] != STACK_PAINT_VALUE);                   \
        printf("# STACK %s %s %d: %lu bytes (avail=%lu%s)\n",               \
               algo, op, sec_level, (unsigned long)_used,                     \
               (unsigned long)_avail,                                         \
               _overflow ? " OVERFLOW" : "");                                 \
        if (_overflow)                                                        \
            printf("# WARNING: stack overflow detected — value unreliable, " \
                   "increase PICO_STACK_SIZE\n");                             \
        sleep_ms(10);                                                         \
    } while (0)

/*
 * Wait for USB serial connection before starting benchmarks
 */
static inline void bench_init(void) {
    stdio_init_all();
    /* Wait until USB serial is actually connected by the host */
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    /* Extra settle time for Windows CDC driver */
    sleep_ms(2000);
    printf("# PQC Cortex-M0+ Benchmark Suite\n");
    printf("# Platform: Raspberry Pi Pico (RP2040, ARM Cortex-M0+ @ 133MHz)\n");
    printf("# RAM: 264 KB, Flash: 2 MB\n");
    printf("# Runs per operation: %d\n", NUM_RUNS);
    printf("#\n");
}

#endif /* BENCH_HARNESS_H */
