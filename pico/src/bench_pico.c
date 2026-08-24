/*
 * bench_pico.c — PQC benchmark entry point for RP2040 (ARM Cortex-M0+).
 *
 * Mirrors the ESP32 campaign so the two platforms are directly comparable:
 * same schemes, same sample sizes, same CSV schema, same stack discipline.
 *
 * Stack discipline (see bench_harness.h for the full rationale):
 *   - setup work (making a key / a signature to verify) happens BEFORE the
 *     BENCH_STACK block, never inside it, so it cannot inflate the figure;
 *   - each op is painted and measured on its own;
 *   - overflow + saturation are detected and reported, never silently returned.
 * keygen / sign / verify MUST come out as three different numbers.
 */
#include "bench_harness.h"
#include "bench_config.h"

#if RUN_MLKEM
#include "api_mlkem512.h"
#endif
#if RUN_MLDSA
#include "api_mldsa44.h"
#endif
#if RUN_FALCON
#include "api_falcon512.h"
#endif
#if RUN_SPHINCS
#include "api_sphincs128s.h"
#endif
#if RUN_ECDSA
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "hardware/structs/rosc.h"
#endif

/* MUST be byte-identical to the string in esp32/main/bench_esp32.c. The two
 * platforms previously signed DIFFERENT messages (124 vs 118 bytes), which is
 * indefensible in a controlled cross-platform comparison even though both fit
 * inside one SHAKE256 block and the timing effect was unmeasurable. */
static const uint8_t msg[] =
    "PQC WSN benchmark: fixed cross-platform test vector. "
    "Identical on RP2040 and ESP32. Do not edit one without the other.";
static const size_t msg_len = sizeof(msg) - 1;

/* ======================= ML-KEM-512 (FIPS 203) ======================= */
#if RUN_MLKEM
static uint8_t k_pk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t k_sk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t k_ct[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t k_ss_ref[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint8_t k_pk2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t k_sk2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t k_ct2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t k_ss2[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
static uint8_t k_ss_out[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];

static void bench_mlkem512(void) {
    printf("# --- ML-KEM-512 (FIPS 203) ---\n");
    printf("# PK=%d SK=%d CT=%d SS=%d bytes\n",
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES,
           PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES);

    /* golden inputs */
    PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(k_pk, k_sk);
    PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(k_ct, k_ss_ref, k_pk);

    BENCH_RUN("ML-KEM", "keygen", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(k_pk2, k_sk2);
    });
    BENCH_RUN("ML-KEM", "encaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(k_ct2, k_ss2, k_pk);
    });
    BENCH_RUN("ML-KEM", "decaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(k_ss_out, k_ct, k_sk);
    });

    PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(k_ss_out, k_ct, k_sk);
    printf("# CORRECTNESS: ML-KEM-512 shared secret %s\n",
           memcmp(k_ss_out, k_ss_ref, PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES) == 0
           ? "MATCH" : "MISMATCH!");

    /* stack: inputs already exist, so each block holds only its own op */
    BENCH_STACK("ML-KEM", "keygen", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(k_pk2, k_sk2);
    });
    BENCH_STACK("ML-KEM", "encaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(k_ct2, k_ss2, k_pk);
    });
    BENCH_STACK("ML-KEM", "decaps", 512, {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(k_ss_out, k_ct, k_sk);
    });
}
#endif

/* ======================= ML-DSA-44 (FIPS 204) ======================= */
#if RUN_MLDSA
static uint8_t d_pk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t d_sk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t d_sig[PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES];
static size_t  d_sig_len = 0;
static uint8_t d_pk2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t d_sk2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t d_sig2[PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES];
static size_t  d_sig2_len = 0;

static void bench_mldsa44(void) {
    printf("# --- ML-DSA-44 (FIPS 204) ---\n");
    printf("# PK=%d SK=%d SIG=%d bytes\n",
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES);

    PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(d_pk, d_sk);
    PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(d_sig, &d_sig_len,
                                                msg, msg_len, d_sk);

    BENCH_RUN("ML-DSA", "keygen", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(d_pk2, d_sk2);
    });
    BENCH_RUN("ML-DSA", "verify", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(d_sig, d_sig_len,
                                                 msg, msg_len, d_pk);
    });
    BENCH_RUN_N("ML-DSA", "sign", 44, N_SIGN, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(d_sig2, &d_sig2_len,
                                                    msg, msg_len, d_sk);
    });

    printf("# CORRECTNESS: ML-DSA-44 %s\n",
           PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(d_sig, d_sig_len,
                                                    msg, msg_len, d_pk) == 0
           ? "OK" : "FAILED!");

    BENCH_STACK("ML-DSA", "keygen", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(d_pk2, d_sk2);
    });
    BENCH_STACK("ML-DSA", "verify", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(d_sig, d_sig_len,
                                                 msg, msg_len, d_pk);
    });
    BENCH_STACK("ML-DSA", "sign", 44, {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(d_sig2, &d_sig2_len,
                                                    msg, msg_len, d_sk);
    });
}
#endif

