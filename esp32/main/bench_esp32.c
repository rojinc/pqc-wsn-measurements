/*
 * bench_esp32.c — PQC benchmark entry point for ESP32 (Xtensa LX6).
 *
 * ML-DSA-44 (FIPS 204): keygen (1k), sign (10k), verify (1k).
 * ML-KEM-512 (FIPS 203): keygen (1k), encaps (1k), decaps (1k).
 *
 * Each measured op runs in its OWN task using pre-made "golden" inputs and its
 * OWN scratch outputs, so a task's stack high-water mark reflects ONLY that op
 * (keygen/sign/verify and keygen/encaps/decaps must all report distinct stacks).
 * Saturation guard + FreeRTOS canary detection guard against the v1 stack bug.
 */
#include "bench_harness_esp32.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "api_mldsa44.h"    /* self-contained copy of PQClean ml-dsa-44 api.h  */
#include "api_mlkem512.h"   /* self-contained copy of PQClean ml-kem-512 api.h */
#include "api_sphincs128s.h"/* SLH-DSA-128s (SPHINCS+-SHA2-128s-simple)        */
#include "api_falcon512.h"  /* FN-DSA-512 draft / Falcon-512                   */
#include "bench_config.h"   /* which schemes this build runs                   */

uint32_t g_cpu_mhz = 0;

#define DSA_STACK  (100 * 1024)   /* ~2x ML-DSA-44 sign peak (~52 KB) */
#define KEM_STACK  ( 64 * 1024)   /* ML-KEM-512 stacks are ~10 KB     */

/* MUST be byte-identical to the string in pico/src/bench_pico.c. The two
 * platforms previously signed DIFFERENT messages (118 vs 124 bytes), which is
 * indefensible in a controlled cross-platform comparison even though both fit
 * inside one SHAKE256 block and the timing effect was unmeasurable. */
static const uint8_t msg[] =
    "PQC WSN benchmark: fixed cross-platform test vector. "
    "Identical on RP2040 and ESP32. Do not edit one without the other.";
static const size_t msg_len = sizeof(msg) - 1;

/* ================= ML-DSA-44 ================= */
static uint8_t d_pk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t d_sk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t d_sig[PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES];
static size_t  d_sig_len = 0;
/* scratch */
static uint8_t d_pk2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t d_sk2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t d_sig2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES];
static size_t  d_sig2_len = 0;
static volatile int d_verify_rc = -1;

static void dsa_setup(void *a) {
    PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(d_pk, d_sk);
    PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(d_sig, &d_sig_len, msg, msg_len, d_sk);
    BENCH_TASK_DONE(a);
}
static void dsa_keygen(void *a) {
    BENCH_RUN("ML-DSA", "keygen", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(d_pk2, d_sk2);
    });
    BENCH_REPORT_STACK("ML-DSA", "keygen", 44, DSA_STACK);
    BENCH_TASK_DONE(a);
}
static void dsa_verify(void *a) {
    BENCH_RUN("ML-DSA", "verify", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(d_sig, d_sig_len, msg, msg_len, d_pk);
    });
    d_verify_rc = PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(d_sig, d_sig_len, msg, msg_len, d_pk);
    BENCH_REPORT_STACK("ML-DSA", "verify", 44, DSA_STACK);
    BENCH_TASK_DONE(a);
}
static void dsa_sign(void *a) {
    BENCH_RUN_N("ML-DSA", "sign", 44, N_SIGN, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(d_sig2, &d_sig2_len, msg, msg_len, d_sk);
    });
    BENCH_REPORT_STACK("ML-DSA", "sign", 44, DSA_STACK);
    BENCH_TASK_DONE(a);
}

/* ================= ML-KEM-512 ================= */
static uint8_t k_pk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t k_sk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t k_ct[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t k_ss_ref[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
/* scratch */
static uint8_t k_pk2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t k_sk2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t k_ct2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t k_ss2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint8_t k_ss_out[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static volatile int k_correct = -1;

static void kem_setup(void *a) {
    PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(k_pk, k_sk);
    PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(k_ct, k_ss_ref, k_pk);
    BENCH_TASK_DONE(a);
}
static void kem_keygen(void *a) {
    BENCH_RUN("ML-KEM", "keygen", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(k_pk2, k_sk2);
    });
    BENCH_REPORT_STACK("ML-KEM", "keygen", 512, KEM_STACK);
    BENCH_TASK_DONE(a);
}
static void kem_encaps(void *a) {
    BENCH_RUN("ML-KEM", "encaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(k_ct2, k_ss2, k_pk);
    });
    BENCH_REPORT_STACK("ML-KEM", "encaps", 512, KEM_STACK);
    BENCH_TASK_DONE(a);
}
static void kem_decaps(void *a) {
    BENCH_RUN("ML-KEM", "decaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(k_ss_out, k_ct, k_sk);
    });
    k_correct = (memcmp(k_ss_out, k_ss_ref, PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES) == 0);
    BENCH_REPORT_STACK("ML-KEM", "decaps", 512, KEM_STACK);
    BENCH_TASK_DONE(a);
}

