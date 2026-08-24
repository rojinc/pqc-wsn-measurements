/*
 * bench_harness_esp32.h — timing/stats + hardened stack measurement for ESP32.
 *
 * CSV schema (identical to the RP2040 harness):
 *   algorithm,operation,security_level,time_us,run_number
 *
 * Every run is streamed as a CSV row; percentiles (p95/p99/p99.9) are computed
 * OFFLINE from the full CSV by scripts/analyze.py. On-device we keep only
 * streaming (Welford) mean/min/max/stddev — no array, so n can be 10,000+.
 *
 * STACK MEASUREMENT (hardened after the arXiv-v1 saturation bug):
 *   Each operation runs in its OWN FreeRTOS task, sized well above the expected
 *   peak. Peak = task_stack - uxTaskGetStackHighWaterMark(). Because every op
 *   gets a fresh task, keygen/sign/verify MUST report different numbers (they
 *   have different code paths) — the sanity tell that caught v1. A saturation
 *   guard prints a loud warning if free space drops near the ceiling, and
 *   FreeRTOS canary overflow detection (sdkconfig) aborts on a real overflow
 *   instead of silently saturating.
 */
#ifndef BENCH_HARNESS_ESP32_H
#define BENCH_HARNESS_ESP32_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"   /* esp_clk_cpu_freq() */

/* Default sample sizes. Override per-call with BENCH_RUN_N(...). */
#define N_DET   1000     /* deterministic ops: keygen, verify, encaps, decaps */
#define N_SIGN  10000    /* rejection-sampling signers: ML-DSA / FALCON        */

/* Warn if a stack-probe task ends with less than this much headroom — the
 * signal that the task was too small and the measurement may be saturated. */
#define STACK_SAT_GUARD  (16 * 1024)

extern uint32_t g_cpu_mhz;   /* filled in bench_init() */

static inline void bench_csv_header(void) {
    printf("algorithm,operation,security_level,time_us,run_number\n");
}

/*
 * Timing: run `code_block` num_runs times, stream each as CSV, accumulate
 * streaming stats. Yields periodically so idle task / UART keep up.
 */
#define BENCH_RUN_N(algo, op, sec_level, num_runs, code_block)               \
    do {                                                                     \
        int _n = (num_runs);                                                 \
        double _mean = 0.0, _m2 = 0.0;                                       \
        uint32_t _min = UINT32_MAX, _max = 0;                               \
        for (int _i = 0; _i < _n; _i++) {                                    \
            int64_t _s = esp_timer_get_time();                               \
            { code_block; }                                                  \
            int64_t _e = esp_timer_get_time();                               \
            uint32_t _t = (uint32_t)(_e - _s);                               \
            printf("%s,%s,%d,%lu,%d\n", algo, op, sec_level,                 \
                   (unsigned long)_t, _i + 1);                               \
            if (_t < _min) _min = _t;                                        \
            if (_t > _max) _max = _t;                                        \
            double _d = (double)_t - _mean;                                  \
            _mean += _d / (_i + 1);                                          \
            _m2 += _d * ((double)_t - _mean);                                \
            if (((_i + 1) & 63) == 0) vTaskDelay(1);                         \
        }                                                                    \
        double _sd = (_n > 1) ? sqrt(_m2 / _n) : 0.0;                        \
        double _cv = (_mean > 0) ? (_sd / _mean * 100.0) : 0.0;             \
        printf("# SUMMARY %s %s %d: mean=%.0fus (%.0fkc) min=%lu max=%lu "  \
               "sd=%.1f cv=%.2f%% n=%d\n",                                   \
               algo, op, sec_level, _mean, _mean * g_cpu_mhz / 1000.0,       \
               (unsigned long)_min, (unsigned long)_max, _sd, _cv, _n);      \
    } while (0)

#define BENCH_RUN(algo, op, sec_level, code_block) \
    BENCH_RUN_N(algo, op, sec_level, N_DET, code_block)

