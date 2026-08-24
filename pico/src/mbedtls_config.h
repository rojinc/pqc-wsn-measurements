/*
 * mbedtls_config.h — Custom mbedTLS configuration for PQC benchmarks on RP2040
 *
 * This file is used by pico-sdk's pico_mbedtls integration.
 * We enable only what's needed: RSA-2048, ECDSA P-256, ECDH P-256.
 */

#include <limits.h>

/* Bare-metal: no OS entropy, use hardware RNG via pico_rand */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Allow direct struct access (needed for our benchmarks) */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

/* ---- Algorithms we need ---- */

/* RSA-2048 */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_GENPRIME

/* ECDSA / ECDH P-256 */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

/* Matched to the ESP32 build (CONFIG_MBEDTLS_ECP_FIXED_POINT_OPTIM=y).
 * This file fully replaces mbedTLS's default config, so anything not defined
 * here is OFF — which is how this option silently differed between platforms.
 *
 * The classical baseline is held to the same standard as the PQC schemes:
 * identical algorithm, identical options, no hardware acceleration on either
 * side. Correspondingly DISABLED on ESP32 as of 2026-07-28:
 *   CONFIG_MBEDTLS_ECDSA_DETERMINISTIC  (RFC 6979 — a DIFFERENT signing
 *                                        procedure from this build's random k)
 *   CONFIG_MBEDTLS_HARDWARE_MPI         (bignum accelerator; no RP2040 analogue)
 * See docs/FINDINGS_build_asymmetry.md. */
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 1

/* ---- Required dependencies ---- */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_PLATFORM_C

/* Save flash on M0+ */
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_SHA256_SMALLER

/* SHA-256 hardware acceleration on RP2040 if available */
#if LIB_PICO_SHA256
#define MBEDTLS_SHA256_ALT
#endif