/* ================= ECDSA P-256 (classical baseline, mbedTLS) ================= */
#define ECDSA_STACK (32 * 1024)

static int esp_rng(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx; esp_fill_random(buf, len); return 0;
}

static uint8_t e_hash[32];               /* SHA-256(msg) */
static uint8_t e_sig[MBEDTLS_ECDSA_MAX_LEN];
static size_t  e_sig_len = 0;
static mbedtls_ecdsa_context e_ctx;      /* golden key (sign/verify inputs) */
static volatile int e_verify_rc = -1;

static void ecdsa_setup(void *a) {
    mbedtls_sha256(msg, msg_len, e_hash, 0);
    mbedtls_ecdsa_init(&e_ctx);
    mbedtls_ecdsa_genkey(&e_ctx, MBEDTLS_ECP_DP_SECP256R1, esp_rng, NULL);
    mbedtls_ecdsa_write_signature(&e_ctx, MBEDTLS_MD_SHA256, e_hash, 32,
                                  e_sig, sizeof(e_sig), &e_sig_len, esp_rng, NULL);
    BENCH_TASK_DONE(a);
}
static void ecdsa_keygen(void *a) {
    mbedtls_ecdsa_context c; mbedtls_ecdsa_init(&c);
    BENCH_RUN("ECDSA-P256", "keygen", 256, {
        mbedtls_ecdsa_genkey(&c, MBEDTLS_ECP_DP_SECP256R1, esp_rng, NULL);
    });
    BENCH_REPORT_STACK("ECDSA-P256", "keygen", 256, ECDSA_STACK);
    mbedtls_ecdsa_free(&c);
    BENCH_TASK_DONE(a);
}
static void ecdsa_sign(void *a) {
    uint8_t sig[MBEDTLS_ECDSA_MAX_LEN]; size_t slen;
    BENCH_RUN("ECDSA-P256", "sign", 256, {
        mbedtls_ecdsa_write_signature(&e_ctx, MBEDTLS_MD_SHA256, e_hash, 32,
                                      sig, sizeof(sig), &slen, esp_rng, NULL);
    });
    BENCH_REPORT_STACK("ECDSA-P256", "sign", 256, ECDSA_STACK);
    BENCH_TASK_DONE(a);
}
static void ecdsa_verify(void *a) {
    BENCH_RUN("ECDSA-P256", "verify", 256, {
        mbedtls_ecdsa_read_signature(&e_ctx, e_hash, 32, e_sig, e_sig_len);
    });
    e_verify_rc = mbedtls_ecdsa_read_signature(&e_ctx, e_hash, 32, e_sig, e_sig_len);
    BENCH_REPORT_STACK("ECDSA-P256", "verify", 256, ECDSA_STACK);
    BENCH_TASK_DONE(a);
}

/* ================= SLH-DSA-128s (SPHINCS+-SHA2-128s-simple, FIPS 205) =======
 * Hash-based, essentially deterministic, but signing is SLOW (~1-2 s/sig on a
 * 240 MHz Xtensa). Sample counts are reduced accordingly: 10k here would run for
 * days and would tell us nothing extra, since there is no rejection-sampling
 * tail to characterise. Keygen is also slow (builds the top Merkle tree).
 * ------------------------------------------------------------------------- */
#define SPX_STACK      (48 * 1024)
/* SPX_N_KEYGEN / SPX_N_SIGN / SPX_N_VERIFY come from bench_config.h — see the
 * note there on setting n by variance rather than a flat 10k. */

#define SPX_PK  PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES
#define SPX_SK  PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES
#define SPX_SIG PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_BYTES

static uint8_t s_pk[SPX_PK], s_sk[SPX_SK];
static uint8_t s_sig[SPX_SIG];
static size_t  s_sig_len = 0;
/* scratch */
static uint8_t s_pk2[SPX_PK], s_sk2[SPX_SK];
static uint8_t s_sig2[SPX_SIG];
static size_t  s_sig2_len = 0;
static volatile int s_verify_rc = -1;