/*
 * Report peak stack of the CURRENT task, with a saturation guard.
 * Call at the end of an isolated per-op task. `total_bytes` is the stack the
 * task was created with. ESP-IDF returns the high-water mark in bytes.
 */
#define BENCH_REPORT_STACK(algo, op, sec_level, total_bytes)                 \
    do {                                                                     \
        uint32_t _mf = (uint32_t)uxTaskGetStackHighWaterMark(NULL);          \
        uint32_t _used = (uint32_t)(total_bytes) - _mf;                      \
        int _sat = (_mf < STACK_SAT_GUARD);                                  \
        printf("# STACK %s %s %d: %lu bytes used (task=%lu min_free=%lu)%s\n",\
               algo, op, sec_level, (unsigned long)_used,                    \
               (unsigned long)(total_bytes), (unsigned long)_mf,             \
               _sat ? "  *** WARNING: near ceiling — value may be "          \
                      "SATURATED, increase task stack ***" : "");            \
    } while (0)

/*
 * Launch `fn` in its own task with `stack_bytes` of stack and block until it
 * finishes. The task receives a binary semaphore as its arg and must give it
 * back right before vTaskDelete(NULL). Only one probe task is alive at a time,
 * so a large per-op stack never coexists with another.
 */
/* Core 1 (APP_CPU). Plain xTaskCreate() leaves the task at tskNO_AFFINITY, so
 * the scheduler may run it on either core and migrate it mid-measurement —
 * uncontrolled, and with no counterpart on the bare-metal single-core RP2040.
 * Pinning removes that variable. Core 1 carries less system housekeeping than
 * core 0 (PRO_CPU), which hosts the main task and most ESP-IDF services. */
#define BENCH_CORE 1

static inline void bench_run_task(TaskFunction_t fn, uint32_t stack_bytes) {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (xTaskCreatePinnedToCore(fn, "benchop", stack_bytes, done, 5, NULL,
                                BENCH_CORE) != pdPASS) {
        printf("# ERROR: could not create task with %lu-byte stack "
               "(out of internal RAM)\n", (unsigned long)stack_bytes);
        vSemaphoreDelete(done);
        return;
    }
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    /* The finished task self-deleted, but FreeRTOS defers freeing its (large)
     * stack to the idle task. Yield long enough for that reclaim to happen
     * before the caller creates the next big per-op task. */
    vTaskDelay(pdMS_TO_TICKS(200));
}

#define BENCH_TASK_DONE(arg) do { xSemaphoreGive((SemaphoreHandle_t)(arg)); \
                                  vTaskDelete(NULL); } while (0)

/* msg_bytes is passed in because the message is declared in the .c file, after
 * this header is included. */
static inline void bench_init(size_t msg_bytes) {
    g_cpu_mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000);
    printf("#\n# PQC ESP32 Benchmark Suite\n");
    printf("# Platform: ESP32-WROOM-32 (Xtensa LX6 dual-core)\n");
    printf("# CPU: %lu MHz (measured: esp_clk_cpu_freq)\n",
           (unsigned long)g_cpu_mhz);
    printf("# Sample sizes: deterministic=%d, signing=%d\n", N_DET, N_SIGN);
    printf("# Stack: per-op isolated tasks, saturation guard=%d KB\n",
           STACK_SAT_GUARD / 1024);
    /* BUILD PROVENANCE — ties this capture to the firmware that produced it.
     * Previously absent on both platforms, which is exactly how a stale build
     * would go unnoticed in a results file. */
    printf("# Build: %s %s | gcc %s | timer=esp_timer_get_time (1us wall clock)\n",
           __DATE__, __TIME__, __VERSION__);
    printf("# Task: pinned to core %d, priority 5\n", BENCH_CORE);
    printf("# Msg: %u bytes (must match the RP2040 build byte-for-byte)\n",
           (unsigned)msg_bytes);
    printf("#\n");
}

#endif /* BENCH_HARNESS_ESP32_H */