/* ================= FN-DSA-512 (Falcon-512, FIPS 206 draft) ================= */
#if RUN_FALCON
static uint8_t f_pk[PQCLEAN_FALCON512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t f_sk[PQCLEAN_FALCON512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t f_sig[PQCLEAN_FALCON512_CLEAN_CRYPTO_BYTES];
static size_t  f_sig_len = 0;
static uint8_t f_pk2[PQCLEAN_FALCON512_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t f_sk2[PQCLEAN_FALCON512_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t f_sig2[PQCLEAN_FALCON512_CLEAN_CRYPTO_BYTES];
static size_t  f_sig2_len = 0;

static void bench_falcon512(void) {
    printf("# --- FN-DSA-512 / Falcon-512 (FIPS 206 draft) ---\n");
    printf("# PK=%d SK=%d SIG_MAX=%d bytes (padded FN-DSA form = 666)\n",
           PQCLEAN_FALCON512_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_FALCON512_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_FALCON512_CLEAN_CRYPTO_BYTES);
    printf("# NOTE: PQClean ships round-3 Falcon with constant-time SOFTWARE\n"
           "#       float emulation (typedef uint64_t fpr) — no FPU is used.\n");

    PQCLEAN_FALCON512_CLEAN_crypto_sign_keypair(f_pk, f_sk);
    PQCLEAN_FALCON512_CLEAN_crypto_sign_signature(f_sig, &f_sig_len,
                                                  msg, msg_len, f_sk);

    /* Sample counts mirror the ESP32 Falcon campaign so the platforms are
     * directly comparable. Keygen is the exception: it costs ~4 s per run on
     * the M0+, so 10k would take 11 h for a ONE-TIME provisioning operation.
     * FAL_N_KEYGEN is set in bench_config.h. */
    BENCH_RUN_N("FN-DSA", "verify", 512, FAL_N_VERIFY, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_verify(f_sig, f_sig_len,
                                                   msg, msg_len, f_pk);
    });
    BENCH_RUN_N("FN-DSA", "keygen", 512, FAL_N_KEYGEN, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_keypair(f_pk2, f_sk2);
    });

    {
        size_t lmin = (size_t)-1, lmax = 0; double lsum = 0;
        BENCH_RUN_N("FN-DSA", "sign", 512, FAL_N_SIGN, {
            PQCLEAN_FALCON512_CLEAN_crypto_sign_signature(f_sig2, &f_sig2_len,
                                                          msg, msg_len, f_sk);
            if (f_sig2_len < lmin) lmin = f_sig2_len;
            if (f_sig2_len > lmax) lmax = f_sig2_len;
            lsum += (double)f_sig2_len;
        });
        printf("# SIGLEN FN-DSA 512: mean=%.1f min=%u max=%u bytes "
               "(variable-length)\n", lsum / N_SIGN,
               (unsigned)lmin, (unsigned)lmax);
    }

    printf("# CORRECTNESS: FN-DSA-512 %s\n",
           PQCLEAN_FALCON512_CLEAN_crypto_sign_verify(f_sig, f_sig_len,
                                                      msg, msg_len, f_pk) == 0
           ? "OK" : "FAILED!");

    BENCH_STACK("FN-DSA", "verify", 512, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_verify(f_sig, f_sig_len,
                                                   msg, msg_len, f_pk);
    });
    BENCH_STACK("FN-DSA", "keygen", 512, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_keypair(f_pk2, f_sk2);
    });
    BENCH_STACK("FN-DSA", "sign", 512, {
        PQCLEAN_FALCON512_CLEAN_crypto_sign_signature(f_sig2, &f_sig2_len,
                                                      msg, msg_len, f_sk);
    });
}
#endif

/* ============= SLH-DSA-128s (SPHINCS+-SHA2-128s-simple, FIPS 205) ========= */
#if RUN_SPHINCS
static uint8_t s_pk[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t s_sk[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t s_sig[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_BYTES];
static size_t  s_sig_len = 0;
static uint8_t s_pk2[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t s_sk2[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t s_sig2[PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_BYTES];
static size_t  s_sig2_len = 0;

static void bench_sphincs128s(void) {
    printf("# --- SLH-DSA-128s (SPHINCS+-SHA2-128s-simple, FIPS 205) ---\n");
    printf("# PK=%d SK=%d SIG=%d bytes\n",
           PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES,
           PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES,
           PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_CRYPTO_BYTES);

    PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_keypair(s_pk, s_sk);
    PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_signature(
        s_sig, &s_sig_len, msg, msg_len, s_sk);

    /* Sample sizes from bench_config.h — see the note there on why n is set by
     * variance rather than a flat 10k for this scheme. */
    BENCH_RUN_N("SLH-DSA", "verify", 128, SPX_N_VERIFY, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_verify(
            s_sig, s_sig_len, msg, msg_len, s_pk);
    });
    BENCH_RUN_N("SLH-DSA", "keygen", 128, SPX_N_KEYGEN, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_keypair(s_pk2, s_sk2);
    });
    BENCH_RUN_N("SLH-DSA", "sign", 128, SPX_N_SIGN, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_signature(
            s_sig2, &s_sig2_len, msg, msg_len, s_sk);
    });

    printf("# CORRECTNESS: SLH-DSA-128s %s\n",
           PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_verify(
               s_sig, s_sig_len, msg, msg_len, s_pk) == 0 ? "OK" : "FAILED!");

    BENCH_STACK("SLH-DSA", "verify", 128, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_verify(
            s_sig, s_sig_len, msg, msg_len, s_pk);
    });
    BENCH_STACK("SLH-DSA", "keygen", 128, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_keypair(s_pk2, s_sk2);
    });
    BENCH_STACK("SLH-DSA", "sign", 128, {
        PQCLEAN_SPHINCSSHA2128SSIMPLE_CLEAN_crypto_sign_signature(
            s_sig2, &s_sig2_len, msg, msg_len, s_sk);
    });
}
#endif