static void spx_setup(void *a) {
    /* SLH-DSA setup is slow (seconds per op) and silent; print progress so a
     * long quiet stretch is never mistaken for a hang by the capture script. */
    int64_t t0 = esp_timer_get_time();
    printf("# setup: SLH-DSA keygen starting...\n");
    PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_keypair(s_pk, s_sk);
    printf("# setup: keygen done in %.2f s, signing...\n",
           (esp_timer_get_time() - t0) / 1e6);
    t0 = esp_timer_get_time();
    PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_signature(
        s_sig, &s_sig_len, msg, msg_len, s_sk);
    printf("# setup: sign done in %.2f s (siglen=%u)\n",
           (esp_timer_get_time() - t0) / 1e6, (unsigned)s_sig_len);
    BENCH_TASK_DONE(a);
}
static void spx_keygen(void *a) {
    BENCH_RUN_N("SLH-DSA", "keygen", 128, SPX_N_KEYGEN, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_keypair(s_pk2, s_sk2);
    });
    BENCH_REPORT_STACK("SLH-DSA", "keygen", 128, SPX_STACK);
    BENCH_TASK_DONE(a);
}
static void spx_verify(void *a) {
    BENCH_RUN_N("SLH-DSA", "verify", 128, SPX_N_VERIFY, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_verify(
            s_sig, s_sig_len, msg, msg_len, s_pk);
    });
    s_verify_rc = PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_verify(
        s_sig, s_sig_len, msg, msg_len, s_pk);
    BENCH_REPORT_STACK("SLH-DSA", "verify", 128, SPX_STACK);
    BENCH_TASK_DONE(a);
}
static void spx_sign(void *a) {
    BENCH_RUN_N("SLH-DSA", "sign", 128, SPX_N_SIGN, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_signature(
            s_sig2, &s_sig2_len, msg, msg_len, s_sk);
    });
    BENCH_REPORT_STACK("SLH-DSA", "sign", 128, SPX_STACK);
    BENCH_TASK_DONE(a);
}

/* ================= FN-DSA-512 (Falcon-512, FIPS 206 draft) =================
 * PQClean stack buffers: keygen 14 KB, sign 36 KB, verify 1 KB -> 64 KB task is
 * ~1.7x the worst case. Signatures are VARIABLE length (<=752 B; the padded
 * FN-DSA form is 666 B) so we log the observed length.
 * Falcon's Gaussian sampler needs DOUBLE precision: the ESP32's FPU is
 * single-precision only, so fpr.c doubles are software-emulated here too.
 * ------------------------------------------------------------------------- */
#define FAL_STACK       (64 * 1024)
/* Sample sizes live in bench_config.h so ONE file controls the whole run.
 * These are fallbacks only, for a build that predates that config. */
#ifndef FAL_N_KEYGEN
#define FAL_N_KEYGEN    1000
#endif
#ifndef FAL_N_SIGN
#define FAL_N_SIGN      10000
#endif
#ifndef FAL_N_VERIFY
#define FAL_N_VERIFY    10000
#endif

#define FAL_PK  PQCLEAN_FALCON512_CLEAN_CRYPTO_PUBLICKEYBYTES
#define FAL_SK  PQCLEAN_FALCON512_CLEAN_CRYPTO_SECRETKEYBYTES
#define FAL_SIG PQCLEAN_FALCON512_CLEAN_CRYPTO_BYTES

static uint8_t f_pk[FAL_PK], f_sk[FAL_SK];
static uint8_t f_sig[FAL_SIG];
static size_t  f_sig_len = 0;
/* scratch */
static uint8_t f_pk2[FAL_PK], f_sk2[FAL_SK];
static uint8_t f_sig2[FAL_SIG];
static size_t  f_sig2_len = 0;
static volatile int f_verify_rc = -1;

static void fal_setup(void *a) {
    PQCLEAN_FALCON512_CLEAN_crypto_sign_keypair(f_pk, f_sk);
    PQCLEAN_FALCON512_CLEAN_crypto_sign_signature(
        f_sig, &f_sig_len, msg, msg_len, f_sk);
    BENCH_TASK_DONE(a);
}
static void fal_keygen(void *a) {
    BENCH_RUN_N("FN-DSA", "keygen", 512, FAL_N_KEYGEN, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_keypair(f_pk2, f_sk2);
    });
    BENCH_REPORT_STACK("FN-DSA", "keygen", 512, FAL_STACK);
    BENCH_TASK_DONE(a);
}
static void fal_verify(void *a) {
    BENCH_RUN_N("FN-DSA", "verify", 512, FAL_N_VERIFY, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_verify(
            f_sig, f_sig_len, msg, msg_len, f_pk);
    });
    f_verify_rc = PQCLEAN_FALCON512_CLEAN_crypto_sign_verify(
        f_sig, f_sig_len, msg, msg_len, f_pk);
    BENCH_REPORT_STACK("FN-DSA", "verify", 512, FAL_STACK);
    BENCH_TASK_DONE(a);
}
static void fal_sign(void *a) {
    size_t lmin = (size_t)-1, lmax = 0; double lsum = 0;
    BENCH_RUN_N("FN-DSA", "sign", 512, FAL_N_SIGN, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_signature(
            f_sig2, &f_sig2_len, msg, msg_len, f_sk);
        if (f_sig2_len < lmin) lmin = f_sig2_len;
        if (f_sig2_len > lmax) lmax = f_sig2_len;
        lsum += (double)f_sig2_len;
    });
    printf("# SIGLEN FN-DSA 512: mean=%.1f min=%u max=%u bytes (variable-length)\n",
           lsum / FAL_N_SIGN, (unsigned)lmin, (unsigned)lmax);
    BENCH_REPORT_STACK("FN-DSA", "sign", 512, FAL_STACK);
    BENCH_TASK_DONE(a);
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    bench_init(msg_len);
    printf("# Internal RAM: free=%u B, largest block=%u B\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bench_csv_header();

#if RUN_MLDSA
    /* ---- ML-DSA-44 ---- */
    printf("# --- ML-DSA-44 (FIPS 204) ---\n");
    printf("# PK=%d SK=%d SIG=%d bytes\n",
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES);
    bench_run_task(dsa_setup,  DSA_STACK);
    bench_run_task(dsa_keygen, DSA_STACK);
    bench_run_task(dsa_verify, DSA_STACK);
    printf("# CORRECTNESS: ML-DSA-44 %s\n", d_verify_rc == 0 ? "OK" : "FAILED!");
#endif

#if RUN_MLKEM
    /* ---- ML-KEM-512 ---- */
    printf("# --- ML-KEM-512 (FIPS 203) ---\n");
    printf("# PK=%d SK=%d CT=%d SS=%d bytes\n",
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES);
    bench_run_task(kem_setup,  KEM_STACK);
    bench_run_task(kem_keygen, KEM_STACK);
    bench_run_task(kem_encaps, KEM_STACK);
    bench_run_task(kem_decaps, KEM_STACK);
    printf("# CORRECTNESS: ML-KEM-512 shared secret %s\n",
           k_correct == 1 ? "MATCH" : "MISMATCH!");
#endif

#if RUN_ECDSA
    /* ---- ECDSA-P256 (classical baseline) ---- */
    printf("# --- ECDSA P-256 (classical baseline, mbedTLS) ---\n");
    bench_run_task(ecdsa_setup,  ECDSA_STACK);
    bench_run_task(ecdsa_keygen, ECDSA_STACK);
    bench_run_task(ecdsa_sign,   ECDSA_STACK);
    bench_run_task(ecdsa_verify, ECDSA_STACK);
    printf("# ECDSA-P256 sig_len=%u bytes\n", (unsigned)e_sig_len);
    printf("# CORRECTNESS: ECDSA-P256 %s\n", e_verify_rc == 0 ? "OK" : "FAILED!");
#endif

#if RUN_SPHINCS
    /* ---- SLH-DSA-128s (FIPS 205) ---- */
    printf("# --- SLH-DSA-128s (SPHINCS+-SHA2-128s-simple, FIPS 205) ---\n");
    printf("# PK=%d SK=%d SIG=%d bytes\n", SPX_PK, SPX_SK, SPX_SIG);
    printf("# n: keygen=%d sign=%d verify=%d\n",
           SPX_N_KEYGEN, SPX_N_SIGN, SPX_N_VERIFY);
    bench_run_task(spx_setup,  SPX_STACK);
    bench_run_task(spx_verify, SPX_STACK);
    bench_run_task(spx_keygen, SPX_STACK);
    bench_run_task(spx_sign,   SPX_STACK);
    printf("# CORRECTNESS: SLH-DSA-128s %s\n", s_verify_rc == 0 ? "OK" : "FAILED!");
#endif

#if RUN_MLDSA
    /* ---- ML-DSA-44 signing (10k) — the long pole, run last ---- */
    printf("# --- ML-DSA-44 signing (10k variance run) ---\n");
    bench_run_task(dsa_sign, DSA_STACK);
#endif

#if RUN_FALCON
    /* ---- FN-DSA-512 (Falcon-512, FIPS 206 draft) ---- */
    printf("# --- FN-DSA-512 / Falcon-512 (FIPS 206 draft) ---\n");
    printf("# PK=%d SK=%d SIG_MAX=%d bytes (padded FN-DSA form = 666)\n",
           FAL_PK, FAL_SK, FAL_SIG);
    printf("# NOTE: PQClean ships round-3 Falcon, not final FIPS 206 FN-DSA.\n");
    bench_run_task(fal_setup,  FAL_STACK);
    bench_run_task(fal_verify, FAL_STACK);
    bench_run_task(fal_keygen, FAL_STACK);
    bench_run_task(fal_sign,   FAL_STACK);
    printf("# CORRECTNESS: FN-DSA-512 %s\n", f_verify_rc == 0 ? "OK" : "FAILED!");
#endif

    printf("# === ESP32 benchmarks complete ===\n");
}