/* ================= ECDSA P-256 (classical baseline, mbedTLS) ============== */
#if RUN_ECDSA
static uint8_t rosc_byte(void) {
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) b = (uint8_t)((b << 1) | (rosc_hw->randombit & 1));
    return b;
}
static int pico_rng(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    for (size_t i = 0; i < len; i++) buf[i] = rosc_byte();
    return 0;
}

static uint8_t e_hash[32];
static uint8_t e_sig[MBEDTLS_ECDSA_MAX_LEN];
static size_t  e_sig_len = 0;
static mbedtls_ecdsa_context e_ctx;

static void bench_ecdsa(void) {
    printf("# --- ECDSA P-256 (classical baseline, mbedTLS) ---\n");

    mbedtls_sha256(msg, msg_len, e_hash, 0);
    mbedtls_ecdsa_init(&e_ctx);
    mbedtls_ecdsa_genkey(&e_ctx, MBEDTLS_ECP_DP_SECP256R1, pico_rng, NULL);
    mbedtls_ecdsa_write_signature(&e_ctx, MBEDTLS_MD_SHA256, e_hash, 32,
                                  e_sig, sizeof(e_sig), &e_sig_len,
                                  pico_rng, NULL);

    {
        static mbedtls_ecdsa_context c;
        mbedtls_ecdsa_init(&c);
        BENCH_RUN("ECDSA-P256", "keygen", 256, {
            mbedtls_ecdsa_genkey(&c, MBEDTLS_ECP_DP_SECP256R1, pico_rng, NULL);
        });
        BENCH_STACK("ECDSA-P256", "keygen", 256, {
            mbedtls_ecdsa_genkey(&c, MBEDTLS_ECP_DP_SECP256R1, pico_rng, NULL);
        });
        mbedtls_ecdsa_free(&c);
    }
    {
        static uint8_t sig[MBEDTLS_ECDSA_MAX_LEN]; static size_t slen;
        BENCH_RUN("ECDSA-P256", "sign", 256, {
            mbedtls_ecdsa_write_signature(&e_ctx, MBEDTLS_MD_SHA256, e_hash, 32,
                                          sig, sizeof(sig), &slen,
                                          pico_rng, NULL);
        });
        BENCH_STACK("ECDSA-P256", "sign", 256, {
            mbedtls_ecdsa_write_signature(&e_ctx, MBEDTLS_MD_SHA256, e_hash, 32,
                                          sig, sizeof(sig), &slen,
                                          pico_rng, NULL);
        });
    }
    BENCH_RUN("ECDSA-P256", "verify", 256, {
        mbedtls_ecdsa_read_signature(&e_ctx, e_hash, 32, e_sig, e_sig_len);
    });
    BENCH_STACK("ECDSA-P256", "verify", 256, {
        mbedtls_ecdsa_read_signature(&e_ctx, e_hash, 32, e_sig, e_sig_len);
    });

    printf("# ECDSA-P256 sig_len=%u bytes\n", (unsigned)e_sig_len);
    printf("# CORRECTNESS: ECDSA-P256 %s\n",
           mbedtls_ecdsa_read_signature(&e_ctx, e_hash, 32,
                                        e_sig, e_sig_len) == 0
           ? "OK" : "FAILED!");
}
#endif

int main(void) {
    bench_init(msg_len);
    bench_csv_header();

#if RUN_MLKEM
    bench_mlkem512();
#endif
#if RUN_ECDSA
    bench_ecdsa();
#endif
#if RUN_SPHINCS
    bench_sphincs128s();
#endif
#if RUN_FALCON
    bench_falcon512();
#endif
#if RUN_MLDSA
    bench_mldsa44();      /* 10k signing — long pole, keep last */
#endif

    printf("# === RP2040 benchmarks complete ===\n");
    while (true) sleep_ms(10000);
    return 0;
}
